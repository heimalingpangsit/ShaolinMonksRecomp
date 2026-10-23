#include "routines.h"
#include "routine_bridge.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
namespace shaolin::mods {
namespace {
constexpr size_t ModuleLimit=8*1024*1024,TotalLimit=64*1024*1024;
constexpr int InstructionLimit=100000;
struct Budget {std::atomic<size_t> bytes{0};};
struct Write {uint32_t address;std::array<uint8_t,4> data;unsigned size;};
struct Session {
    std::span<uint8_t> memory;
    const MemoryCheck& check;
    std::vector<Write> writes;
    X86Context registers;
    int callback=LUA_NOREF;
    unsigned return_bytes=0;
    bool Valid(uint32_t address,unsigned size)const {
        if(address>memory.size()||size>memory.size()-address)return false;
        if(!check)return true;
        try{return check(address,size);}catch(...){return false;}
    }
};
struct Module {
    Budget& total;
    size_t bytes=0;
    lua_State* lua=nullptr;
    std::mutex mutex;
    std::string id;
    RoutineLog log;
    Session* session=nullptr;
    int remaining=InstructionLimit;
    bool failed=false;
    explicit Module(Budget& budget):total(budget){}
    ~Module(){if(lua)lua_close(lua);}
    static void* Allocate(void* user,void* pointer,size_t old,size_t size) {
        auto& module=*static_cast<Module*>(user);if(!pointer)old=0;
        if(!size){std::free(pointer);module.bytes-=old;module.total.bytes.fetch_sub(old);return nullptr;}
        if(size>ModuleLimit||module.bytes-old>ModuleLimit-size)return nullptr;
        if(size>old) {
            const auto growth=size-old;
            auto used=module.total.bytes.load();
            do {if(growth>TotalLimit-used)return nullptr;}while(!module.total.bytes.compare_exchange_weak(used,used+growth));
        }
        auto* result=std::realloc(pointer,size);
        if(!result){if(size>old)module.total.bytes.fetch_sub(size-old);return nullptr;}
        if(old>size)module.total.bytes.fetch_sub(old-size);
        module.bytes=module.bytes-old+size;return result;
    }
    static Module& Self(lua_State* lua){return **static_cast<Module**>(lua_getextraspace(lua));}
    static void Tick(lua_State* lua,lua_Debug*) {
        auto& module=Self(lua);module.remaining-=1000;
        if(module.remaining<=0)luaL_error(lua,"Routine instruction budget exceeded");
    }
    void ResetBudget(){remaining=InstructionLimit;lua_sethook(lua,Tick,LUA_MASKCOUNT,1000);}
    void Fail(std::string message) {
        failed=true;
        if(log)try{log(id+": "+message+". Script disabled for this session.");}catch(...){}
    }
    static uint32_t U32(lua_State* lua,int at) {
        const auto value=luaL_checkinteger(lua,at);
        if(value<0||uint64_t(value)>UINT32_MAX)luaL_error(lua,"Expected an unsigned 32-bit value");
        return uint32_t(value);
    }
    static Session& Current(lua_State* lua) {
        auto& module=Self(lua);
        if(!module.session)luaL_error(lua,"Guest memory is available only inside a routine callback");
        return *module.session;
    }
    static int ReadMemory(lua_State* lua,unsigned width,bool floating=false) {
        const auto address=U32(lua,1);auto& session=Current(lua);
        if(!session.Valid(address,width))return luaL_error(lua,"Guest memory read is outside committed memory");
        uint32_t value=0;
        for(unsigned i=0;i<width;++i) {
            auto byte=session.memory[address+i];
            for(const auto& write:session.writes)if(address+i>=write.address&&address+i-write.address<write.size)
                byte=write.data[address+i-write.address];
            value|=uint32_t(byte)<<(i*8);
        }
        if(floating){float number;std::memcpy(&number,&value,4);lua_pushnumber(lua,number);}
        else lua_pushinteger(lua,value);
        return 1;
    }
    static int WriteMemory(lua_State* lua,unsigned width,bool floating=false) {
        const auto address=U32(lua,1);uint32_t value;
        if(floating){const float number=float(luaL_checknumber(lua,2));std::memcpy(&value,&number,4);}
        else {value=U32(lua,2);if(width<4&&value>=(1u<<(width*8)))return luaL_error(lua,"Value exceeds the selected memory width");}
        auto& session=Current(lua);
        if(!session.Valid(address,width))return luaL_error(lua,"Guest memory write is outside committed memory");
        if(session.writes.size()>=256)return luaL_error(lua,"Routine memory write budget exceeded");
        Write write{address,{},width};for(unsigned i=0;i<width;++i)write.data[i]=uint8_t(value>>(8*i));
        bool allocated=true;try{session.writes.push_back(write);}catch(...){allocated=false;}
        if(!allocated)return luaL_error(lua,"Cannot stage routine memory write");
        return 0;
    }
    static int Read8(lua_State* lua){return ReadMemory(lua,1);}
    static int Read16(lua_State* lua){return ReadMemory(lua,2);}
    static int Read32(lua_State* lua){return ReadMemory(lua,4);}
    static int ReadFloat(lua_State* lua){return ReadMemory(lua,4,true);}
    static int Write8(lua_State* lua){return WriteMemory(lua,1);}
    static int Write16(lua_State* lua){return WriteMemory(lua,2);}
    static int Write32(lua_State* lua){return WriteMemory(lua,4);}
    static int WriteFloat(lua_State* lua){return WriteMemory(lua,4,true);}
    static int Initialize(lua_State* lua) {
        for(const auto& library:std::array<std::pair<const char*,lua_CFunction>,5>{{
            {LUA_GNAME,luaopen_base},{LUA_TABLIBNAME,luaopen_table},{LUA_STRLIBNAME,luaopen_string},
            {LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8}}}) {
            luaL_requiref(lua,library.first,library.second,1);lua_pop(lua,1);
        }
        // Protected calls inside a script could catch the instruction-limit error.
        for(const auto* name:{"dofile","loadfile","load","collectgarbage","pcall","xpcall","print","warn","setmetatable","getmetatable"}) {
            lua_pushnil(lua);lua_setglobal(lua,name);
        }
        // Native pattern searches do not consume Lua instruction-hook ticks.
        lua_getglobal(lua,"string");
        for(const auto* name:{"find","match","gmatch","gsub"}){lua_pushnil(lua);lua_setfield(lua,-2,name);}
        lua_pop(lua,1);
        const luaL_Reg api[]={{"read_u8",Read8},{"read_u16",Read16},{"read_u32",Read32},{"read_f32",ReadFloat},
            {"write_u8",Write8},{"write_u16",Write16},{"write_u32",Write32},{"write_f32",WriteFloat},{nullptr,nullptr}};
        luaL_newlib(lua,api);lua_setglobal(lua,"game");return 0;
    }
    void Open() {
        lua=lua_newstate(Allocate,this);if(!lua)throw std::runtime_error("Cannot allocate routine script state");
        *static_cast<Module**>(lua_getextraspace(lua))=this;
        lua_pushcfunction(lua,Initialize);
        if(lua_pcall(lua,0,0,0)!=LUA_OK)throw std::runtime_error(Error());
        ResetBudget();
    }
    std::string Error()const{const auto* text=lua_type(lua,-1)==LUA_TSTRING?lua_tostring(lua,-1):nullptr;return text?text:"Routine script failed";}
};
struct Hook {Module* module;int before=LUA_NOREF,after=LUA_NOREF,replace=LUA_NOREF;unsigned stack_pop=0;};
struct Register {const char* name;uint32_t X86Context::*member;bool flag=false;};
constexpr std::array<Register,14> Registers{{{"eax",&X86Context::eax},{"ecx",&X86Context::ecx},{"edx",&X86Context::edx},
    {"ebx",&X86Context::ebx},{"esp",&X86Context::esp},{"ebp",&X86Context::ebp},{"esi",&X86Context::esi},{"edi",&X86Context::edi},
    {"cf",&X86Context::cf,true},{"zf",&X86Context::zf,true},{"sf",&X86Context::sf,true},{"of",&X86Context::of,true},
    {"pf",&X86Context::pf,true},{"df",&X86Context::df,true}}};
int ExecuteCallback(lua_State* lua) {
    auto& session=Module::Current(lua);
    lua_newtable(lua);
    for(const auto& reg:Registers){lua_pushinteger(lua,session.registers.*reg.member);lua_setfield(lua,-2,reg.name);}
    lua_rawgeti(lua,LUA_REGISTRYINDEX,session.callback);lua_pushvalue(lua,1);
    lua_call(lua,1,0);
    for(const auto& reg:Registers) {
        lua_getfield(lua,1,reg.name);int integer=0;const auto value=lua_tointegerx(lua,-1,&integer);lua_pop(lua,1);
        if(!integer||value<0||uint64_t(value)>UINT32_MAX||(reg.flag&&value>1))
            return luaL_error(lua,"Invalid guest register value");
        session.registers.*reg.member=uint32_t(value);
    }
    if(!session.Valid(session.registers.esp,std::max(4u,session.return_bytes))||
       session.return_bytes&&session.registers.esp>UINT32_MAX-session.return_bytes)
        return luaL_error(lua,"Invalid guest stack address");
    if(session.return_bytes)session.registers.esp+=session.return_bytes;
    return 0;
}
bool Call(Hook& hook,int reference,X86Context& context,std::span<uint8_t> memory,const MemoryCheck& check,bool replacement=false) {
    if(reference==LUA_NOREF)return false;
    auto& module=*hook.module;std::lock_guard lock(module.mutex);
    if(module.failed)return false;
    auto* lua=module.lua;lua_settop(lua,0);
    Session session{memory,check,{},context,reference,replacement?4+hook.stack_pop:0};module.session=&session;module.ResetBudget();
    // Table allocation and register validation must remain inside the protected call.
    lua_pushcfunction(lua,ExecuteCallback);
    const auto result=lua_pcall(lua,0,0,0);module.session=nullptr;
    if(result!=LUA_OK){module.Fail(module.Error());lua_settop(lua,0);return false;}
    for(const auto& write:session.writes)if(!session.Valid(write.address,write.size)) {
        module.Fail("Guest memory changed before the write committed");lua_settop(lua,0);return false;
    }
    for(const auto& write:session.writes)std::copy_n(write.data.begin(),write.size,memory.begin()+write.address);
    context=session.registers;lua_settop(lua,0);return true;
}
}
struct RoutineEngine::State {
    Budget budget;
    std::vector<std::unique_ptr<Module>> modules;
    std::map<uint32_t,std::vector<Hook>> hooks;
};
RoutineEngine::RoutineEngine()=default;
RoutineEngine::~RoutineEngine()=default;
bool RoutineEngine::Load(const std::vector<RoutineScript>& scripts,std::span<const uint32_t> targets,const RoutineLog& log,std::string& error){
    error.clear();
    try {
        if(scripts.size()>64)throw std::runtime_error("At most 64 routine entry scripts can be active");
        auto next=std::make_unique<State>();const std::set<uint32_t> allowed(targets.begin(),targets.end());size_t total_hooks=0;
        for(const auto& script:scripts) {
            if(std::filesystem::file_size(script.file)>1024*1024)throw std::runtime_error("Routine scripts must not exceed 1 MiB");
            std::ifstream stream(script.file,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read routine script");
            const std::string text{std::istreambuf_iterator<char>(stream),{}};
            auto module=std::make_unique<Module>(next->budget);module->id=script.mod_id;module->log=log;module->Open();auto* lua=module->lua;
            if(luaL_loadbufferx(lua,text.data(),text.size(),script.mod_id.c_str(),"t")!=LUA_OK||lua_pcall(lua,0,1,0)!=LUA_OK)
                throw std::runtime_error(script.mod_id+": "+module->Error());
            struct Parsing {State* next;Module* module;const std::set<uint32_t>* allowed;size_t* count;std::string error;};
            Parsing parsing{next.get(),module.get(),&allowed,&total_hooks,{}};
            lua_pushcfunction(lua,(+[](lua_State* lua)->int {
              auto& parsing=*static_cast<Parsing*>(lua_touserdata(lua,1));lua_remove(lua,1);
              auto* next=parsing.next;auto* module=parsing.module;
              const auto& allowed=*parsing.allowed;auto& total_hooks=*parsing.count;
              try {
            if(!lua_istable(lua,-1))throw std::runtime_error("A routine script must return an array of hooks");
            const auto count=lua_rawlen(lua,-1);if(count>512||total_hooks+count>4096)throw std::runtime_error("Routine hook count exceeds its limit");
            total_hooks+=count;
            for(size_t i=1;i<=count;++i) {
                lua_rawgeti(lua,1,lua_Integer(i));if(!lua_istable(lua,-1))throw std::runtime_error("Invalid routine hook entry");
                lua_getfield(lua,-1,"address");int integer=0;const auto address=lua_tointegerx(lua,-1,&integer);lua_pop(lua,1);
                if(!integer||address<=0||uint64_t(address)>UINT32_MAX||!allowed.contains(uint32_t(address)))
                    throw std::runtime_error(module->id+": routine target is unavailable or belongs to host compatibility code");
                Hook hook{module};lua_getfield(lua,-1,"stack_pop");
                if(!lua_isnil(lua,-1)) {
                    const auto value=lua_tointegerx(lua,-1,&integer);
                    if(!integer||value<0||value>1024||value%4)throw std::runtime_error("stack_pop must be a multiple of four from 0 to 1024");
                    hook.stack_pop=unsigned(value);
                }
                lua_pop(lua,1);
                for(const auto& [name,field]:std::array<std::pair<const char*,int*>,3>{{{"before",&hook.before},{"after",&hook.after},{"replace",&hook.replace}}}) {
                    lua_getfield(lua,-1,name);
                    if(lua_isnil(lua,-1))lua_pop(lua,1);
                    else {if(!lua_isfunction(lua,-1))throw std::runtime_error("Routine callbacks must be functions");*field=luaL_ref(lua,LUA_REGISTRYINDEX);}
                }
                if(hook.before==LUA_NOREF&&hook.after==LUA_NOREF&&hook.replace==LUA_NOREF)throw std::runtime_error("A routine hook needs a before, after or replace callback");
                auto& hooks=next->hooks[uint32_t(address)];
                if(hook.replace!=LUA_NOREF&&std::any_of(hooks.begin(),hooks.end(),[](auto& h){return h.replace!=LUA_NOREF;}))
                    throw std::runtime_error("Enabled mods replace the same routine");
                hooks.push_back(hook);lua_pop(lua,1);
            }
              }catch(const std::exception& e){parsing.error=e.what();}
              return 0;
            }));
            lua_pushlightuserdata(lua,&parsing);lua_pushvalue(lua,1);
            if(lua_pcall(lua,2,0,0)!=LUA_OK)throw std::runtime_error(script.mod_id+": "+module->Error());
            if(!parsing.error.empty())throw std::runtime_error(parsing.error);
            lua_settop(lua,0);next->modules.push_back(std::move(module));
        }
        state_=std::move(next);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RoutineEngine::Invoke(uint32_t address,X86Context& ctx,std::span<uint8_t> memory,const MemoryCheck& check,X86Func* original){
    if(!state_){original(ctx,memory.data());return;}
    const auto found=state_->hooks.find(address);if(found==state_->hooks.end()){original(ctx,memory.data());return;}
    for(auto& hook:found->second)Call(hook,hook.before,ctx,memory,check);
    bool replaced=false;
    for(auto& hook:found->second)if(hook.replace!=LUA_NOREF)replaced=Call(hook,hook.replace,ctx,memory,check,true);
    if(!replaced)original(ctx,memory.data());
    for(auto& hook:found->second)Call(hook,hook.after,ctx,memory,check);
}
std::vector<uint32_t> RoutineEngine::Targets()const{std::vector<uint32_t> targets;if(state_)for(const auto& [address,hooks]:state_->hooks)targets.push_back(address);return targets;}
namespace {
struct Routing {
    std::vector<uint8_t> flags;
    RoutineRoute route{};
    RoutineEngine* engine;
    std::span<uint8_t> memory;
    MemoryCheck check;
};
std::unique_ptr<Routing> routing;
}
std::atomic<const RoutineRoute*> CurrentRoutines{nullptr};
void ClearRoutines(){CurrentRoutines.store(nullptr,std::memory_order_release);routing.reset();}
void PublishRoutines(RoutineEngine& engine,std::span<uint8_t> memory,const MemoryCheck& check) {
    ClearRoutines();const auto targets=engine.Targets();if(targets.empty())return;
    const auto limit=*std::max_element(targets.begin(),targets.end());
    if(limit>=16*1024*1024)throw std::runtime_error("Routine target exceeds the supported game code range");
    auto next=std::make_unique<Routing>();next->engine=&engine;next->memory=memory;next->check=check;
    next->flags.resize(size_t(limit)+1);for(const auto target:targets)next->flags[target]=1;
    next->route={uint32_t(next->flags.size()),next->flags.data()};routing=std::move(next);
    CurrentRoutines.store(&routing->route,std::memory_order_release);
}
void InvokeRouted(uint32_t address,X86Context& ctx,uint8_t* base,X86Func* original) {
    if(routing&&routing->memory.data()==base)routing->engine->Invoke(address,ctx,routing->memory,routing->check,original);
    else original(ctx,base);
}
}

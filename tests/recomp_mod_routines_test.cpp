#include "mods/routines.h"
#include "mods/routine_bridge.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
using namespace shaolin::mods;
void Check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void Original(X86Context& ctx,uint8_t*){ctx.eax+=10;ctx.esp+=4;}
X86_FUNC(__imp__sub_00000100){Original(ctx,base);}
X86_WEAK_FUNC(sub_00000100,__imp__sub_00000100)
int main(){
    const auto root=fs::temp_directory_path()/("shaolin-routines-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try{
        const auto script=root/"gameplay.lua";
        std::ofstream(script)<<R"(return {
          {address=0x100, before=function(ctx) ctx.eax=ctx.eax+2; game.write_u32(100,77) end,
           after=function(ctx) ctx.eax=ctx.eax*3 end},
          {address=0x200, stack_pop=8, replace=function(ctx) ctx.eax=42 end}
        })";
        RoutineEngine engine;std::string error;
        const auto load=[&](const std::vector<RoutineScript>& scripts,std::span<const uint32_t> allowed){
            if(!engine.Load(scripts,allowed,{},error))throw std::runtime_error("Routine loading failed: "+error);
        };
        const std::array<uint32_t,2> targets{0x100,0x200};
        load({RoutineScript{"Gameplay",script}},targets);
        std::array<uint8_t,256> memory{};
        X86Context ctx{};ctx.eax=1;ctx.esp=64;
        engine.Invoke(0x100,ctx,memory,{},Original);
        Check(ctx.eax==39&&ctx.esp==68&&memory[100]==77,"Before/after hooks must affect real guest state around the original function");
        PublishRoutines(engine,memory,{});
        ctx.eax=1;ctx.esp=64;sub_00000100(ctx,memory.data());
        Check(ctx.eax==39&&ctx.esp==68,"Generated direct function calls must pass through the routine hook bridge");
        ClearRoutines();ctx.eax=1;ctx.esp=64;sub_00000100(ctx,memory.data());
        Check(ctx.eax==11,"Disabling routine mods must restore generated calls without changing game code");
        ctx.eax=1;ctx.esp=64;engine.Invoke(0x200,ctx,memory,{},Original);
        Check(ctx.eax==42&&ctx.esp==76,"A replacement must skip the original and apply its guest return convention");
        ctx.eax=1;ctx.esp=64;engine.Invoke(0x300,ctx,memory,{},Original);
        Check(ctx.eax==11&&ctx.esp==68,"Unhooked routines must preserve original behavior");
        std::atomic<unsigned> failures{0};
        std::vector<std::jthread> threads;
        for(int thread=0;thread<4;++thread)threads.emplace_back([&]{for(int i=0;i<100;++i){
            X86Context local{};local.eax=1;local.esp=64;engine.Invoke(0x100,local,memory,{},Original);
            if(local.eax!=39||local.esp!=68)++failures;
        }});
        threads.clear();Check(failures==0,"Concurrent guest threads must not corrupt Lua state or guest registers");
        const auto broken=root/"broken.lua";
        std::ofstream(broken)<<R"(return {{address=0x100, before=function(ctx) ctx.eax=999; game.write_u32(104,999); game.write_u32(99999,1) end}})";
        load({RoutineScript{"Broken",broken}},targets);
        ctx.eax=1;ctx.esp=64;engine.Invoke(0x100,ctx,memory,{},Original);
        Check(ctx.eax==11&&memory[104]==0,"Failed hooks must roll back their registers and staged memory writes");
        std::ofstream(broken)<<R"(return {{address=0x100, before=function(ctx) ctx.esp=128; ctx.eax=999 end}})";
        load({RoutineScript{"InvalidStack",broken}},targets);
        ctx.eax=1;ctx.esp=64;
        engine.Invoke(0x100,ctx,memory,[](uint32_t address,unsigned size){return size==0||(address<128&&size<=128-address);},Original);
        Check(ctx.eax==11&&ctx.esp==68,"Hooks must not redirect the guest stack into uncommitted memory");
        const auto loop=root/"loop.lua";
        std::ofstream(loop)<<R"(return {{address=0x100, replace=function(ctx) while true do end end}})";
        load({RoutineScript{"Loop",loop}},targets);
        ctx.eax=1;ctx.esp=64;engine.Invoke(0x100,ctx,memory,{},Original);
        Check(ctx.eax==11&&ctx.esp==68,"Instruction exhaustion must disable the script and run the original routine");
        const auto sandbox=root/"sandbox.lua";
        std::ofstream(sandbox)<<R"(assert(io==nil and os==nil and package==nil and debug==nil and dofile==nil and loadfile==nil); return {})";
        Check(engine.Load({RoutineScript{"Sandbox",sandbox}},targets,{},error),"Scripts must not expose host file or process APIs");
        Check(!engine.Load({RoutineScript{"InvalidTarget",script}},std::array<uint32_t,1>{0x999},{},error),"Unknown or host-owned function targets must fail before gameplay");
        Check(!engine.Load({RoutineScript{"One",script},RoutineScript{"Two",script}},targets,{},error),"Two replacements of one function must conflict");
        std::ofstream(loop)<<"while true do end";
        Check(!engine.Load({RoutineScript{"StartupLoop",loop}},targets,{},error),"Startup code must also obey the instruction budget");
        std::ofstream(loop)<<"local t={} while true do t[#t+1]=string.rep('x',10000) end";
        Check(!engine.Load({RoutineScript{"MemoryLimit",loop}},targets,{},error),"Scripts must obey a bounded memory budget");
        std::ofstream(loop)<<"return string.rep('x',16*1024*1024)";
        Check(!engine.Load({RoutineScript{"AllocationLimit",loop}},targets,{},error),"One native library allocation must also obey the memory budget");
        unsigned pressure_success=0,pressure_failure=0;
        const auto pressure=[&](unsigned bytes) {
            std::ofstream(loop)<<"local bytes="<<bytes<<R"(
                local keep={} for i=1,math.floor(bytes/32768) do keep[i]=string.rep('x',32768)..i end
                keep[#keep+1]=string.rep('x',bytes%32768)
                local fn=function(ctx) if #keep==0 then ctx.eax=0 end end
                local hooks={} for i=1,512 do hooks[i]={address=0x100,before=fn,after=fn} end
                return hooks)";
            // Both success and rejection are valid. Neither may escape the protected Lua boundary.
            const bool ok=engine.Load({RoutineScript{"MetadataPressure",loop}},targets,{},error);
            if(ok)++pressure_success;else ++pressure_failure;
            return ok;
        };
        for(unsigned bytes=7*1024*1024;bytes<8*1024*1024;bytes+=4096)pressure(bytes);
        Check(pressure_success&&pressure_failure,"Memory pressure fixtures must exercise accepted and rejected scripts");
        unsigned lower=7*1024*1024,upper=8*1024*1024;
        while(upper-lower>1){const auto middle=lower+(upper-lower)/2;if(pressure(middle))lower=middle;else upper=middle;}
        fs::remove_all(root);std::cout<<"Routine hook checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ignored;fs::remove_all(root,ignored);return 1;}
}

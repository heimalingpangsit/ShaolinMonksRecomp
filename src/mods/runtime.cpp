#include "runtime.h"
#include "kernel/memory.h"
#include "core/log.h"
namespace shaolin::mods {
namespace {
std::vector<RoutineScript> scripts;
RoutineEngine engine;
}
void StageRoutines(std::vector<RoutineScript> next){ClearRoutines();scripts=std::move(next);}
bool StartRoutines(uint8_t* memory,std::string& error) {
    try {
        if(!engine.Load(scripts,CompiledRoutineTargets(),[](std::string_view message){LOGW("Mod routine: %.*s",int(message.size()),message.data());},error))return false;
        const MemoryCheck check=[](uint32_t at,uint32_t size) {
            const uint64_t end=uint64_t(at)+size;if(end>uint64_t(UINT32_MAX)+1)return false;
            for(uint64_t page=uint64_t(at)&~uint64_t(0xfff);page<end;page+=0x1000)
                if(!mem::IsCommitted(uint32_t(page)))return false;
            return true;
        };
        PublishRoutines(engine,{memory,size_t(UINT32_MAX)+1},check);
        if(!scripts.empty())LOGI("Mods: %zu routine scripts active",scripts.size());
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}

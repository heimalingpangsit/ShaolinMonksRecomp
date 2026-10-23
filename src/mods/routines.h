#pragma once
#include "cpu/x86_context.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace shaolin::mods {
struct RoutineScript {std::string mod_id;std::filesystem::path file;};
using MemoryCheck=std::function<bool(uint32_t address,uint32_t size)>;
using RoutineLog=std::function<void(std::string_view)>;
class RoutineEngine {
    struct State;
    std::unique_ptr<State> state_;
public:
    RoutineEngine();
    ~RoutineEngine();
    bool Load(const std::vector<RoutineScript>& scripts,std::span<const uint32_t> targets,
              const RoutineLog& log,std::string& error);
    void Invoke(uint32_t address,X86Context& ctx,std::span<uint8_t> memory,
                const MemoryCheck& check,X86Func* original);
    std::vector<uint32_t> Targets() const;
};
void PublishRoutines(RoutineEngine& engine,std::span<uint8_t> memory,const MemoryCheck& check);
void ClearRoutines();
std::span<const uint32_t> CompiledRoutineTargets();
}

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
struct X86Context {uint32_t esp=0x1100,eax=0,ecx=0;};
namespace shaolin::preferences { void Apply(X86Context&, uint8_t*) {} }
std::array<uint8_t,4096> bytes{};
uint32_t selected=0,top_menu=0x598430;
namespace kernel {
uint32_t R32(uint32_t p) {
    if(p==0x598468)return selected;
    if(p==0x6C9308)return 1;
    if(p==0x6C930C)return top_menu;
    uint32_t v;std::memcpy(&v,bytes.data()+p-0x1000,4);return v;
}
void W32(uint32_t p,uint32_t v) {std::memcpy(bytes.data()+p-0x1000,&v,4);}
}
std::vector<bool> modes;
bool original_called=false;
bool frontend_poll=false;
namespace shaolin::controller {
void SetMultiplayer(bool value) {if(!original_called && !frontend_poll)std::abort();modes.push_back(value);}
}
#define X86_FUNC(name) void name(X86Context& ctx,uint8_t* base)
#define X86_EXTERN_FUNC(name) X86_FUNC(name)
#define LOG_FIRST(...) ((void)0)
#define LOGI(...) ((void)0)
X86_FUNC(__imp__sub_0001670D) {original_called=true;ctx.eax=kernel::R32(ctx.esp+4)+10;ctx.esp+=4;}
X86_FUNC(__imp__sub_00015C86) {original_called=true;top_menu=kernel::R32(ctx.esp+4);ctx.eax=0xCAFE;ctx.ecx=0xDEADBEEF;ctx.esp+=12;}
#include "controller_mode.inl"
void Check(bool ok,const char* text) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main() {
    X86Context menu;menu.ecx=0x598430;selected=1;kernel::W32(menu.esp+4,0x598DF0);
    sub_00015C86(menu,nullptr);
    Check(!modes.empty() && modes.back(),"entering original retail Ko-op selector separates players before confirming their characters");
    Check(menu.eax==0xCAFE && menu.ecx==0xDEADBEEF && menu.esp==0x110C,"retail menu hook preserves registers and RET8");
    for (uint32_t selection:{0u,1u,2u}) {
        modes.clear();original_called=false;menu={};menu.ecx=0x598430;selected=selection;
        kernel::W32(menu.esp+4,0x598DF0);sub_00015C86(menu,nullptr);
        Check(modes.size()==1 && modes.back()==(selection!=0),"character selector uses the confirmed main-menu game type");
    }
    modes.clear();menu={};menu.ecx=0x598430;selected=1;kernel::W32(menu.esp+4,0x598430);sub_00015C86(menu,nullptr);
    Check(modes.size()==1 && !modes.back(),"highlighting Ko-op keeps shared P1 input before confirmation");
    modes.clear();menu={};menu.ecx=0x598430;selected=0;kernel::W32(menu.esp+4,0x598430);sub_00015C86(menu,nullptr);
    Check(modes.size()==1 && !modes.back(),"selecting Single Player restores shared input");
    modes.clear();menu={};menu.ecx=0x123456;kernel::W32(menu.esp+4,0x598DF0);sub_00015C86(menu,nullptr);
    Check(modes.empty(),"other menu instances do not change player assignment");
    // The original main-menu callback does not run again after the character
    // panel opens. XInputGetState must still switch to two independent ports.
    frontend_poll=true;
    original_called=false;
    modes.clear();selected=1;top_menu=0x598430;
    SyncRetailFrontendMode();
    Check(modes.size()==1 && !modes.back(),"highlighting Ko-op does not split ports");
    modes.clear();top_menu=0x598DF0;
    SyncRetailFrontendMode();
    Check(modes.size()==1 && modes.back(),"first character-screen state poll separates keyboard P1 and gamepad P2 without menu callback");
    modes.clear();selected=0;SyncRetailFrontendMode();
    Check(modes.size()==1 && !modes.back(),"single-player selector retains shared P1");
    modes.clear();selected=1;top_menu=0x123456;SyncRetailFrontendMode();
    Check(modes.empty(),"other menu stacks do not override routing");
    frontend_poll=false;
    for (uint32_t mode:{0u,1u,2u,7u}) {
        modes.clear();original_called=false;X86Context ctx;
        kernel::W32(ctx.esp+4,mode);sub_0001670D(ctx,nullptr);
        Check(original_called,"original mode setter is preserved");
        if (mode<=2) Check(modes.size()==1 && modes.back()==(mode!=0),"actual game mode selects shared or split input");
        else Check(modes.empty(),"unknown mode does not invent an input assignment");
        Check(ctx.eax==mode+10 && ctx.esp==0x1104,"cdecl mode wrapper preserves return value and stack");
    }
    std::puts("PASS: original mode setter, solo/Ko-op/versus routing and preserved ABI");
}

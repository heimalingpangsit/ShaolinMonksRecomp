#include "game_preferences.h"
// Audited against the SHA-256 pinned by CMakeLists.txt.
// The retail frontend calls 1670D -> 73D70 with 0=solo, 1=Ko-op, 2=versus.
// Preserve its work and ABI, then synchronize the native device assignment.
// Character selection precedes that setter. Observe the retail main menu's
// stack transition (main 598430, characters 598DF0) and its selection at +38.
// The main-menu callback stops when the character panel takes ownership.
// Observe the guest menu stack again from XInputGetState before its Poll.
void SyncRetailFrontendMode() {
    const uint32_t depth=kernel::R32(0x6C9308);
    if (!depth || depth>16) return;
    const uint32_t top=kernel::R32(0x6C9308+depth*4);
    if (top==0x598430) shaolin::controller::SetMultiplayer(false);
    else if (top==0x598DF0) {
        const uint32_t selected=kernel::R32(0x598430+0x38);
        shaolin::controller::SetMultiplayer(selected==1 || selected==2);
    }
}
X86_EXTERN_FUNC(__imp__sub_00015C86);
X86_FUNC(sub_00015C86) {
    const uint32_t menu=ctx.ecx;
    __imp__sub_00015C86(ctx,base);
    LOG_FIRST(4,"input: retail menu callback this=%08X",menu);
    if (menu!=0x598430) return;
    shaolin::preferences::Apply(ctx, base);
    const uint32_t depth=kernel::R32(0x6C9308);
    if (!depth || depth>16) return;
    const uint32_t top=kernel::R32(0x6C9308+depth*4);
    static uint64_t last_menu=~uint64_t{};
    const uint64_t state=(uint64_t(top)<<32)|kernel::R32(menu+0x38);
    if (state!=last_menu) {LOGI("input: retail menu top=%08X selected=%u depth=%u",top,uint32_t(state),depth);last_menu=state;}
    SyncRetailFrontendMode();
}
X86_EXTERN_FUNC(__imp__sub_0001670D);
X86_FUNC(sub_0001670D) {
    const uint32_t mode=kernel::R32(ctx.esp+4);
    __imp__sub_0001670D(ctx,base);
    if (mode<=2) shaolin::controller::SetMultiplayer(mode!=0);
}

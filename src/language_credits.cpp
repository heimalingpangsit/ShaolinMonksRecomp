#include "language_credits.h"
#include "cpu/x86_ops.h"
#include <cstring>
#include <limits>

namespace {
uint32_t Read(const uint8_t* base, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, base + address, sizeof(value));
    return value;
}
void Write(uint8_t* base, uint32_t address, uint32_t value) {
    std::memcpy(base + address, &value, sizeof(value));
}
}

X86_EXTERN_FUNC(__imp__sub_000EBD00);
X86_EXTERN_FUNC(__imp__sub_00152960);
X86_EXTERN_FUNC(__imp__sub_0015ABF0);

// The audited credits renderer compares this prefix before choosing font style.
// Other uses of '#' include controller tokens and must retain their original path.
X86_FUNC(sub_000EBD00) {
    if (shaolin::language::CreditsEnabled() && Read(base, ctx.esp) == 0x4B3E6 &&
        Read(base, ctx.esp + 8) == 0x49D4BC && Read(base, ctx.esp + 12) == 16) {
        const auto line = Read(base, ctx.esp + 4);
        if (line && base[line] == '#') {
            ctx.eax = 0;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_000EBD00(ctx, base);
}

X86_FUNC(sub_00152960) {
    const auto slot = ctx.esp + 20;
    if (shaolin::language::CreditsEnabled() && Read(base, ctx.esp) == 0x4B419 &&
        Read(base, ctx.esp + 16) == 2 && ctx.edi &&
        ctx.edi <= std::numeric_limits<uint32_t>::max() - 16 &&
        Read(base, slot) == ctx.edi + 16 && base[ctx.edi] == '#') {
        // The original LEA still skips 16 bytes. Replace only this draw argument.
        const auto original = Read(base, slot);
        Write(base, slot, ctx.edi + 1);
        __imp__sub_00152960(ctx, base);
        Write(base, slot, original);
        return;
    }
    __imp__sub_00152960(ctx, base);
}

X86_FUNC(sub_0015ABF0) {
    // This return address belongs only to campaign credits update (C5D10).
    // Extras credits have a separate caller and require their manual exit.
    const bool campaign = shaolin::language::CreditsEnabled() &&
        Read(base, ctx.esp) == 0xC5D3D && ctx.ecx == 0x67C658 &&
        Read(base, ctx.esp + 4) == 4;
    __imp__sub_0015ABF0(ctx, base);
    if (campaign) ctx.eax &= ~0x40u;
}

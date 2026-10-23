// This title's public SetEG was absent from the scanner profile. The original
// wrapper and RET8 match the supported executable ABI. The XBE hash is pinned.
#define SYM_IDirectSoundBuffer_SetEG 003FCDAE
BUFFER(SetEG, 2) {
    constexpr uint32_t invalid = 0x80070057u;
    const uint32_t descriptor = a[1];
    if (!descriptor || descriptor > 0xFFFFFFFFu - 39 ||
        !mem::IsCommitted(descriptor) || !mem::IsCommitted(descriptor + 39)) return invalid;
    uint32_t envelope[10];
    std::memcpy(envelope, HostPtr(descriptor), sizeof(envelope));
    constexpr uint32_t off[10] = {1, 0, 0, 0, 0, 0, 0, 255, 0, 0};
    if (std::memcmp(envelope, off, sizeof(off)))
        x86_trap(a.ctx, 0x003FCDAE, "Shaolin buffer envelope: only audited amplitude OFF is supported");
    // OFF applies no envelope multiplier. Preserve the mixer's volume/headroom
    // and current playback position; its dry voice already has this behavior.
    LOG_FIRST(3, "shaolin audio: amplitude envelope OFF; dry voice gain preserved");
    return 0;
}

// Public buffer pitch wrapper, RET8. Pitch is absolute, with zero at 48 kHz;
// multiplying by the file rate applies the rate conversion a second time.
#define SYM_IDirectSoundBuffer_SetPitch 003FCD76
BUFFER(SetPitch, 2) {
    const int32_t pitch = int32_t(a[1]);
    const double frequency = 48000.0 * std::exp2(double(pitch) / 4096.0);
    if (!std::isfinite(frequency) || frequency < 100 || frequency > 192000) return 0x80070057u;
    v.freq = uint32_t(frequency);
    LOG_FIRST(3, "shaolin audio: pitch=%d rate=%u frequency=%u", pitch, v.rate, v.freq);
    return 0;
}

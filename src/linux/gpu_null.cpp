// A renderer that draws nothing. Used for background diagnostics and when the
// PC has no usable Vulkan device; the game keeps running and logs its frames.
#include "gpu/backend.h"
#include "core/log.h"

namespace gpu {
namespace {
class NullBackend final : public Backend {
public:
    bool Init(uint32_t width, uint32_t height) override {
        LOGR("gpu: null renderer (%ux%u), no picture", width, height);
        return true;
    }
    void Clear(const State&, uint32_t, const uint8_t*, uint32_t, uint32_t, float, uint32_t) override {}
    void Draw(const State&, uint32_t, uint32_t, uint32_t, const uint8_t*) override {}
    void Copy(const Surface&, const Surface&, uint32_t, uint32_t, uint32_t) override {}
    void Present(const Surface&) override {
        if (++presents_ % 600 == 0) LOGI("gpu: null renderer, %llu frames", static_cast<unsigned long long>(presents_));
    }
    void Sync(uint32_t, uint32_t) override {}
    void SyncLate(uint32_t) override {}
    void Flush() override {}
    void SetGamma(const uint8_t*) override {}

private:
    uint64_t presents_ = 0;
};
}  // namespace

std::unique_ptr<Backend> CreateNullBackend() { return std::make_unique<NullBackend>(); }
}  // namespace gpu

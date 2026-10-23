// Renderer choice on Linux: Vulkan, or the null renderer for background
// diagnostics, SHAOLIN_RENDERER=null, or a PC without a usable Vulkan device.
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "gpu/backend.h"
#include "host.h"
#include "linux/linux_platform.h"

namespace gpu {
std::unique_ptr<Backend> CreateNullBackend();
std::unique_ptr<Backend> CreateVulkanBackend();  // null when built without Vulkan

namespace {
// Vulkan, falling back to the null renderer when it cannot start: the game keeps running and says why
class Fallback final : public Backend {
public:
    explicit Fallback(std::unique_ptr<Backend> inner) : inner_(std::move(inner)) {}
    bool Init(uint32_t w, uint32_t h) override {
        if (inner_->Init(w, h)) return true;
        LOGE("gpu: Vulkan could not start; running without a picture");
        linux_platform::ShowError("Shaolin Monks Recomp",
                                  "Vulkan could not start on this PC, so the game has no picture.\n\n"
                                  "Install or update the Vulkan driver for your graphics card (Mesa for AMD and "
                                  "Intel, the NVIDIA driver for NVIDIA) and try again. The log has the details.");
        inner_ = CreateNullBackend();
        return inner_->Init(w, h);
    }
    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
               uint32_t stencil) override {
        inner_->Clear(s, count, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        inner_->Draw(s, prim, start, count, indices);
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override {
        inner_->Copy(src, dst, count, src_rects, dst_points);
    }
    void Present(const Surface& front) override { inner_->Present(front); }
    void Sync(uint32_t data, uint32_t size) override { inner_->Sync(data, size); }
    void SyncLate(uint32_t data) override { inner_->SyncLate(data); }
    bool DeliverLate(uint32_t data) override { return inner_->DeliverLate(data); }
    void RecordLate(uint32_t data) override { inner_->RecordLate(data); }
    void Flush() override { inner_->Flush(); }
    void SetGamma(const uint8_t* ramp) override { inner_->SetGamma(ramp); }
    void ClearReport() override { inner_->ClearReport(); }
    void GetReport(uint32_t report) override { inner_->GetReport(report); }
    uint32_t Fence() override { return inner_->Fence(); }
    bool FencePending(uint32_t f) override { return inner_->FencePending(f); }
    void WaitFence(uint32_t f) override { inner_->WaitFence(f); }
    bool Busy(uint32_t r) override { return inner_->Busy(r); }
    void WaitResource(uint32_t r) override { inner_->WaitResource(r); }

private:
    std::unique_ptr<Backend> inner_;
};
}  // namespace

std::unique_ptr<Backend> CreateBackend() {
    const char* choice = std::getenv("SHAOLIN_RENDERER");
    if (shaolin::options.background || (choice && !std::strcmp(choice, "null"))) return CreateNullBackend();
    if (auto vulkan = CreateVulkanBackend()) return std::make_unique<Fallback>(std::move(vulkan));
    LOGE("gpu: built without Vulkan; running without a picture");
    return CreateNullBackend();
}
}  // namespace gpu

#include "linux/linux_launcher_window.h"

#include "linux/vk/vk_api.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include "core/log.h"

using vkapi::vk;

namespace linux_launcher {
namespace {

constexpr uint32_t kFrames = 2;

ImGuiKey Key(SDL_Keycode key) {
    switch (key) {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
        case SDLK_RCTRL: return ImGuiKey_RightCtrl;
        case SDLK_LSHIFT: return ImGuiKey_LeftShift;
        case SDLK_RSHIFT: return ImGuiKey_RightShift;
        case SDLK_LALT: return ImGuiKey_LeftAlt;
        case SDLK_RALT: return ImGuiKey_RightAlt;
        case SDLK_A: return ImGuiKey_A;
        case SDLK_C: return ImGuiKey_C;
        case SDLK_V: return ImGuiKey_V;
        case SDLK_X: return ImGuiKey_X;
        case SDLK_Y: return ImGuiKey_Y;
        case SDLK_Z: return ImGuiKey_Z;
        default: return ImGuiKey_None;
    }
}

int MouseButton(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        default: return -1;
    }
}

std::string g_clipboard;
const char* GetClipboard(void*) {
    char* text = SDL_GetClipboardText();
    g_clipboard = text ? text : "";
    SDL_free(text);
    return g_clipboard.c_str();
}
void SetClipboard(void*, const char* text) { SDL_SetClipboardText(text); }

// a proportional system font with the accented letters paths and names use; ImGui's own font otherwise
void LoadFont(float pixels) {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    for (const char* path : candidates) {
        if (SDL_GetPathInfo(path, nullptr) && io.Fonts->AddFontFromFileTTF(path, pixels)) return;
    }
    ImFontConfig config;
    config.SizePixels = pixels;
    io.Fonts->AddFontDefault(&config);
}

void Style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0;
    s.FrameRounding = 4;
    s.TabRounding = 4;
    s.GrabRounding = 4;
    s.PopupRounding = 4;
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowPadding = ImVec2(18, 16);
    ImVec4* c = s.Colors;
    const ImVec4 accent(0.72f, 0.18f, 0.12f, 1.0f);  // the series' red
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.19f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.22f, 0.24f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.25f, 0.26f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.16f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.55f, 0.16f, 0.11f, 1.0f);
    c[ImGuiCol_TabSelected] = accent;
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.16f, 0.11f, 1.0f);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = ImVec4(0.30f, 0.14f, 0.11f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.16f, 0.11f, 1.0f);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.55f, 0.30f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.35f, 0.22f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.55f, 0.30f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.27f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.57f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.12f, 0.09f, 1.0f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
    s.WindowBorderSize = 0;
    s.PopupBorderSize = 1;
    s.ScaleAllSizes(scale);
}

PFN_vkVoidFunction UiLoader(const char* name, void* user) {
    auto instance = static_cast<VkInstance>(user);
    PFN_vkVoidFunction f = vk.vkGetInstanceProcAddr(instance, name);
    if (!f) {
        std::string core = name;
        if (core.size() > 3 && core.compare(core.size() - 3, 3, "KHR") == 0) {
            core.resize(core.size() - 3);
            f = vk.vkGetInstanceProcAddr(instance, core.c_str());
        }
    }
    return f;
}

}  // namespace

struct Window::State {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> done;  // per swap chain image
    VkCommandPool pools[kFrames] = {};
    VkCommandBuffer cmds[kFrames] = {};
    VkFence fences[kFrames] = {};
    VkSemaphore acquired[kFrames] = {};
    uint32_t frame = 0;
    VkDescriptorPool ui_pool = VK_NULL_HANDLE;
    bool imgui = false;
    bool imgui_vulkan = false;
    bool dirty = false;
    bool text_input = false;
    uint64_t last_ticks = 0;

    bool Instance();
    bool Device();
    bool Swapchain();
    void DestroySwapchain();
    void Render();
};

bool Window::State::Instance() {
    if (!vkapi::LoadGlobal()) return false;
    Uint32 count = 0;
    const char* const* ext = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!ext) return false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Shaolin Monks Recomp launcher";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = count;
    ci.ppEnabledExtensionNames = ext;
    if (vk.vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) return false;
    if (!vkapi::LoadInstance(instance)) return false;
    return SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface);
}

bool Window::State::Device() {
    uint32_t n = 0;
    vk.vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> list(n);
    vk.vkEnumeratePhysicalDevices(instance, &n, list.data());
    bool dynamic_ext = false;
    int best = -1;
    for (VkPhysicalDevice d : list) {
        VkPhysicalDeviceProperties p;
        vk.vkGetPhysicalDeviceProperties(d, &p);
        uint32_t en = 0;
        vk.vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> ext(en);
        vk.vkEnumerateDeviceExtensionProperties(d, nullptr, &en, ext.data());
        auto has = [&](const char* name) {
            return std::any_of(ext.begin(), ext.end(), [&](const VkExtensionProperties& e) { return !std::strcmp(e.extensionName, name); });
        };
        const bool khr_dynamic = has(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        if (p.apiVersion < VK_API_VERSION_1_2 || !has(VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
            (p.apiVersion < VK_API_VERSION_1_3 && !khr_dynamic)) {
            continue;
        }
        uint32_t qn = 0;
        vk.vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vk.vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qf.data());
        for (uint32_t i = 0; i < qn; i++) {
            VkBool32 present = VK_FALSE;
            vk.vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
            if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 4
                        : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                        : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 2
                                                                                 : 1;
            if (score > best) {
                best = score;
                gpu = d;
                family = i;
                dynamic_ext = khr_dynamic;
            }
            break;
        }
    }
    if (!gpu) return false;
    VkPhysicalDeviceDynamicRenderingFeatures dyn{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dyn.dynamicRendering = VK_TRUE;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    std::vector<const char*> ext = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (dynamic_ext) ext.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &dyn;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = uint32_t(ext.size());
    ci.ppEnabledExtensionNames = ext.data();
    if (vk.vkCreateDevice(gpu, &ci, nullptr, &device) != VK_SUCCESS) return false;
    if (!vkapi::LoadDevice(device)) return false;
    vk.vkGetDeviceQueue(device, family, 0, &queue);
    for (uint32_t i = 0; i < kFrames; i++) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family;
        if (vk.vkCreateCommandPool(device, &pi, nullptr, &pools[i]) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pools[i];
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vk.vkAllocateCommandBuffers(device, &ai, &cmds[i]) != VK_SUCCESS) return false;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vk.vkCreateFence(device, &fi, nullptr, &fences[i]) != VK_SUCCESS) return false;
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vk.vkCreateSemaphore(device, &si, nullptr, &acquired[i]) != VK_SUCCESS) return false;
    }
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 8;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    return vk.vkCreateDescriptorPool(device, &pi, nullptr, &ui_pool) == VK_SUCCESS;
}

void Window::State::DestroySwapchain() {
    for (VkImageView v : views) vk.vkDestroyImageView(device, v, nullptr);
    for (VkSemaphore s : done) vk.vkDestroySemaphore(device, s, nullptr);
    views.clear();
    done.clear();
    images.clear();
}

bool Window::State::Swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    if (vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps) != VK_SUCCESS) return false;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    VkExtent2D e = caps.currentExtent;
    if (e.width == 0xFFFFFFFFu) {
        e.width = std::clamp<uint32_t>(uint32_t(std::max(pw, 1)), caps.minImageExtent.width, caps.maxImageExtent.width);
        e.height = std::clamp<uint32_t>(uint32_t(std::max(ph, 1)), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!e.width || !e.height) return false;
    uint32_t n = 0;
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    if (imgui_vulkan && chosen.format != format) return false;  // the overlay's pipeline was made for the first format
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        for (uint32_t bit = 1; bit; bit <<= 1) {
            if (caps.supportedCompositeAlpha & bit) {
                alpha = VkCompositeAlphaFlagBitsKHR(bit);
                break;
            }
        }
    }
    VkSwapchainKHR old = swap;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface;
    ci.minImageCount = caps.maxImageCount ? std::min(caps.minImageCount + 1, caps.maxImageCount) : caps.minImageCount + 1;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = e;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = alpha;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VkResult r = vk.vkCreateSwapchainKHR(device, &ci, nullptr, &swap);
    DestroySwapchain();
    if (old) vk.vkDestroySwapchainKHR(device, old, nullptr);
    if (r != VK_SUCCESS) {
        swap = VK_NULL_HANDLE;
        return false;
    }
    format = chosen.format;
    extent = e;
    vk.vkGetSwapchainImagesKHR(device, swap, &n, nullptr);
    images.resize(n);
    vk.vkGetSwapchainImagesKHR(device, swap, &n, images.data());
    for (VkImage image : images) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vk.vkCreateImageView(device, &vi, nullptr, &view);
        views.push_back(view);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s = VK_NULL_HANDLE;
        vk.vkCreateSemaphore(device, &si, nullptr, &s);
        done.push_back(s);
    }
    dirty = false;
    return true;
}

void Window::State::Render() {
    if (dirty || !swap) {
        vk.vkDeviceWaitIdle(device);
        if (!Swapchain()) return;
    }
    vk.vkWaitForFences(device, 1, &fences[frame], VK_TRUE, UINT64_MAX);
    uint32_t index = 0;
    VkResult r = vk.vkAcquireNextImageKHR(device, swap, UINT64_MAX, acquired[frame], VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        dirty = true;
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return;
    if (r == VK_SUBOPTIMAL_KHR) dirty = true;
    vk.vkResetFences(device, 1, &fences[frame]);
    vk.vkResetCommandPool(device, pools[frame], 0);
    VkCommandBuffer cmd = cmds[frame];
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = images[index];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &b);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = views[index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.09f, 0.09f, 0.10f, 1.0f}};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    vk.vkCmdBeginRendering(cmd, &ri);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vk.vkCmdEndRendering(cmd);
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = 0;
    vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                            nullptr, 0, nullptr, 1, &b);
    vk.vkEndCommandBuffer(cmd);
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired[frame];
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &done[index];
    vk.vkQueueSubmit(queue, 1, &si, fences[frame]);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap;
    pi.pImageIndices = &index;
    r = vk.vkQueuePresentKHR(queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) dirty = true;
    frame = (frame + 1) % kFrames;
}

Window::Window() : s_(new State) {}
Window::~Window() {
    Close();
    delete s_;
}

SDL_Window* Window::Handle() const { return s_->window; }

bool Window::Open(const char* title, int width, int height) {
    State& s = *s_;
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        LOGI("launcher: no display (%s)", SDL_GetError());
        return false;
    }
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    s.window = SDL_CreateWindow(title, int(width * scale), int(height * scale),
                                SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!s.window) {
        LOGI("launcher: no window (%s)", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(s.window, int(560 * scale), int(460 * scale));
    if (!s.Instance() || !s.Device() || !s.Swapchain()) {
        LOGI("launcher: Vulkan unavailable for the settings window");
        Close();
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s.imgui = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "shaolin_sdl3";
    io.SetClipboardTextFn = SetClipboard;
    io.GetClipboardTextFn = GetClipboard;
    LoadFont(17.0f * scale);
    Style(scale);
    if (!ImGui_ImplVulkan_LoadFunctions(UiLoader, s.instance)) {
        Close();
        return false;
    }
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = s.instance;
    ii.PhysicalDevice = s.gpu;
    ii.Device = s.device;
    ii.QueueFamily = s.family;
    ii.Queue = s.queue;
    ii.DescriptorPool = s.ui_pool;
    ii.MinImageCount = 2;
    ii.ImageCount = std::max<uint32_t>(uint32_t(s.images.size()), 2);
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &s.format;
    if (!ImGui_ImplVulkan_Init(&ii)) {
        Close();
        return false;
    }
    s.imgui_vulkan = true;
    ImGui_ImplVulkan_CreateFontsTexture();
    s.last_ticks = SDL_GetTicksNS();
    SDL_ShowWindow(s.window);
    return true;
}

bool Window::BeginFrame() {
    State& s = *s_;
    ImGuiIO& io = ImGui::GetIO();
    bool open = true;
    SDL_Event e;
    // idle while nothing happens: the launcher wakes on input. A press that follows a move waits for the next
    // frame: ImGui's tabs take a click only over an item they already saw hovered on the frame before.
    if (SDL_WaitEventTimeout(nullptr, 250)) {
        bool moved = false;
        while (SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1) {
            if (moved && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) break;
            SDL_PollEvent(&e);
            switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: open = false; break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: s.dirty = true; break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED: io.AddFocusEvent(true); break;
                case SDL_EVENT_WINDOW_FOCUS_LOST: io.AddFocusEvent(false); break;
                case SDL_EVENT_MOUSE_MOTION:
                    io.AddMousePosEvent(e.motion.x, e.motion.y);
                    moved = true;
                    break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE: io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    io.AddMousePosEvent(e.button.x, e.button.y);
                    if (int b = MouseButton(e.button.button); b >= 0) io.AddMouseButtonEvent(b, e.button.down);
                    break;
                case SDL_EVENT_MOUSE_WHEEL: io.AddMouseWheelEvent(e.wheel.x, e.wheel.y); break;
                case SDL_EVENT_TEXT_INPUT: io.AddInputCharactersUTF8(e.text.text); break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    io.AddKeyEvent(ImGuiMod_Ctrl, (e.key.mod & SDL_KMOD_CTRL) != 0);
                    io.AddKeyEvent(ImGuiMod_Shift, (e.key.mod & SDL_KMOD_SHIFT) != 0);
                    io.AddKeyEvent(ImGuiMod_Alt, (e.key.mod & SDL_KMOD_ALT) != 0);
                    if (ImGuiKey k = Key(e.key.key); k != ImGuiKey_None) io.AddKeyEvent(k, e.key.down);
                    break;
                }
                default: break;
            }
        }
    }
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(s.window, &w, &h);
    SDL_GetWindowSizeInPixels(s.window, &pw, &ph);
    io.DisplaySize = ImVec2(float(w), float(h));
    if (w > 0 && h > 0) io.DisplayFramebufferScale = ImVec2(float(pw) / w, float(ph) / h);
    const uint64_t now = SDL_GetTicksNS();
    io.DeltaTime = std::max(1e-4f, float(double(now - s.last_ticks) / 1e9));
    s.last_ticks = now;
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (io.WantTextInput != s.text_input) {
        s.text_input = io.WantTextInput;
        if (s.text_input) SDL_StartTextInput(s.window);
        else SDL_StopTextInput(s.window);
    }
    return open;
}

void Window::EndFrame() {
    ImGui::Render();
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(s_->window, &pw, &ph);
    if (pw > 0 && ph > 0) s_->Render();
}

void Window::Close() {
    State& s = *s_;
    if (s.device) vk.vkDeviceWaitIdle(s.device);
    if (s.imgui_vulkan) ImGui_ImplVulkan_Shutdown();
    s.imgui_vulkan = false;
    if (s.imgui) ImGui::DestroyContext();
    s.imgui = false;
    if (s.device) {
        s.DestroySwapchain();
        if (s.swap) vk.vkDestroySwapchainKHR(s.device, s.swap, nullptr);
        for (uint32_t i = 0; i < kFrames; i++) {
            if (s.fences[i]) vk.vkDestroyFence(s.device, s.fences[i], nullptr);
            if (s.acquired[i]) vk.vkDestroySemaphore(s.device, s.acquired[i], nullptr);
            if (s.pools[i]) vk.vkDestroyCommandPool(s.device, s.pools[i], nullptr);
            s.fences[i] = VK_NULL_HANDLE;
            s.acquired[i] = VK_NULL_HANDLE;
            s.pools[i] = VK_NULL_HANDLE;
        }
        if (s.ui_pool) vk.vkDestroyDescriptorPool(s.device, s.ui_pool, nullptr);
        vk.vkDestroyDevice(s.device, nullptr);
    }
    s.swap = VK_NULL_HANDLE;
    s.ui_pool = VK_NULL_HANDLE;
    s.device = VK_NULL_HANDLE;
    if (s.surface) vk.vkDestroySurfaceKHR(s.instance, s.surface, nullptr);
    if (s.instance) vk.vkDestroyInstance(s.instance, nullptr);
    s.surface = VK_NULL_HANDLE;
    s.instance = VK_NULL_HANDLE;
    s.gpu = VK_NULL_HANDLE;
    if (s.window) {
        if (s.text_input) SDL_StopTextInput(s.window);
        SDL_DestroyWindow(s.window);
    }
    s.window = nullptr;
    s.text_input = false;
}

}  // namespace linux_launcher

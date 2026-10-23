#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "platform_compat.h"
#include <SDL3/SDL.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include "controller.h"
#include "host.h"
#include "keyboard.h"
#include "mouse.h"
#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace {
struct Slot {
    SDL_Gamepad* device=nullptr;
    input::Pad state{};
    bool rumble=false, motor_warning=false;
    uint16_t desired[2]{}, sent[2]{};
    uint64_t refreshed=0;
    uint64_t activity_logged=0;
};
std::mutex lock;
std::array<Slot,4> slots;
bool initialized=false, focused=false;
unsigned long owner_thread=0;
#ifdef _WIN32
HWND game_window=nullptr;
#else
void* game_window=nullptr;  // SDL_Window; the Linux host calls Update from its event loop
#endif
#ifdef _WIN32
constexpr UINT_PTR timer=0x534D4354;
#endif
uint64_t last_device_scan=0;
float deadzone=.15f;
bool rumble_enabled=true;
bool multiplayer=false;
int auto_reserved=-1, auto_keyboard=0;
std::array<SDL_Joystick*,4> fixtures{};
bool Enabled() { return !shaolin::options.background || shaolin::options.virtual_gamepads; }
int PhysicalPort(int port) {
    if (port<0 || port>=4 || port==auto_reserved) return -1;
    return port-(auto_reserved>=0 && port>auto_reserved);
}
int LogicalPort(int index) { return index+(auto_reserved>=0 && index>=auto_reserved); }
bool StandbyPort(int port) {
    return port==1 && !multiplayer && slots[0].device;
}
constexpr const char* binding_keys[]={"a","b","x","y","black","white","start","back","left_stick","right_stick"};
std::array<SDL_GamepadButton,10> bindings={SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_LEFT_STICK,SDL_GAMEPAD_BUTTON_RIGHT_STICK};
void Sticks(int16_t raw_x,int16_t raw_y,int16_t& x,int16_t& y) {
    float fx=raw_x/32768.0f, fy=-raw_y/32768.0f, length=std::hypot(fx,fy);
    if (length<=deadzone) { x=y=0; return; }
    float gain=(std::min(length,1.0f)-deadzone)/((1-deadzone)*length);
    auto encode=[](float v) { return int16_t(std::clamp(std::lround(v*32768),-32768l,32767l)); };
    x=encode(fx*gain); y=encode(fy*gain);
}
void Add(SDL_JoystickID id) {
    for (auto& s:slots) if (s.device && SDL_GetGamepadID(s.device)==id) return;
    auto it=std::find_if(slots.begin(),slots.end(),[](const Slot& s){return !s.device;});
    if (it==slots.end()) return;
    auto* pad=SDL_OpenGamepad(id);
    if (!pad) { LOGW("input: cannot open controller: %s",SDL_GetError()); return; }
    it->device=pad;
    it->rumble=SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad),SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,false);
    LOGI("input: port=%d name=%s type=%s connection=%d vendor=%04X product=%04X rumble=%d",
         LogicalPort(int(it-slots.begin())),SDL_GetGamepadName(pad),SDL_GetGamepadStringForType(SDL_GetGamepadType(pad)),
         int(SDL_GetGamepadConnectionState(pad)),SDL_GetGamepadVendor(pad),SDL_GetGamepadProduct(pad),it->rumble);
}
void Remove(SDL_JoystickID id) {
    for (auto& s:slots) if (s.device && SDL_GetGamepadID(s.device)==id) {
        SDL_CloseGamepad(s.device); s={};
        LOGI("input: port=%d disconnected",LogicalPort(int(&s-slots.data())));
    }
}
void AttachFixture(unsigned index) {
    if (fixtures[index]) return;
    SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.name="Shaolin isolated diagnostic pad";
    const auto id=SDL_AttachVirtualJoystick(&desc);
    fixtures[index]=id?SDL_OpenJoystick(id):nullptr;
    if (!fixtures[index]) {if(id)SDL_DetachVirtualJoystick(id);LOGE("input: cannot create diagnostic gamepad");return;}
    SDL_SetJoystickVirtualAxis(fixtures[index],SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(fixtures[index],SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
}
void DetachFixture(unsigned index) {
    if (!fixtures[index]) return;
    const auto id=SDL_GetJoystickID(fixtures[index]);
    SDL_CloseJoystick(fixtures[index]);SDL_DetachVirtualJoystick(id);fixtures[index]=nullptr;
}
}
namespace shaolin::controller {
void SetMultiplayer(bool enabled) {
    std::lock_guard guard(lock);
    if (multiplayer==enabled) return;
    multiplayer=enabled;auto_reserved=-1;auto_keyboard=0;
    if (enabled) {
        const auto count=std::count_if(slots.begin(),slots.end(),[](const Slot& s){return s.device!=nullptr;});
        if (count<2) {
            // Native PC rule: with keyboard + one gamepad, the keyboard owns
            // player one regardless of which input navigated the main menu.
            auto_reserved=0;
        } else {
            // Two physical gamepads own P1/P2; keyboard stays available in
            // solo, but cannot unexpectedly control one of the co-op players.
            auto_keyboard=-1;
        }
    }
    // Modes change on the guest thread. The window thread stops old motor
    // requests on its next SDL update, without issuing SDL calls here.
    for (auto& s:slots) s.desired[0]=s.desired[1]=0;
    LOGI("input: multiplayer=%d keyboard=%d reserved_port=%d",enabled,auto_keyboard+1,auto_reserved);
}
int KeyboardPort() {
    std::lock_guard guard(lock);
    return auto_keyboard;
}
void Load(const void* raw) {
    const auto c=(*static_cast<const toml::table*>(raw))["controller"];
    const double value=c["deadzone"].value_or(.15);
    deadzone=std::isfinite(value)?float(std::clamp(value,0.0,.5)):.15f;
    rumble_enabled=c["rumble"].value_or(true);
    multiplayer=false;auto_reserved=-1;auto_keyboard=0;
    for (size_t i=0;i<bindings.size();++i) if (auto name=c["bindings"][binding_keys[i]].value<std::string>()) {
        auto button=SDL_GetGamepadButtonFromString(name->c_str());
        if (*name=="south" || *name=="cross") button=SDL_GAMEPAD_BUTTON_SOUTH;
        if (*name=="east" || *name=="circle") button=SDL_GAMEPAD_BUTTON_EAST;
        if (*name=="west" || *name=="square") button=SDL_GAMEPAD_BUTTON_WEST;
        if (*name=="north" || *name=="triangle") button=SDL_GAMEPAD_BUTTON_NORTH;
        if (button!=SDL_GAMEPAD_BUTTON_INVALID) bindings[i]=button;
        else LOGW("input: ignored unknown binding %s=%s",binding_keys[i],name->c_str());
    }
}
void Save(void* raw) {
    auto& table=*static_cast<toml::table*>(raw);
    toml::table mapped;
    for (size_t i=0;i<bindings.size();++i) mapped.insert(binding_keys[i],SDL_GetGamepadStringForButton(bindings[i]));
    table.insert_or_assign("controller",toml::table{{"deadzone",double(deadzone)},{"rumble",rumble_enabled},
        {"bindings",std::move(mapped)}});
}
void SetFocused(bool value) {
    std::lock_guard guard(lock);
    if (focused!=value) LOGI("input: game window focus=%d",value);
    focused=value;
    if (!focused) for (auto& s:slots) {
        bool connected=s.state.connected; s.state={}; s.state.connected=connected;
        s.desired[0]=s.desired[1]=0;
    }
}
bool HasRumble(int port) {
    std::lock_guard guard(lock);
    const int index=PhysicalPort(port);
    if (index>=0 && slots[index].device) return slots[index].rumble && rumble_enabled;
    // The game caches these capabilities before confirming Ko-op and binding
    // the available real controller to its second player.
    return StandbyPort(port) && app::kGame.pad && slots[0].rumble && rumble_enabled;
}
void Update() {
    if (!Enabled() || !initialized || GetCurrentThreadId()!=owner_thread) return;
    SDL_PumpEvents();
    std::lock_guard guard(lock);
    SDL_Event event;
    while (SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_EVENT_GAMEPAD_ADDED,SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED)==1) {
        if (event.type==SDL_EVENT_GAMEPAD_ADDED) Add(event.gdevice.which);
        else if (event.type==SDL_EVENT_GAMEPAD_REMOVED) Remove(event.gdevice.which);
    }
    const uint64_t now=SDL_GetTicks();
    // Bluetooth reconnects and some Windows drivers can fail to leave an
    // actionable gamepad event. Reconcile the actual SDL device list too.
    if (now-last_device_scan>=750) {
        int count=0;
        if (SDL_JoystickID* ids=SDL_GetGamepads(&count)) {
            for (int i=0;i<count;++i) Add(ids[i]);
            SDL_free(ids);
            for (auto& s:slots) if (s.device && !SDL_GamepadConnected(s.device)) {
                const auto index=int(&s-slots.data());
                SDL_CloseGamepad(s.device); s={};
                LOGI("input: port=%d disconnected (device scan)",LogicalPort(index));
            }
        }
        last_device_scan=now;
    }
    for (auto& s:slots) {
        const auto previous=s.state;s.state={}; auto* pad=s.device;
        if (!pad || !SDL_GamepadConnected(pad)) continue;
        auto& p=s.state; p.connected=true;
        if (focused) {
            constexpr SDL_GamepadButton dpad[]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
            for (int i=0;i<4;++i) if (SDL_GetGamepadButton(pad,dpad[i])) p.buttons|=uint16_t(1u<<i);
            for (int i=0;i<6;++i) p.analog[i]=SDL_GetGamepadButton(pad,bindings[i])?255:0;
            for (int i=0;i<4;++i) if (SDL_GetGamepadButton(pad,bindings[6+i])) p.buttons|=uint16_t(1u<<(4+i));
            auto axis=[&](SDL_GamepadAxis a){return SDL_GetGamepadAxis(pad,a);};
            p.analog[6]=uint8_t(std::max(int(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)),0)*255/32767);
            p.analog[7]=uint8_t(std::max(int(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)),0)*255/32767);
            Sticks(axis(SDL_GAMEPAD_AXIS_LEFTX),axis(SDL_GAMEPAD_AXIS_LEFTY),p.lx,p.ly);
            Sticks(axis(SDL_GAMEPAD_AXIS_RIGHTX),axis(SDL_GAMEPAD_AXIS_RIGHTY),p.rx,p.ry);
            bool changed=(p.buttons&~previous.buttons)!=0;
            for (int i=0;i<8;++i) changed|=p.analog[i]>previous.analog[i];
            changed|=std::abs(int(p.lx)-previous.lx)>4000 || std::abs(int(p.ly)-previous.ly)>4000 ||
                     std::abs(int(p.rx)-previous.rx)>4000 || std::abs(int(p.ry)-previous.ry)>4000;
            if (changed) {
                if (now-s.activity_logged>=200) {
                    LOGI("input: gamepad port=%d buttons=%04X A=%u B=%u left=%d,%d",
                        LogicalPort(int(&s-slots.data())),p.buttons,p.analog[0],p.analog[1],p.lx,p.ly);
                    s.activity_logged=now;
                }
            }
        }
        const uint16_t low=focused && rumble_enabled?s.desired[0]:0, high=focused && rumble_enabled?s.desired[1]:0;
        if (s.rumble && (low!=s.sent[0] || high!=s.sent[1] || ((low||high) && now-s.refreshed>=100))) {
            if (!SDL_RumbleGamepad(pad,low,high,(low||high)?300:0) && !s.motor_warning) {
                LOGW("input: rumble unavailable: %s",SDL_GetError()); s.motor_warning=true;
            }
            s.sent[0]=low; s.sent[1]=high; s.refreshed=now;
        }
    }
}
void Shutdown() {
    if (!initialized || GetCurrentThreadId()!=owner_thread) return;
#ifdef _WIN32
    if (game_window) KillTimer(game_window,timer);
#endif
    std::lock_guard guard(lock);
    for (auto& s:slots) if (s.device) {
        if (s.sent[0]||s.sent[1]) SDL_RumbleGamepad(s.device,0,0,0);
        SDL_CloseGamepad(s.device); s={};
    }
    for (unsigned i=0;i<fixtures.size();++i) DetachFixture(i);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD); initialized=false; game_window=nullptr;
    multiplayer=false;auto_reserved=-1;auto_keyboard=0;last_device_scan=0;
}
void Window(void* hwnd) {
    mouse::Window(hwnd);
    if (!Enabled() || !initialized) return;
#ifndef _WIN32
    game_window=hwnd;
    SetFocused(true);
#else
    game_window=static_cast<HWND>(hwnd);
    SetFocused(options.background || GetForegroundWindow()==game_window);
    if (!SetTimer(game_window,timer,8,nullptr)) LOGE("input: controller timer failed");
#endif
    Update();
}
bool Message(void* hwnd,uint32_t message,uintptr_t wp,intptr_t lp,intptr_t* result) {
#ifndef _WIN32
    (void)hwnd;(void)message;(void)wp;(void)lp;(void)result;
    return false;  // Linux delivers SDL events through linux_input.cpp
#else
    if (mouse::Message(hwnd, message, wp, lp, result)) return true;
    // Fixture messages are accepted only by our own hidden diagnostic window.
    // Physical backends are disabled before SDL initialization in this mode.
    if (options.background && options.virtual_gamepads && hwnd==game_window &&
        (message==WM_APP+0x51 || message==WM_APP+0x52 || message==WM_APP+0x53)) {
        unsigned index=HIWORD(wp), control=LOWORD(wp);
        if (message==WM_APP+0x53 && index<options.virtual_gamepads) {
            if (lp) AttachFixture(index);else DetachFixture(index);
            Update();*result=0;return true;
        }
        if (index<fixtures.size() && fixtures[index]) {
            if (message==WM_APP+0x51 && control<SDL_GAMEPAD_BUTTON_COUNT)
                SDL_SetJoystickVirtualButton(fixtures[index],int(control),lp!=0);
            if (message==WM_APP+0x52 && control<SDL_GAMEPAD_AXIS_COUNT && lp>=-32768 && lp<=32767)
                SDL_SetJoystickVirtualAxis(fixtures[index],int(control),Sint16(lp));
            Update();
        }
        *result=0; return true;
    }
    if (Enabled()) {
        if (message==WM_TIMER && wp==timer) {
            if (!options.background && game_window && IsWindowVisible(game_window)) {
                const HWND active=GetForegroundWindow();
                SetFocused(active==game_window || (active && GetAncestor(active,GA_ROOTOWNER)==game_window));
            }
            Update(); *result=0; return true;
        }
        if (message==WM_SETFOCUS || (message==WM_ACTIVATE && LOWORD(wp)!=WA_INACTIVE)) SetFocused(true);
        if (message==WM_KILLFOCUS || (message==WM_ACTIVATE && LOWORD(wp)==WA_INACTIVE)) SetFocused(false);
        if (message==WM_ACTIVATEAPP) SetFocused(wp!=0);
        if (message==WM_DESTROY) Shutdown();
    }
    return keyboard::Message(hwnd,message,wp,lp,result);
#endif
}
}
namespace input {
void Init() {
    if (!Enabled() || initialized) return;
    if (shaolin::options.background) {
        for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_WGI,SDL_HINT_XINPUT_ENABLED,
                SDL_HINT_JOYSTICK_DIRECTINPUT,SDL_HINT_JOYSTICK_RAWINPUT,SDL_HINT_JOYSTICK_GAMEINPUT,
                SDL_HINT_JOYSTICK_HIDAPI_PS4,SDL_HINT_JOYSTICK_HIDAPI_PS5})
            SDL_SetHintWithPriority(hint,"0",SDL_HINT_OVERRIDE);
    }
    // SDL chooses compatible backends and suppresses duplicate device representations.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS,"auto");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) { LOGE("input: SDL initialization failed: %s",SDL_GetError()); return; }
    owner_thread=GetCurrentThreadId(); initialized=true;
    for (unsigned i=0;i<shaolin::options.virtual_gamepads;++i) AttachFixture(i);
    const auto db=(shaolin::options.data_dir.empty()?app::ExeDir():shaolin::options.data_dir)/"gamecontrollerdb.txt";
    if (std::filesystem::is_regular_file(db)) LOGI("input: local mappings added=%d",SDL_AddGamepadMappingsFromFile(db.string().c_str()));
    int count=0;
    if (auto* ids=SDL_GetGamepads(&count)) {
        std::lock_guard guard(lock);
        for (int i=0;i<count;++i) Add(ids[i]);
        SDL_free(ids);
    }
    LOGI("input: SDL %d, %d gamepads; PS4 Bluetooth/USB via HIDAPI",SDL_GetVersion(),count);
}
Pad Poll(int port) {
    Pad p{};bool standby=false;
    if (Enabled()) {
        std::lock_guard guard(lock);const int index=PhysicalPort(port);if (index>=0) p=slots[index].state;
        standby=StandbyPort(port);
    }
    if (app::kGame.pad) app::kGame.pad(port,p);
    if (standby && !p.connected && app::kGame.pad) {
        // The original menu must see two sources before accepting Ko-op.
        // Advertise availability without copying any P1 buttons into P2.
        Pad keyboard{};app::kGame.pad(0,keyboard);p.connected=keyboard.connected;
    }
    return p;
}
void Rumble(int port,uint16_t left,uint16_t right) {
    if (shaolin::options.background) return;
    if (app::kGame.rumble) app::kGame.rumble(port,left,right);
    std::lock_guard guard(lock);
    const int index=PhysicalPort(port);
    if (index>=0 && slots[index].device && focused) { slots[index].desired[0]=left; slots[index].desired[1]=right; }
}
}

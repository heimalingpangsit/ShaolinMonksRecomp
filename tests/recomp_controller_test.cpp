// Synthetic SDL devices only: never opens or vibrates the user's controller.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <toml++/toml.hpp>
#include "app/app.h"
#include "core/input.h"
#include "host.h"
#include "controller.h"
#include "keyboard.h"
static bool keyboard_active=false;
static void TestKeyboard(int port,input::Pad& pad) {if(keyboard_active) shaolin::keyboard::Poll(port,pad);}
namespace app { const GameInfo kGame{.pad=TestKeyboard}; std::filesystem::path ExeDir() { return std::filesystem::current_path(); } }
namespace shaolin { Options options; }
static void Check(bool value,const char* description) {
    if (!value) { std::fprintf(stderr,"FAIL: %s (%s)\n",description,SDL_GetError()); std::exit(1); }
}
// The real game only calls controller::Update from its Win32 timer.
// Never refresh SDL separately here: that would hide an input pump bug.
static void Tick() { shaolin::controller::Update(); }
static Uint16 low_motor=0, high_motor=0;
static bool SDLCALL Motor(void*,Uint16 low,Uint16 high) { low_motor=low; high_motor=high; return true; }
static LRESULT CALLBACK TestWindow(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    intptr_t result=0;
    if (shaolin::controller::Message(hwnd,msg,wp,lp,&result)) return result;
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static SDL_JoystickID Attach(const char* name) {
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes=SDL_GAMEPAD_AXIS_COUNT; d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    d.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    d.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    d.name=name; d.Rumble=Motor;
    return SDL_AttachVirtualJoystick(&d);
}
int main() {
    // Override normal backend hints before SDL_Init: hardware cannot be touched.
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_WGI,SDL_HINT_XINPUT_ENABLED,
                            SDL_HINT_JOYSTICK_DIRECTINPUT,SDL_HINT_JOYSTICK_RAWINPUT,SDL_HINT_JOYSTICK_GAMEINPUT,
                            SDL_HINT_JOYSTICK_HIDAPI_PS4,SDL_HINT_JOYSTICK_HIDAPI_PS5})
        SDL_SetHintWithPriority(hint,"0",SDL_HINT_OVERRIDE);
    input::Init();
    shaolin::controller::SetFocused(true);
    int initial=0; auto* initial_ids=SDL_GetGamepads(&initial); SDL_free(initial_ids);
    Check(initial==0,"virtual tests must not enumerate any physical gamepad");
    auto first=Attach("Shaolin synthetic pad"); Check(first!=0,"attach virtual gamepad");
    auto* joy=SDL_OpenJoystick(first); Check(joy!=nullptr,"open virtual joystick");
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTX,900);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTY,-700);
    Tick();
    auto p=input::Poll(0);
    Check(p.connected,"hot-plug connected");
    Check(p.lx==0 && p.ly==0,"stick drift inside default deadzone is neutral");
    Check(p.rx==0 && p.ry==0 && p.analog[6]==0 && p.analog[7]==0,"neutral right stick and triggers");
    SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_SOUTH,true);
    SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
    SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_START,true);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTX,32767);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTY,-32768);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,0);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_RIGHTX,-32768);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_RIGHTY,32767);
    Tick(); p=input::Poll(0);
    Check(p.analog[0]==255 && p.analog[4]==255 && p.analog[7]==255,"face, shoulder and trigger mapping");
    Check((p.buttons&0x10)!=0 && p.lx>20000 && p.ly>20000,"START and sticks delivered");
    Check(p.rx<-20000 && p.ry<-20000,"camera stick preserves sign and magnitude");
    Check(p.analog[6]>=126 && p.analog[6]<=128,"trigger retains intermediate pressure");
    input::Rumble(0,12345,23456); Tick();
    Check(low_motor==12345 && high_motor==23456,"rumble has a nonzero duration");
    for (int n=0;n<4;++n) {SDL_Delay(100);Tick();}
    Check(low_motor==12345 && high_motor==23456,"motors refreshed beyond their lease");
    shaolin::controller::SetFocused(false); p=input::Poll(0);
    Check(p.connected && !p.buttons && !p.lx && !p.ly && !p.analog[0],"focus loss releases input while keeping connection");
    Tick(); Check(!low_motor && !high_motor,"focus loss stops motors");
    shaolin::controller::SetFocused(true); Tick();
    Check(!low_motor && !high_motor,"focus return does not replay stale rumble");
    input::Pad threaded;
    std::thread reader([&]{threaded=input::Poll(0);shaolin::controller::Update();});reader.join();
    Check(threaded.analog[0]==255,"game thread reads a snapshot without pumping SDL");
    auto config=toml::parse("[controller]\ndeadzone=0.15\nrumble=false\n[controller.bindings]\na='east'\n");
    shaolin::controller::Load(&config); Tick();
    Check(!input::Poll(0).analog[0] && !shaolin::controller::HasRumble(0),"custom mapping and rumble toggle");
    SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_EAST,true);Tick();
    Check(input::Poll(0).analog[0]==255,"remapped face button reaches Xbox A");
    toml::table saved;shaolin::controller::Save(&saved);
    Check(saved["controller"]["bindings"]["a"].value_or(std::string{})=="b","mapping saved in SDL TOML format");
    Check(saved["controller"].as_table()->size()==3,"controller settings contain only deadzone, rumble, and bindings");
    auto second=Attach("Shaolin synthetic second pad"); Check(second!=0,"second gamepad");
    Tick(); Check(input::Poll(1).connected,"second player has a separate port");
    SDL_CloseJoystick(joy); Check(SDL_DetachVirtualJoystick(first),"detach first");
    Tick(); Check(!input::Poll(0).connected && input::Poll(1).connected,"disconnect preserves second port");
    auto third=Attach("Shaolin synthetic replacement"); Check(third!=0,"reconnect replacement");
    Tick(); Check(input::Poll(0).connected && input::Poll(1).connected,"reconnect fills free port");
    SDL_DetachVirtualJoystick(second); SDL_DetachVirtualJoystick(third); Tick();
    // Exercise the real HWND timer bridge: Poll must receive the new state
    // without the game thread or test calling Update directly.
    WNDCLASSW wc{};wc.lpfnWndProc=TestWindow;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ShaolinControllerTest";
    Check(RegisterClassW(&wc)!=0,"register owned hidden test window");
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,0,0,16,16,nullptr,nullptr,wc.hInstance,nullptr);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    Check(window && pid==GetCurrentProcessId() && !IsWindowVisible(window),"hidden timer window belongs to this test");
    shaolin::controller::Window(window);
    auto timed=Attach("Shaolin timer pad");auto* timed_joy=SDL_OpenJoystick(timed);
    Check(timed && timed_joy,"attach timer device");
    SDL_SetJoystickVirtualButton(timed_joy,SDL_GAMEPAD_BUTTON_START,true);
    SendMessageW(window,WM_SETFOCUS,0,0);
    for (int n=0;n<30;++n) {
        MSG msg;while(PeekMessageW(&msg,window,0,0,PM_REMOVE)) DispatchMessageW(&msg);
        SDL_Delay(2);
    }
    Check((input::Poll(0).buttons&0x10)!=0,"owned window timer refreshes input without guest SDL calls");
    SendMessageW(window,WM_KILLFOCUS,0,0);
    Check(!input::Poll(0).buttons,"window focus message releases the controller snapshot");
    SDL_CloseJoystick(timed_joy);SDL_DetachVirtualJoystick(timed);
    DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    shaolin::controller::Shutdown();
    keyboard_active=true;
    const toml::table automatic;
    shaolin::controller::Load(&automatic);
    input::Init();shaolin::controller::SetFocused(true);
    auto coop=Attach("Shaolin coop pad");auto* coop_joy=SDL_OpenJoystick(coop);
    Check(coop && coop_joy,"attach co-op gamepad");
    SDL_SetJoystickVirtualButton(coop_joy,SDL_GAMEPAD_BUTTON_EAST,true);
    Tick();
    shaolin::controller::SetMultiplayer(true);
    Check(input::Poll(0).connected && input::Poll(1).connected,"keyboard and first gamepad occupy separate player ports");
    intptr_t key_result=0;
    shaolin::keyboard::Message(nullptr,WM_KEYDOWN,'W',0,&key_result);
    Check(input::Poll(0).ly==32767 && !input::Poll(1).ly,"keyboard movement reaches only player one");
    Check(!input::Poll(0).analog[1] && input::Poll(1).analog[1]==255,"gamepad attack reaches only player two");
    shaolin::keyboard::Message(nullptr,WM_KILLFOCUS,0,0,&key_result);
    SDL_CloseJoystick(coop_joy);SDL_DetachVirtualJoystick(coop);Tick();
    Check(input::Poll(0).connected && !input::Poll(1).connected,"gamepad unplug does not disconnect keyboard player");
    coop=Attach("Shaolin coop replacement");Tick();
    Check(input::Poll(0).connected && input::Poll(1).connected,"gamepad reconnect returns to second player");
    SDL_DetachVirtualJoystick(coop);Tick();shaolin::controller::Shutdown();
    shaolin::controller::Load(&automatic);
    shaolin::options.background=true;shaolin::options.virtual_gamepads=1;
    input::Init();shaolin::controller::SetFocused(true);Tick();
    shaolin::controller::SetMultiplayer(true);
    Check(input::Poll(0).connected && input::Poll(1).connected,"isolated hidden fixture supplies second player");
    Check(!input::Poll(2).connected && !input::Poll(3).connected,"hidden fixture enumerates no physical controllers");
    Check(RegisterClassW(&wc)!=0,"register hidden fixture window");
    window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,0,0,16,16,nullptr,nullptr,wc.hInstance,nullptr);
    Check(window && !IsWindowVisible(window),"fixture window is hidden");
    shaolin::controller::Window(window);
    SendMessageW(window,WM_APP+0x51,SDL_GAMEPAD_BUTTON_START,1);
    SendMessageW(window,WM_APP+0x52,SDL_GAMEPAD_AXIS_LEFTX,32767);
    Check(!input::Poll(0).buttons && !input::Poll(0).lx && (input::Poll(1).buttons&0x10) && input::Poll(1).lx>32000,
          "hidden fixture messages pass through SDL to player two only");
    SendMessageW(window,WM_APP+0x51,SDL_GAMEPAD_BUTTON_START,0);
    SendMessageW(window,WM_APP+0x52,SDL_GAMEPAD_AXIS_LEFTX,0);
    Check(!input::Poll(1).buttons && !input::Poll(1).lx,"fixture releases its buttons and axes");
    SendMessageW(window,WM_APP+0x53,0,0);
    Check(input::Poll(0).connected && !input::Poll(1).connected,"hidden fixture can disconnect before the game enumerates devices");
    SendMessageW(window,WM_APP+0x53,0,1);
    Check(input::Poll(0).connected && input::Poll(1).connected,"hidden fixture can connect after the game is already running");
    DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    shaolin::controller::Shutdown();
    // Reproduce launching normally, navigating on keyboard, then connecting a
    // gamepad after selecting Ko-op. No dedicated launch flag is involved.
    shaolin::options.background=false;shaolin::options.virtual_gamepads=0;
    shaolin::controller::Load(&automatic);
    input::Init();shaolin::controller::SetFocused(true);
    Check(input::Poll(0).connected && !input::Poll(1).connected,"normal keyboard-only launch has one player");
    shaolin::keyboard::Message(nullptr,WM_KEYDOWN,'W',0,&key_result);
    shaolin::keyboard::Message(nullptr,WM_KEYUP,'W',0,&key_result);
    shaolin::controller::SetMultiplayer(true);
    auto late=Attach("Shaolin late co-op pad");auto* late_joy=SDL_OpenJoystick(late);
    Check(late && late_joy,"connect gamepad after choosing Ko-op");
    SDL_SetJoystickVirtualButton(late_joy,SDL_GAMEPAD_BUTTON_START,true);Tick();
    Check(input::Poll(0).connected && input::Poll(1).connected,"normal launch recognizes late gamepad as second player in Ko-op");
    Check(!input::Poll(0).buttons && (input::Poll(1).buttons&0x10),"late gamepad START reaches only player two");
    Check(!shaolin::controller::HasRumble(0) && shaolin::controller::HasRumble(1),"co-op motor capabilities follow the real controller port");
    SDL_CloseJoystick(late_joy);SDL_DetachVirtualJoystick(late);Tick();
    Check(input::Poll(0).connected && !input::Poll(1).connected,"late unplug preserves keyboard player");
    late=Attach("Shaolin late co-op reconnect");Tick();
    Check(input::Poll(0).connected && input::Poll(1).connected,"late reconnect restores second player");
    shaolin::controller::SetMultiplayer(false);
    Check(input::Poll(0).connected && input::Poll(1).connected && !input::Poll(1).buttons,
          "solo advertises keyboard plus controller to unlock Ko-op while keeping player two neutral");
    SDL_DetachVirtualJoystick(late);Tick();shaolin::controller::Shutdown();
    // The input used to navigate the menu must not change the fixed PC rule.
    // Even when START was pressed on the controller, keyboard is P1 in co-op.
    shaolin::controller::Load(&automatic);input::Init();shaolin::controller::SetFocused(true);
    late=Attach("Shaolin gamepad primary");late_joy=SDL_OpenJoystick(late);
    SDL_SetJoystickVirtualButton(late_joy,SDL_GAMEPAD_BUTTON_START,true);Tick();
    Check((input::Poll(0).buttons&0x10) && !input::Poll(1).buttons,
          "solo gamepad and keyboard share P1 before Ko-op selection");
    shaolin::controller::SetMultiplayer(true);
    shaolin::keyboard::Message(nullptr,WM_KEYDOWN,'W',0,&key_result);
    Check(!(input::Poll(0).buttons&0x10) && input::Poll(0).ly==32767 &&
          (input::Poll(1).buttons&0x10) && !input::Poll(1).ly,
          "Ko-op always uses keyboard P1 and single gamepad P2, regardless of menu navigation");
    shaolin::keyboard::Message(nullptr,WM_KILLFOCUS,0,0,&key_result);
    SDL_CloseJoystick(late_joy);SDL_DetachVirtualJoystick(late);Tick();shaolin::controller::Shutdown();
    // Two actual gamepads retain their original logical ports on disconnect.
    shaolin::controller::Load(&automatic);input::Init();shaolin::controller::SetFocused(true);
    first=Attach("Shaolin automatic first");second=Attach("Shaolin automatic second");Tick();
    shaolin::controller::SetMultiplayer(true);
    Check(input::Poll(0).connected && input::Poll(1).connected && !input::Poll(2).connected,
          "two gamepads use P1 and P2 in automatic co-op");
    shaolin::keyboard::Message(nullptr,WM_KEYDOWN,'W',0,&key_result);
    Check(!input::Poll(0).ly && !input::Poll(1).ly,
          "keyboard cannot unexpectedly control either player when two gamepads own Ko-op");
    shaolin::keyboard::Message(nullptr,WM_KILLFOCUS,0,0,&key_result);
    SDL_DetachVirtualJoystick(second);Tick();
    Check(input::Poll(0).connected && !input::Poll(1).connected,"second gamepad unplug never moves first gamepad to P2");
    second=Attach("Shaolin automatic second reconnect");Tick();
    Check(input::Poll(0).connected && input::Poll(1).connected,"second gamepad reconnect restores its original player");
    SDL_DetachVirtualJoystick(first);SDL_DetachVirtualJoystick(second);Tick();shaolin::controller::Shutdown();
    // A keyboard-only solo session must accept a newly connected gamepad on
    // P1 immediately, then give it P2 when the user enters co-op by keyboard.
    keyboard_active=true;
    shaolin::controller::Load(&automatic);input::Init();shaolin::controller::SetFocused(true);
    auto switched=Attach("Shaolin solo hotplug pad");auto* switched_joy=SDL_OpenJoystick(switched);
    Check(switched && switched_joy,"attach gamepad after keyboard solo startup");
    SDL_SetJoystickVirtualButton(switched_joy,SDL_GAMEPAD_BUTTON_EAST,true);Tick();
    Check(input::Poll(0).analog[0]==255 && !input::Poll(1).analog[0],
          "gamepad connected during keyboard solo immediately controls player one");
    shaolin::keyboard::Message(nullptr,WM_KEYDOWN,'W',0,&key_result);
    shaolin::keyboard::Message(nullptr,WM_KEYUP,'W',0,&key_result);
    shaolin::controller::SetMultiplayer(true);
    Check(!input::Poll(0).analog[0] && input::Poll(1).analog[0]==255,
          "keyboard-selected Ko-op moves the live controller to player two");
    SDL_CloseJoystick(switched_joy);SDL_DetachVirtualJoystick(switched);Tick();
    shaolin::controller::Shutdown();
    // Simulate a driver that changes its device list while its SDL add/remove
    // notifications are consumed by another component of the event loop.
    keyboard_active=false;
    shaolin::controller::Load(&automatic);input::Init();shaolin::controller::SetFocused(true);
    auto unnoticed=Attach("Shaolin missed hotplug event");
    SDL_PumpEvents();
    SDL_Event missed{};
    while(SDL_PeepEvents(&missed,1,SDL_GETEVENT,SDL_EVENT_GAMEPAD_ADDED,SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED)==1) {}
    SDL_Delay(800);Tick();
    Check(input::Poll(0).connected,"device scan detects a gamepad even if its added event was missed");
    SDL_DetachVirtualJoystick(unnoticed);
    SDL_PumpEvents();
    while(SDL_PeepEvents(&missed,1,SDL_GETEVENT,SDL_EVENT_GAMEPAD_ADDED,SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED)==1) {}
    SDL_Delay(800);Tick();
    Check(!input::Poll(0).connected,"device scan releases a gamepad even if its removed event was missed");
    shaolin::controller::Shutdown();
    std::puts("PASS: virtual controller mapping, deadzone, rumble and independent hot-plug ports");
}

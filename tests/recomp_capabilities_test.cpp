#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <cstdint>
struct X86Context { uint32_t esp=0x1100,eax=0,eip=0; };
std::array<uint8_t,4096> bytes;
namespace mem { bool IsCommitted(uint32_t p) {return p>=0x1000 && p<0x2000;} }
namespace kernel {
uint8_t* HostPtr(uint32_t p) {return bytes.data()+p-0x1000;}
uint32_t R32(uint32_t p) {uint32_t v;std::memcpy(&v,HostPtr(p),4);return v;}
void W32(uint32_t p,uint32_t v) {std::memcpy(HostPtr(p),&v,4);}
void Return(X86Context& c,uint32_t value,int count) {c.eax=value;c.esp+=4*(1+count);}
}
using namespace kernel;
namespace input { struct Pad {bool connected=false;}; Pad state{true}; Pad Poll(int){return state;} }
namespace shaolin::controller { bool motor=true; bool HasRumble(int){return motor;} }
std::mutex g_ports_lock;
struct Handle {uint32_t port;};
std::unordered_map<uint32_t,Handle> g_handles{{123,{0}}};
void __imp__sub_0048E398(X86Context& c,uint8_t*) {Return(c,1167,2);}
#define LOGI(...) ((void)0)
#define LOG_FIRST(...) ((void)0)
#define X86_FUNC(name) void name(X86Context& ctx,uint8_t* base)
#include "capabilities_test.inl"
static bool Call(uint32_t handle,uint32_t destination,uint32_t expected) {
    X86Context c; W32(c.esp,0x87654321);W32(c.esp+4,handle);W32(c.esp+8,destination);
    sub_0048E398(c,nullptr);
    return c.eax==expected && c.esp==0x110C;
}
int main() {
    bytes.fill(0xCD);
    if (!Call(123,0x1201,0)) {std::puts("FAIL: connected host handle must return capabilities, not 1167");return 1;}
    auto* out=HostPtr(0x1201);
    if (out[-1]!=0xCD || out[25]!=0xCD || out[0]!=1 || out[1] || out[2] || out[3]!=255 || out[4]) return 1;
    for(int i=5;i<13;++i) if(out[i]!=255) return 1;
    for(int i=13;i<21;i+=2) if(out[i]!=255 || out[i+1]!=127) return 1;
    for(int i=21;i<25;++i) if(out[i]!=255) return 1;
    shaolin::controller::motor=false;
    if(!Call(123,0x1201,0)) return 1;
    for(int i=21;i<25;++i) if(out[i]) return 1;
    bytes.fill(0xCD); auto snapshot=bytes;
    if(!Call(999,0x1201,6) || std::memcmp(HostPtr(0x1200),snapshot.data()+0x200,27)) return 1;
    input::state.connected=false;
    if(!Call(123,0x1201,1167) || std::memcmp(HostPtr(0x1200),snapshot.data()+0x200,27)) return 1;
    input::state.connected=true;
    if(!Call(123,0x1FF0,87)) return 1;
    std::puts("PASS: packed 25-byte capabilities, four axes, motors, canaries and rejected handles/buffers");
}

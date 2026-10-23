#include "vcdiff.h"
#include <array>
#include <limits>
#include <stdexcept>
namespace shaolin::mods {
namespace {
constexpr size_t limit=512*1024*1024;
[[noreturn]] void Bad() { throw std::runtime_error("Invalid or unsupported xdelta patch"); }
struct Reader {
    std::span<const uint8_t> bytes; size_t at=0;
    uint8_t Byte() { if(at>=bytes.size()) Bad(); return bytes[at++]; }
    size_t Int() {
        size_t n=0;
        for(unsigned i=0;i<10;++i) { auto b=Byte(); if(n>(std::numeric_limits<size_t>::max()>>7)) Bad(); n=(n<<7)|(b&127); if(!(b&128)) return n; } Bad();
    }
    std::span<const uint8_t> Take(size_t n) { if(n>bytes.size()-at) Bad(); auto b=bytes.subspan(at,n); at+=n; return b; }
};
enum Type { Run, Add, Copy, None };
struct Op { Type type=None; unsigned size=0,mode=0; };
struct Code { Op a,b; };
std::array<Code,256> Table() {
    std::array<Code,256> t{}; size_t i=0;
    t[i++]={{Run,0}}; t[i++]={{Add,0}};
    for(unsigned s=1;s<=17;++s) t[i++]={{Add,s}};
    for(unsigned m=0;m<9;++m) { t[i++]={{Copy,0,m}}; for(unsigned s=4;s<=18;++s) t[i++]={{Copy,s,m}}; }
    for(unsigned m=0;m<6;++m) for(unsigned a=1;a<=4;++a) for(unsigned c=4;c<=6;++c) t[i++]={{Add,a},{Copy,c,m}};
    for(unsigned m=6;m<9;++m) for(unsigned a=1;a<=4;++a) t[i++]={{Add,a},{Copy,4,m}};
    for(unsigned m=0;m<9;++m) t[i++]={{Copy,4,m},{Add,1}};
    return t;
}
uint32_t Adler(std::span<const uint8_t> data) {
    uint32_t a=1,b=0; for(auto c:data) { a=(a+c)%65521; b=(b+a)%65521; } return (b<<16)|a;
}
}
std::vector<uint8_t> ApplyDelta(std::span<const uint8_t> original, std::span<const uint8_t> delta) {
    Reader r{delta}; if(r.Byte()!=0xd6||r.Byte()!=0xc3||r.Byte()!=0xc4||r.Byte()!=0) Bad();
    const auto flags=r.Byte(); if(flags&~4) Bad(); if(flags&4) r.Take(r.Int());
    static const auto table=Table(); std::vector<uint8_t> output;
    while(r.at<delta.size()) {
        const auto indicator=r.Byte(); if(indicator&~7 || (indicator&3)==3) Bad();
        size_t source_size=0,source_at=0; std::span<const uint8_t> source;
        if(indicator&3) {
            source_size=r.Int(); source_at=r.Int();
            const auto input=(indicator&1)?original:std::span<const uint8_t>(output);
            if(source_at>input.size() || source_size>input.size()-source_at) Bad();
            // Copy target-source windows before output growth can invalidate their storage.
            source=input.subspan(source_at,source_size);
        }
        std::vector<uint8_t> source_copy;
        if(indicator&2) { source_copy.assign(source.begin(),source.end()); source=source_copy; }
        const auto delta_size=r.Int(); Reader w{r.Take(delta_size)};
        const auto target_size=w.Int(); if(target_size>limit-output.size()) Bad();
        if(w.Byte()!=0) Bad();
        const auto data_len=w.Int(),inst_len=w.Int(),addr_len=w.Int();
        uint32_t checksum=0; if(indicator&4) for(int i=0;i<4;++i) checksum=(checksum<<8)|w.Byte();
        Reader data{w.Take(data_len)},inst{w.Take(inst_len)},addr{w.Take(addr_len)};
        if(w.at!=w.bytes.size()) Bad();
        const size_t begin=output.size(); output.reserve(begin+target_size);
        std::array<size_t,4> near{}; std::array<size_t,768> same{}; size_t next=0;
        auto execute=[&](const Op& op) {
            if(op.type==None) return;
            const size_t n=op.size?op.size:inst.Int();
            if(n>target_size-(output.size()-begin)) Bad();
            if(op.type==Add) { const auto bytes=data.Take(n); output.insert(output.end(),bytes.begin(),bytes.end()); }
            else if(op.type==Run) { const auto byte=data.Byte(); output.insert(output.end(),n,byte); }
            else {
                const auto here=source_size+output.size()-begin; size_t address=0;
                if(op.mode==0) address=addr.Int();
                else if(op.mode==1) { auto back=addr.Int(); if(back>here) Bad(); address=here-back; }
                else if(op.mode<6) { auto offset=addr.Int(); if(offset>std::numeric_limits<size_t>::max()-near[op.mode-2]) Bad(); address=near[op.mode-2]+offset; }
                else address=same[(op.mode-6)*256+addr.Byte()];
                near[next]=address; next=(next+1)%near.size(); same[address%same.size()]=address;
                if(n && address>=here) Bad();
                for(size_t i=0;i<n;++i) {
                    if(address<source_size) output.push_back(source[address]);
                    else { auto pos=address-source_size; if(pos>=output.size()-begin) Bad(); output.push_back(output[begin+pos]); }
                    ++address;
                }
            }
        };
        while(inst.at<inst.bytes.size()) { const auto code=table[inst.Byte()]; execute(code.a); execute(code.b); }
        if(output.size()-begin!=target_size || data.at!=data.bytes.size() || addr.at!=addr.bytes.size()) Bad();
        if((indicator&4) && Adler(std::span<const uint8_t>(output).subspan(begin))!=checksum) Bad();
    }
    return output;
}
}

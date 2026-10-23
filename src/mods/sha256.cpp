#include "sha256.h"
#include <array>
#include <bit>
#include <fstream>
#include <stdexcept>
namespace shaolin::mods {
namespace {
struct Hash {
    std::array<uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<uint8_t,64> buffer{};
    uint64_t length = 0; size_t used = 0;
    void Block() {
        static constexpr uint32_t k[]{0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
            0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
            0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64]{};
        for (int i=0;i<16;++i) for(int j=0;j<4;++j) w[i]=(w[i]<<8)|buffer[i*4+j];
        for(int i=16;i<64;++i) { const auto a=w[i-15],b=w[i-2]; w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10)); }
        auto v=h;
        for(int i=0;i<64;++i) {
            const auto s1=std::rotr(v[4],6)^std::rotr(v[4],11)^std::rotr(v[4],25);
            const auto t1=v[7]+s1+((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i];
            const auto t2=(std::rotr(v[0],2)^std::rotr(v[0],13)^std::rotr(v[0],22))+((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
            for(int j=7;j>0;--j) v[j]=v[j-1];
            v[4]+=t1; v[0]=t1+t2;
        }
        for(int i=0;i<8;++i) h[i]+=v[i];
    }
    void Add(std::span<const uint8_t> bytes) {
        length+=bytes.size();
        for(auto b:bytes) { buffer[used++]=b; if(used==64) { Block(); used=0; } }
    }
    std::string Finish() {
        const auto bits=length*8;
        const uint8_t marker=0x80,zero=0;
        Add({&marker,1}); while(used!=56) Add({&zero,1});
        uint8_t tail[8]; for(int i=0;i<8;++i) tail[i]=uint8_t(bits>>((7-i)*8)); Add(tail);
        const char hex[]="0123456789abcdef"; std::string out;
        for(auto word:h) for(int i=7;i>=0;--i) out+=hex[(word>>(i*4))&15]; return out;
    }
};
}
std::string Sha256(std::span<const uint8_t> data) { Hash h; h.Add(data); return h.Finish(); }
std::string Sha256File(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary); if(!file) throw std::runtime_error("Cannot read file for SHA-256 verification");
    Hash h; std::array<uint8_t,65536> buffer;
    while(file) { file.read(reinterpret_cast<char*>(buffer.data()),buffer.size()); h.Add({buffer.data(),size_t(file.gcount())}); }
    if(!file.eof()) throw std::runtime_error("File read failed during SHA-256 verification");
    return h.Finish();
}
}

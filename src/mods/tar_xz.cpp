#include "tar_xz.h"
#include "../file_ops.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <span>
extern "C" {
#include <xz.h>
}
namespace fs=std::filesystem;
namespace shaolin::mods {
namespace {
struct Input {
    std::ifstream file;
    std::unique_ptr<xz_dec,decltype(&xz_dec_end)> dec{nullptr,xz_dec_end};
    std::array<uint8_t,65536> in{},out{};
    xz_buf b{}; size_t at=0,available=0; bool ended=false;
    uint64_t consumed=0,total=0;
    Progress progress;
    Input(const fs::path& path,const Progress& p):file(path,std::ios::binary),progress(p) {
        if(!file) throw std::runtime_error("Cannot open mod archive"); total=fs::file_size(path);
        static std::once_flag crc; std::call_once(crc,[]{xz_crc32_init();xz_crc64_init();});
        dec.reset(xz_dec_init(XZ_DYNALLOC,64*1024*1024)); if(!dec) throw std::runtime_error("Cannot allocate XZ decoder");
        b.in=in.data(); b.out=out.data(); b.out_size=out.size();
    }
    bool Fill() {
        if(ended) return false;
        while(true) {
            if(b.in_pos==b.in_size) {
                file.read(reinterpret_cast<char*>(in.data()),in.size()); b.in_pos=0;b.in_size=size_t(file.gcount()); consumed+=b.in_size;
                if(!file.eof()&&file.fail()) throw std::runtime_error("Cannot read mod archive");
            }
            b.out_pos=0; const auto before=b.in_pos;
            const auto status=xz_dec_run(dec.get(),&b); at=0;available=b.out_pos;
            Report(progress,"Extracting",consumed-(b.in_size-b.in_pos),total);
            if(status==XZ_STREAM_END) {
                ended=true; if(b.in_pos!=b.in_size||file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Trailing data after the XZ stream");
            } else if(status!=XZ_OK) throw std::runtime_error("Corrupt, truncated or unsupported XZ archive (decoder status "+std::to_string(status)+")");
            if(available) return true;
            if(ended) return false;
            if(before==b.in_pos && !b.in_size) throw std::runtime_error("Truncated XZ archive");
        }
    }
    size_t Read(std::span<uint8_t> target) {
        size_t n=0;
        while(n<target.size()) {
            if(at==available&&!Fill()) break;
            auto count=std::min(target.size()-n,available-at);
            std::copy_n(out.data()+at,count,target.data()+n); at+=count;n+=count;
        } return n;
    }
    void Exact(std::span<uint8_t> target) {
        if(Read(target)!=target.size())throw std::runtime_error("The mod archive is incomplete. Download it again or rebuild it with the Krypt builder.");
    }
};
std::string Field(const uint8_t* p,size_t n) { size_t len=0;while(len<n&&p[len])++len;return {reinterpret_cast<const char*>(p),len}; }
uint64_t Octal(const uint8_t* p,size_t n) {
    uint64_t value=0; bool ended=false;
    for(size_t i=0;i<n;++i) {
        if(p[i]==' '||p[i]==0) { if(value||p[i]==0) ended=true; continue; }
        if(ended||p[i]<'0'||p[i]>'7'||value>(UINT64_MAX>>3)) throw std::runtime_error("Invalid TAR number");
        value=(value<<3)|(p[i]-'0');
    } return value;
}
}
void ExtractTarXz(const fs::path& file,const fs::path& destination,const Progress& progress) {
    if(!fs::is_directory(destination)||!fs::is_empty(destination)) throw std::runtime_error("Mod staging folder must be empty");
    Input input(file,progress); std::array<uint8_t,512> header{}; std::array<uint8_t,65536> buffer{};
    uint64_t total=0; unsigned count=0; std::set<std::string> names;
    while(true) {
        input.Exact(header);
        if(std::all_of(header.begin(),header.end(),[](auto c){return c==0;})) {
            input.Exact(header);
            if(!std::all_of(header.begin(),header.end(),[](auto c){return c==0;})) throw std::runtime_error("Invalid TAR terminator");
            size_t padding=0,n=0;
            while((n=input.Read(buffer))) {
                padding+=n;
                if(padding>10240||!std::all_of(buffer.begin(),buffer.begin()+n,[](auto c){return c==0;})) throw std::runtime_error("Unexpected data after TAR terminator");
            }
            return;
        }
        unsigned checksum=0;for(size_t i=0;i<header.size();++i)checksum+=i>=148&&i<156?' ':header[i];
        if(checksum!=Octal(header.data()+148,8)||Field(header.data()+257,6)!="ustar") throw std::runtime_error("Invalid USTAR header or checksum");
        auto name=Field(header.data(),100); const auto prefix=Field(header.data()+345,155); if(!prefix.empty())name=prefix+"/"+name;
        const auto type=header[156]; const bool directory=type=='5';
        if(directory&&name.ends_with('/'))name.pop_back();
        if(!files::SafeRelative(name)||(type!=0&&type!='0'&&!directory)||++count>20000) throw std::runtime_error("Unsafe or unsupported TAR entry");
        std::string key=name;for(auto& c:key)if(c>='A'&&c<='Z')c+=32;
        if(!names.insert(key).second) throw std::runtime_error("Duplicate TAR entry");
        const auto size=Octal(header.data()+124,12);total+=size;
        if(size>512*1024*1024||total>8ull*1024*1024*1024||(directory&&size)||(key=="mod.json"||key=="manifest.json")&&size>8*1024*1024) throw std::runtime_error("Mod archive exceeds extraction limits");
        const auto target=destination/files::FromUtf8(name);
        if(directory) { fs::create_directories(target); continue; }
        fs::create_directories(target.parent_path());
        if(fs::exists(target))throw std::runtime_error("Conflicting TAR entries");
        std::ofstream output(target,std::ios::binary);if(!output)throw std::runtime_error("Cannot create mod file; check permissions");
        uint64_t remaining=size;
        while(remaining) { auto n=size_t(std::min<uint64_t>(remaining,buffer.size()));input.Exact({buffer.data(),n});output.write(reinterpret_cast<const char*>(buffer.data()),n);remaining-=n;
            if(!output)throw std::runtime_error("Cannot write mod file; check free disk space"); }
        output.close();if(!output)throw std::runtime_error("Cannot finish mod file; check free disk space");
        auto pad=size_t((512-size%512)%512);input.Exact({buffer.data(),pad});
    }
}
}

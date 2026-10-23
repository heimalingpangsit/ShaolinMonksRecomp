#include "game_reader.h"
#include "sha256.h"
#include "../file_ops.h"
#include <fstream>
#include <algorithm>
#include <cstring>
namespace fs=std::filesystem;
namespace shaolin::mods {
namespace {
std::string Lower(std::string value) {for(auto& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
uint32_t Le(const uint8_t* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
}
GameReader::GameReader(const fs::path& input) {
    auto path=input;if(Lower(files::Utf8(path.filename()))=="default.xbe")path=path.parent_path();
    if(fs::is_directory(path))folder_=path;
    else {std::string error;image_=true;if(!disc_.Open(path,error))throw std::runtime_error("Select an extracted game folder, ISO or XISO before installing mods. "+error);}
    if(Sha256(Read("default.xbe"))!=GameSha256)throw std::runtime_error("Your default.xbe is not the supported USA/Europe game version");
}
std::vector<uint8_t> GameReader::Read(std::string name,uint64_t offset,uint64_t size) {
    std::replace(name.begin(),name.end(),'\\','/');if(!files::SafeRelative(name))throw std::runtime_error("Unsafe original game path");
    if(image_) {
        std::replace(name.begin(),name.end(),'/','\\');const auto* entry=disc_.Find(name);
        if(!entry||entry->dir||offset>entry->size)throw std::runtime_error("Original game file is missing: "+name);
        if(size==UINT64_MAX)size=entry->size-offset;
        if(size>entry->size-offset||size>512*1024*1024)throw std::runtime_error("Invalid original game byte range");
        std::vector<uint8_t> out(size);if(disc_.Read(*entry,offset,out.data(),uint32_t(size))!=size)throw std::runtime_error("Cannot read original game data");return out;
    }
    fs::path path=folder_;
    for(const auto& part:files::FromUtf8(name)) {
        auto next=path/part;
        if(!fs::exists(next))for(const auto& e:fs::directory_iterator(path))if(Lower(files::Utf8(e.path().filename()))==Lower(files::Utf8(part))){next=e.path();break;}
        path=next;
    }
    const auto length=fs::file_size(path);if(offset>length)throw std::runtime_error("Original game byte range is out of bounds");
    if(size==UINT64_MAX)size=length-offset;
    if(size>length-offset||size>512*1024*1024)throw std::runtime_error("Invalid original game byte range");
    std::ifstream file(path,std::ios::binary);file.seekg(offset);std::vector<uint8_t> out(size);file.read(reinterpret_cast<char*>(out.data()),size);
    if(size_t(file.gcount())!=size)throw std::runtime_error("Cannot read original game file: "+name);return out;
}
std::vector<uint8_t> GameReader::Original(const Patch& p) {
    if(p.wad.empty())return Read(p.file,p.range?p.offset:0,p.range?p.size:UINT64_MAX);
    const auto header=Read(p.wad+".WAD",0,32);
    if(std::memcmp(header.data(),"PWF ",4))throw std::runtime_error("Invalid original PWF archive");
    const auto count=Le(header.data()+12),stride=Le(header.data()+16);
    if(!count||count>100000||!stride||p.record>=count)throw std::runtime_error("Invalid original PWF record");
    const auto entry=Read(p.wad+".WAD",32+p.record*8,8);const auto a=Le(entry.data()),b=Le(entry.data()+4);
    const uint64_t offset=(b&0x80000000)?uint64_t(a&0x1fffff)*0x800:a;
    const uint64_t size=(b&0x80000000)?((a>>22)|(b<<10))&0x3fffff:b;
    const auto volume=offset/stride;if(volume>22)throw std::runtime_error("Invalid original PWF volume");
    return Read(p.wad+".WA"+char('D'+volume),offset%stride,size);
}
}

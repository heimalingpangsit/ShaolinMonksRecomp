#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

bool Extract(const std::string& path) {
    mz_zip_archive archive{};
    auto* file=std::fopen(path.c_str(),"rb");
    if(!file)throw std::runtime_error("ZIP64 fixture is missing");
    const bool opened=mz_zip_reader_init_cfile(&archive,file,0,0);
    bool valid=opened;
    if(opened)for(const auto* name:{"game/one.txt","game/two.txt"}) {
        const auto index=mz_zip_reader_locate_file(&archive,name,nullptr,0);
        mz_zip_archive_file_stat info{};char output[32]{};
        const std::string expected=std::string(name).ends_with("one.txt")?"first":"second";
        valid=valid&&index>=0&&mz_zip_reader_file_stat(&archive,index,&info)&&info.m_uncomp_size==expected.size();
        valid=valid&&mz_zip_reader_extract_to_mem(&archive,index,output,sizeof(output),0)&&std::string(output,expected.size())==expected;
    }
    if(opened)mz_zip_reader_end(&archive);
    std::fclose(file);return valid;
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Expected ZIP64 fixture directory");
        const std::string root=std::string(argv[1])+"/";
        if(!Extract(root+"valid.zip"))throw std::runtime_error("ZIP64 offsets with ordinary file sizes must extract correctly");
        if(Extract(root+"out-of-bounds.zip"))throw std::runtime_error("ZIP64 offsets must remain inside the archive");
        if(Extract(root+"corrupt.zip"))throw std::runtime_error("ZIP64 entries must retain CRC verification");
        std::cout<<"ZIP64 offsets, archive bounds and CRC checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

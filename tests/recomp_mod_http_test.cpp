// Optional network check against a pinned dependency file. Excluded from offline CTest runs.
#include "mods/http.h"
#include "mods/sha256.h"
#include <chrono>
#include <iostream>
namespace fs=std::filesystem;
int main(){
    const auto root=fs::temp_directory_path()/("shaolin-http-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        using namespace shaolin::mods;
        fs::create_directory(root);
        const char* url="https://raw.githubusercontent.com/tukaani-project/xz-embedded/ae63ae3a36ed01724674e8f3d750dc47bf125410/COPYING";
        Download(url,root/"license",1024*1024);
        if(Sha256File(root/"license")!="d9ff4f089509582e7b197e86f3d2d66948b89169f27c1f2349eb0d1ed4459309")throw std::runtime_error("HTTPS download content mismatch");
        bool rejected=false;try{Download(url,root/"too-small",16);}catch(...){rejected=true;}
        if(!rejected)throw std::runtime_error("Oversize downloads must fail");
        rejected=false;try{Download(url,root/"cancelled",1024*1024,[](auto,uint64_t,uint64_t){return false;});}catch(...){rejected=true;}
        if(!rejected)throw std::runtime_error("Download cancellation must fail before installation");
        rejected=false;try{Download("https://raw.githubusercontent.com/tukaani-project/xz-embedded/ae63ae3a36ed01724674e8f3d750dc47bf125410/does-not-exist",root/"missing",1024*1024);}catch(...){rejected=true;}
        if(!rejected)throw std::runtime_error("HTTP errors must not be accepted as packages");
        fs::remove_all(root);std::cout<<"HTTPS download, hash, size limit, cancellation and HTTP error checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ec;fs::remove_all(root,ec);return 1;}
}

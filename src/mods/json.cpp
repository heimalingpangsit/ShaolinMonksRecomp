#include "json.h"
#include <stdexcept>
#include <charconv>
namespace shaolin::mods {
namespace {
struct Parser {
    std::string_view input; size_t at=0;
    [[noreturn]] void Fail() { throw std::runtime_error("Invalid mod JSON at byte " + std::to_string(at)); }
    void Space() { while(at<input.size() && (input[at]==' ' || input[at]=='\n' || input[at]=='\r' || input[at]=='\t')) ++at; }
    bool Take(char c) { Space(); if(at<input.size() && input[at]==c) { ++at; return true; } return false; }
    unsigned Hex() {
        unsigned value=0;
        for(int i=0;i<4;++i) {
            if(at==input.size()) Fail(); const char c=input[at++];
            const int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(digit<0) Fail(); value=(value<<4)|unsigned(digit);
        } return value;
    }
    void Utf8(std::string& out,unsigned c) {
        if(c<0x80) out+=char(c);
        else if(c<0x800) { out+=char(0xc0|(c>>6)); out+=char(0x80|(c&63)); }
        else if(c<0x10000) { out+=char(0xe0|(c>>12)); out+=char(0x80|((c>>6)&63)); out+=char(0x80|(c&63)); }
        else { out+=char(0xf0|(c>>18)); out+=char(0x80|((c>>12)&63)); out+=char(0x80|((c>>6)&63)); out+=char(0x80|(c&63)); }
    }
    std::string String() {
        if(!Take('"')) Fail(); std::string out;
        while(at<input.size()) {
            unsigned char c=input[at++]; if(c=='"') return out;
            if(c<32) Fail();
            if(c!='\\') { out+=char(c); continue; }
            if(at==input.size()) Fail();
            const char e=input[at++];
            if(e=='"'||e=='\\'||e=='/') out+=e;
            else if(e=='b') out+='\b'; else if(e=='f') out+='\f'; else if(e=='n') out+='\n'; else if(e=='r') out+='\r'; else if(e=='t') out+='\t';
            else if(e=='u') {
                auto v=Hex();
                if(v>=0xd800 && v<=0xdbff) {
                    if(at+2>input.size()||input.substr(at,2)!="\\u") Fail(); at+=2; const auto lo=Hex();
                    if(lo<0xdc00||lo>0xdfff) Fail(); v=0x10000+((v-0xd800)<<10)+(lo-0xdc00);
                } else if(v>=0xdc00 && v<=0xdfff) Fail();
                Utf8(out,v);
            } else Fail();
        } Fail();
    }
    Json Value(unsigned depth=0) {
        if(depth>48) Fail(); Space(); if(at==input.size()) Fail();
        if(input[at]=='"') return Json{String()};
        if(Take('{')) {
            Json::Object out;
            if(Take('}')) return Json{out};
            do { auto key=String(); if(!Take(':')) Fail(); auto value=Value(depth+1); if(!out.emplace(key,std::move(value)).second) Fail(); } while(Take(','));
            if(!Take('}')) Fail(); return Json{out};
        }
        if(Take('[')) {
            Json::Array out; if(Take(']')) return Json{out};
            do { out.push_back(Value(depth+1)); if(out.size()>100000) Fail(); } while(Take(','));
            if(!Take(']')) Fail(); return Json{out};
        }
        for(auto word:{"true","false","null"}) if(input.substr(at,std::char_traits<char>::length(word))==word) {
            at+=std::char_traits<char>::length(word);
            if(word[0]=='n') return {}; return Json{word[0]=='t'};
        }
        const auto start=at; if(input[at]=='-') ++at;
        if(at==input.size()||input[at]<'0'||input[at]>'9') Fail();
        if(input[at]=='0') ++at; else while(at<input.size()&&input[at]>='0'&&input[at]<='9') ++at;
        int64_t n=0; const auto parsed=std::from_chars(input.data()+start,input.data()+at,n);
        if(parsed.ec!=std::errc{}) Fail(); return Json{n};
    }
};
}
Json Json::Parse(std::string_view text) {
    if(text.size()>8*1024*1024) throw std::runtime_error("Mod JSON exceeds 8 MiB");
    Parser p{text}; auto result=p.Value(); p.Space(); if(p.at!=text.size()) p.Fail(); return result;
}
const Json& Json::At(std::string_view key) const { return std::get<Object>(value).at(std::string(key)); }
bool Json::Has(std::string_view key) const { const auto* p=std::get_if<Object>(&value); return p && p->contains(std::string(key)); }
const std::string& Json::Text() const { return std::get<std::string>(value); }
int64_t Json::Number() const { return std::get<int64_t>(value); }
const Json::Array& Json::Items() const { return std::get<Array>(value); }
}

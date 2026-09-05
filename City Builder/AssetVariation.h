#pragma once
#include "SimpleXml.h"
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

// Visual choices share the parent module's dimensions, entrances and simulation data.
struct AssetVariation {
    std::string id, moduleId, wallMaterial="inherit", carStyle="sedan", treeStyle="oak";
    int weight=1, seed=0;
    float red=-1, green=-1, blue=-1;
    std::uint16_t meshHandle=0;
};
using AssetVariationBindings=std::map<std::uint16_t,std::vector<AssetVariation>>;
inline AssetVariation ReadAssetVariation(const std::string& tag) {
    AssetVariation v;
    v.id=XmlAttributeValue(tag,"id","");
    if(v.id.empty()||v.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::runtime_error("Variation IDs must use lowercase letters, digits, underscores or hyphens.");
    auto integer=[&](const char* name,int fallback){auto text=XmlAttributeValue(tag,name,std::to_string(fallback));int n;if(!SimpleXmlDetail::TryParseInt(text,n))throw std::runtime_error(std::string("Invalid variation ")+name);return n;};
    v.weight=integer("weight",1);v.seed=integer("seed",0);
    if(v.weight<1||v.weight>10000||v.seed<0||v.seed>1000000)throw std::runtime_error("Variation weight must be 1-10000 and seed 0-1000000.");
    v.wallMaterial=XmlAttributeValue(tag,"wallMaterial","inherit");
    v.carStyle=XmlAttributeValue(tag,"carStyle","sedan");v.treeStyle=XmlAttributeValue(tag,"treeStyle","oak");
    if(v.wallMaterial!="inherit"&&v.wallMaterial!="brick"&&v.wallMaterial!="render"&&v.wallMaterial!="metal")throw std::runtime_error("Unknown variation wallMaterial.");
    if(v.carStyle!="sedan"&&v.carStyle!="wagon"&&v.carStyle!="pickup")throw std::runtime_error("Unknown variation carStyle.");
    if(v.treeStyle!="oak"&&v.treeStyle!="birch"&&v.treeStyle!="conifer")throw std::runtime_error("Unknown variation treeStyle.");
    auto color=[&](const char* name){auto s=XmlAttributeValue(tag,name,"");if(s.empty())return -1.0f;size_t pos=0;float f=std::stof(s,&pos);if(pos!=s.size()||!std::isfinite(f)||f<0||f>1)throw std::runtime_error("Variation colors must be between 0 and 1.");return f;};
    v.red=color("colorR");v.green=color("colorG");v.blue=color("colorB");return v;
}
inline std::vector<AssetVariation> ReadAssetVariations(const std::string& xml) {
    std::vector<AssetVariation> result;std::set<std::string> ids;
    std::string source=xml;
    for(size_t p=0;(p=source.find("<!--",p))!=std::string::npos;){auto end=source.find("-->",p);if(end==std::string::npos)throw std::runtime_error("Unclosed XML comment.");source.erase(p,end-p+3);}
    for(size_t p=0;(p=source.find("<variation",p))!=std::string::npos;){
        if(p+10>=source.size()||!std::isspace(static_cast<unsigned char>(source[p+10]))){p+=10;continue;}
        auto end=source.find('>',p);if(end==std::string::npos)throw std::runtime_error("Unclosed variation tag.");
        auto v=ReadAssetVariation(source.substr(p,end-p+1));
        if(!ids.insert(v.id).second)throw std::runtime_error("Duplicate variation ID: "+v.id);
        result.push_back(v);p=end+1;
        if(result.size()>32)throw std::runtime_error("A module supports up to 32 visual variations.");
    }
    return result;
}
inline std::uint32_t AssetVisualHash(std::uint32_t h,std::uint32_t value) {
    h^=value+0x9e3779b9u+(h<<6)+(h>>2);h^=h>>16;h*=0x7feb352du;h^=h>>15;return h;
}
inline std::uint16_t SelectAssetVariation(const std::vector<AssetVariation>& variants,std::uint32_t seed,std::uint16_t fallback) {
    unsigned total=0;for(const auto& v:variants)total+=v.weight;if(!total)return fallback;
    unsigned choice=seed%total;for(const auto& v:variants){if(choice<unsigned(v.weight))return v.meshHandle;choice-=v.weight;}return fallback;
}

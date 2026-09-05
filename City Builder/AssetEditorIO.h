#pragma once
#include "AssetPipeline.h"
#include "AssetLoader.h"
#include <filesystem>

namespace AssetEditorIO {
namespace fs=std::filesystem;
inline void Write(const fs::path& path,const std::string& text){
    std::ofstream out(path,std::ios::binary);out<<text;
    if(!out)throw std::runtime_error("Cannot write "+path.string());
}
struct StagingDirectory {
    fs::path path;
    explicit StagingDirectory(const fs::path& root){
        path=root/"Generated"/("Edit-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        fs::create_directories(path);
    }
    ~StagingDirectory(){std::error_code ignored;fs::remove_all(path,ignored);}
};
inline void Save(const fs::path& root,const fs::path& source,const std::string& expected,const std::string& xml){
    auto relative=fs::relative(source,root);
    if(relative.empty()||*relative.begin()==".."||source.extension()!=".xml")throw std::runtime_error("Asset is outside the data directory.");
    if(XmlReadFileToString(source.string())!=expected)throw std::runtime_error("This file changed on disk. Reload it before saving.");
    const std::string tag=relative.begin()->string()=="Modules"?"module":"lot";
    if(XmlAttributeValue(AssetArt::Tag(xml,tag),"id","")!=XmlAttributeValue(AssetArt::Tag(expected,tag),"id",""))
        throw std::runtime_error("Keep the asset ID stable so existing lots and saves can resolve it.");
    StagingDirectory staging(root);
    for(const auto& entry:fs::directory_iterator(root))if(entry.is_directory()&&entry.path().filename()!="Generated"&&entry.path().filename()!="RetiredAssets")
        fs::copy(entry.path(),staging.path/entry.path().filename(),fs::copy_options::recursive);
    Write(staging.path/relative,xml);
    LoadedGameAssets loaded;CityParameterRegistry registry;std::string error;
    if(!LoadGameAssets(staging.path.string(),registry,loaded,error)||!loaded.invalidLotReports.empty())
        throw std::runtime_error(error.empty()?loaded.invalidLotReports.front():error);
    AssetArt::GenerateAssetCatalog(staging.path.string());
    const auto file=source.string();
    fs::copy_file(source,file+".bak",fs::copy_options::overwrite_existing);
    Write(file+".tmp",xml);
    if(!MoveFileExA((file+".tmp").c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace asset XML.");
    try {
        // The large catalog is replaced atomically too; a failed publish restores XML.
        auto target=root/"Generated"/"module_meshes.txt";
        if(!MoveFileExA((staging.path/"Generated"/"module_meshes.txt").string().c_str(),target.string().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish generated catalog.");
        for(const char* extra:{"asset_report.csv","materials.ppm"})fs::copy_file(staging.path/"Generated"/extra,root/"Generated"/extra,fs::copy_options::overwrite_existing);
    }catch(...){Write(file,expected);AssetArt::GenerateAssetCatalog(root.string());throw;}
}
}

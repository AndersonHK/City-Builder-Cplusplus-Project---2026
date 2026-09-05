#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "AssetPipeline.h"
#include <iostream>
int main(int argc,char**argv){
    try {AssetArt::GenerateAssetCatalog(argc>1?argv[1]:"Data");std::cout<<"Generated metric meshes, material swatches and asset report.\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

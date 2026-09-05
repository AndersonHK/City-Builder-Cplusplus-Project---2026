#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

// Original seamless 2m material swatches. Generated offline for inspection and
// identically at GPU initialization, then kept resident with mipmaps.
enum LotMaterial { Plain, Brick, Render, Roof, Glass, Concrete, Asphalt, Grass, Metal, Wood, Foliage, Gravel, MaterialCount };
constexpr int kLotTextureSize = 256;
inline std::vector<unsigned char> BuildLotMaterialPixels() {
    const int n = kLotTextureSize;
    std::vector<unsigned char> out(n*n*4*MaterialCount);
    for (int m=0;m<MaterialCount;++m) for (int y=0;y<n;++y) for (int x=0;x<n;++x) {
        uint32_t h = uint32_t(x + y*127 + m*911)*747796405u + 2891336453u;
        h = ((h >> ((h >> 28u)+4u)) ^ h)*277803737u; h=(h>>22u)^h;
        float noise = float(h & 255)/255.0f;
        float v=0.90f + (noise-0.5f)*0.10f;
        if(m==Plain || m==Glass) v=1.0f;
        if(m==Brick) {
            int row=y/10, bx=(x+(row%2)*16)%32;
            bool mortar = y%10<1 || bx<1;
            v=mortar ? 0.60f : 0.83f+float((row*13+(x+(row%2)*16)/32*19)%17)/100.0f+(noise-0.5f)*0.12f;
        }
        if(m==Roof) v=(y%16<2 ? 0.53f : 0.85f)+(noise-0.5f)*0.18f+(((x+(y/16%2)*16)%32<1)?-0.16f:0);
        if(m==Render) v=0.95f+(noise-0.5f)*0.10f;
        if(m==Concrete) v=(x<2 || y<2 ? 0.69f : 0.94f)+(noise-0.5f)*0.11f;
        if(m==Asphalt) v=0.86f+(noise-0.5f)*0.30f;
        if(m==Grass || m==Foliage) v=0.84f+(noise-0.5f)*0.30f;
        if(m==Metal) v=x%16<2 ? 0.62f : 0.95f+(noise-0.5f)*0.07f;
        if(m==Wood) v=0.83f+0.11f*std::sin(float(x)*0.65f+noise)+(y%32<2?-0.2f:0);
        if(m==Gravel) v=0.74f+noise*0.26f;
        auto i=((m*n+y)*n+x)*4;
        out[i]=out[i+1]=out[i+2]=(unsigned char)(std::max(0.0f,std::min(1.0f,v))*255);
        out[i+3]=255;
    }
    return out;
}

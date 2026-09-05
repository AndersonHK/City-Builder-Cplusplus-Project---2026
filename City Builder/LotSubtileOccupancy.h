#pragma once
#include "WorldScale.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Three ownership cells per tile. Geometry can be finer (doorways and cap
// portals), but random props may not share an access-owned two-metre cell.
class LotSubtileOccupancy {
public:
    static constexpr int CellsPerTile=3;
    LotSubtileOccupancy(float width,float depth)
        : width_(int(std::ceil(width*CellsPerTile))),depth_(int(std::ceil(depth*CellsPerTile))),cells_(width_*depth_,false) {}
    void reserve(float x,float z,float w,float d){
        visit(x,z,w,d,[&](int index){cells_[index]=true;});
    }
    bool free(float x,float z,float w,float d) const {
        if(x<0||z<0||x+w>width_/float(CellsPerTile)+.001f||z+d>depth_/float(CellsPerTile)+.001f)return false;
        bool available=true;visit(x,z,w,d,[&](int index){if(cells_[index])available=false;});return available;
    }
private:
    template<class F> void visit(float x,float z,float w,float d,F fn) const {
        int left=std::max(0,int(std::floor(x*CellsPerTile+.001f))),front=std::max(0,int(std::floor(z*CellsPerTile+.001f)));
        int right=std::min(width_,int(std::ceil((x+w)*CellsPerTile-.001f))),back=std::min(depth_,int(std::ceil((z+d)*CellsPerTile-.001f)));
        for(int zz=front;zz<back;++zz)for(int xx=left;xx<right;++xx)fn(zz*width_+xx);
    }
    int width_,depth_;
    std::vector<bool> cells_;
};

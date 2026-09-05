#pragma once
#include "Lot.h"
#include "WorldScale.h"
#include <algorithm>
#include <cmath>
#include <queue>

// The access surface layout is presentation data, rebuilt with the lot snapshot.
// It uses actual metric model bounds, including on older saved placements.
namespace LotAccessVisuals {
template<class T> inline T Clamp(T v,T lo,T hi){return std::max(lo,std::min(v,hi));}
struct Rect {float x,z,w,d;};
inline bool Intersects(Rect a,Rect b){return a.x<b.x+b.w-0.001f&&a.x+a.w>b.x+0.001f&&a.z<b.z+b.d-0.001f&&a.z+a.d>b.z+0.001f;}
struct Building {Rect bounds;const LotModule* module;};
struct Surface {Rect bounds;int kind;}; // 0 path, 1 garage apron, 3 middle tile, 4/5 left/right cap
inline Rect ToFront(Rect a,float x,float z,float w,float d,int r){
    a.x-=x;a.z-=z;
    if(r==1)return {a.z,w-a.x-a.w,a.d,a.w};
    if(r==2)return {w-a.x-a.w,d-a.z-a.d,a.w,a.d};
    if(r==3)return {d-a.z-a.d,a.x,a.d,a.w};
    return a;
}
inline Rect FromFront(Rect a,float x,float z,float w,float d,int r){
    if(r==1)return {x+w-a.z-a.d,z+a.x,a.d,a.w};
    if(r==2)return {x+w-a.x-a.w,z+d-a.z-a.d,a.w,a.d};
    if(r==3)return {x+a.z,z+d-a.x-a.w,a.d,a.w};
    return {x+a.x,z+a.z,a.w,a.d};
}
inline Rect Bounds(const LotRenderInstance& i){return {i.originX+i.renderOffsetX,i.originY+i.renderOffsetY,i.renderWidth,i.renderHeightOverride};}
inline std::vector<Surface> Plan(float width,float depth,const std::vector<Building>& buildings){
    std::vector<Surface> result;
    const float pathWidth=MetersToTiles(width<=3?1.0f:1.5f),driveWidth=MetersToTiles(3.0f);
    auto clear=[&](Rect rect){for(const auto& b:buildings)if(Intersects(rect,b.bounds))return false;return rect.x>=0&&rect.z>=0&&rect.x+rect.w<=width&&rect.z+rect.d<=depth;};
    auto walkClear=[&](Rect rect){
        if(rect.x<0||rect.z<0||rect.x+rect.w>width||rect.z+rect.d>depth)return false;
        for(const auto& b:buildings){
            const auto& f=b.module->artFamily;
            bool small=f=="house"||f=="duplex"||f=="rowhouse"||f=="trailer";
            float side=MetersToTiles(small?0.4f:0.55f),front=MetersToTiles(small?1.6f:0.95f);
            Rect body{b.bounds.x+side,b.bounds.z+front,b.bounds.w-2*side,b.bounds.d-front-MetersToTiles(0.6f)};
            if(Intersects(rect,body))return false;
        }
        return true;
    };
    bool parking=false;
    Rect parkingCap{};
    int capKind=4;
    float bestScore=1e9f;
    // Compose integral 6x6m modules. Prefer an inner end near the house, not
    // the parcel corner; the same middle tile repeats without stretching cars.
    for(const auto& b:buildings){
        const auto& f=b.module->artFamily;
        if(f!="house"&&f!="duplex"&&f!="trailer"&&f!="factory"&&f!="warehouse"&&f!="workshop")continue;
        int preferredLength=std::min(int(depth),std::max(1,int(std::ceil(b.bounds.z+0.60f))));
        for(int length=1;length<=preferredLength;++length)for(int x=0;x<int(width);++x){
            Rect strip{float(x),0,1,float(length)};if(!clear(strip))continue;
            float score=(preferredLength-length)*3.0f+std::abs(x+0.5f-(b.bounds.x+b.bounds.w/2));
            if(score<bestScore){bestScore=score;parkingCap={float(x),float(length-1),1,1};parking=true;capKind=x+0.5f>b.bounds.x+b.bounds.w/2?4:5;}
        }
    }
    if(parking){
        for(int z=0;z<int(parkingCap.z);++z)result.push_back({{parkingCap.x,float(z),1,1},3});
        result.push_back({parkingCap,capKind});
    }
    const bool hasParkingCap=parking;
    // Route on a 0.75m grid. Obstacles are dilated by half the path width.
    const float step=MetersToTiles(0.75f);int nx=std::max(1,int(std::ceil(width/step))),nz=std::max(1,int(std::ceil(depth/step)));
    auto route=[&](float targetX,float targetZ,float startX=-1.0f,float startZ=0.0f){
        const int tx=Clamp(int(targetX/step),0,nx-1),tz=Clamp(int(targetZ/step),0,nz-1),goal=tz*nx+tx;
        std::vector<int> previous(nx*nz,-2);std::queue<int> q;
        // Choose the legal sidewalk entry nearest the actual door.
        if(startX>=0){
            int sx=Clamp(int(startX/step),0,nx-1),sz=Clamp(int(startZ/step),0,nz-1);
            previous[sz*nx+sx]=-1;q.push(sz*nx+sx);
        }
        for(int x=0;startX<0&&x<nx;++x){Rect cell{Clamp(x*step-pathWidth*0.5f+step*0.5f,0.0f,width-pathWidth),0,pathWidth,step};if(walkClear(cell)){int idx=x;previous[idx]=-1;q.push(idx);}}
        while(!q.empty()&&previous[goal]==-2){int at=q.front();q.pop();int x=at%nx,z=at/nx;
            const int dx[]={0,1,-1,0},dz[]={1,0,0,-1};
            for(int k=0;k<4;++k){int xx=x+dx[k],zz=z+dz[k];if(xx<0||zz<0||xx>=nx||zz>=nz)continue;int next=zz*nx+xx;if(previous[next]!=-2)continue;
                Rect cell{Clamp((xx+0.5f)*step-pathWidth*0.5f,0.0f,width-pathWidth),Clamp((zz+0.5f)*step-pathWidth*0.5f,0.0f,depth-pathWidth),pathWidth,pathWidth};
                if(next!=goal&&!walkClear(cell))continue;previous[next]=at;q.push(next);
            }
        }
        if(previous[goal]==-2)return false;
        int current=goal;
        while(current>=0){int x=current%nx,z=current/nx;
            result.push_back({{Clamp((x+0.5f)*step-pathWidth/2,0.0f,width-pathWidth),Clamp((z+0.5f)*step-pathWidth/2,0.0f,depth-pathWidth),pathWidth,pathWidth},0});current=previous[current];
        }
        float cx=(tx+0.5f)*step,cz=(tz+0.5f)*step;
        result.push_back({{std::min(cx,targetX)-pathWidth/2,cz-pathWidth/2,std::abs(targetX-cx)+pathWidth,pathWidth},0});
        result.push_back({{targetX-pathWidth/2,std::min(cz,targetZ),pathWidth,std::abs(targetZ-cz)+pathWidth/2},0});
        return true;
    };
    bool linkedParking=false;
    for(const auto& b:buildings){
        auto f=b.module->artFamily;float W=b.bounds.w*6;
        int doors=f=="duplex"?2:(f=="rowhouse"?std::max(1,int(W/4.8f)):1);
        for(int i=0;i<doors;++i){
            float doorX=b.bounds.x+b.bounds.w*(i+0.5f)/doors;
            float entryZ=b.bounds.z+MetersToTiles((f=="house"||f=="duplex"||f=="rowhouse")?1.6f:1.0f);
            if(!route(doorX,std::max(0.0f,b.bounds.z-pathWidth*0.5f)))continue;
            result.push_back({{doorX-pathWidth/2,std::max(0.0f,b.bounds.z-pathWidth),pathWidth,entryZ-std::max(0.0f,b.bounds.z-pathWidth)},0});
            if(hasParkingCap&&!linkedParking){
                // The cap has a 1.5m-wide exit across its inner end to one side.
                float exitX=parkingCap.x+(capKind==4?0.0f:1.0f);
                linkedParking=route(doorX,std::max(0.0f,b.bounds.z-pathWidth*0.5f),exitX,parkingCap.z+0.875f);
            }
        }
        if(!parking && b.module->hasGarageEntrance){
            float gx=b.bounds.x+MetersToTiles(0.95f);
            Rect approach{gx,0,driveWidth,b.bounds.z+MetersToTiles(1.1f)};
            bool blocked=false;for(const auto& other:buildings)if(&other!=&b&&Intersects(approach,other.bounds))blocked=true;
            if(!blocked&&approach.x+approach.w<=width){result.push_back({approach,1});parking=true;}
        }
    }
    for(auto& surface:result){
        auto& a=surface.bounds;
        float right=std::min(width,a.x+a.w),back=std::min(depth,a.z+a.d);
        a.x=std::max(0.0f,a.x);a.z=std::max(0.0f,a.z);a.w=right-a.x;a.d=back-a.z;
    }
    result.erase(std::remove_if(result.begin(),result.end(),[](const Surface& s){return s.bounds.w<=0||s.bounds.d<=0;}),result.end());
    return result;
}
inline void Append(const Lot& lot,size_t begin,std::vector<LotRenderInstance>& instances){
    int r=((lot.rotationSteps()%4)+4)%4;float x=float(lot.minimumTileX()),z=float(lot.minimumTileY()),w=float(lot.footprintWidth()),d=float(lot.footprintHeight());
    std::vector<Building> buildings;const LotModule* bindings=nullptr;
    for(const auto& p:lot.modules())if(p.module&&p.module->metricGeometry&&p.module->hasPedestrianEntrance){
        for(size_t i=begin;i<instances.size();++i)if(instances[i].renderMeshHandle==p.module->renderMeshHandle&&instances[i].originX==lot.anchorTileX()+p.localOrigin.x&&instances[i].originY==lot.anchorTileY()+p.localOrigin.y){
            buildings.push_back({ToFront(Bounds(instances[i]),x,z,w,d,r),p.module});bindings=p.module;break;
        }
    }
    if(!bindings)return;
    auto surfaces=Plan((r&1)?d:w,(r&1)?w:d,buildings);
    for(size_t i=begin;i<instances.size();++i){auto& inst=instances[i];
        if(inst.renderMeshHandle==bindings->pathMeshHandle||inst.renderMeshHandle==bindings->driveMeshHandle){inst.renderMeshHandle=bindings->grassMeshHandle;inst.renderHeight=MetersToTiles(0.25f);inst.colorR=inst.colorG=inst.colorB=1;}
        if(inst.renderMeshHandle==bindings->gardenMeshHandle||inst.renderMeshHandle==bindings->treeMeshHandle||inst.renderMeshHandle==bindings->fenceMeshHandle){
            Rect a=ToFront(Bounds(inst),x,z,w,d,r);
            for(const auto& s:surfaces)if(Intersects(a,s.bounds)){inst.renderMeshHandle=bindings->grassMeshHandle;inst.renderHeight=MetersToTiles(0.25f);inst.colorR=inst.colorG=inst.colorB=1;break;}
        }
    }
    for(const auto& s:surfaces){
        auto rect=FromFront(s.bounds,x,z,w,d,r);LotRenderInstance inst;inst.lotId=lot.id();inst.originX=int(std::floor(rect.x));inst.originY=int(std::floor(rect.z));
        inst.renderOffsetX=rect.x-inst.originX;inst.renderOffsetY=rect.z-inst.originY;inst.renderWidth=rect.w;inst.renderHeightOverride=rect.d;
        inst.width=int(std::ceil(rect.w+inst.renderOffsetX));inst.height=int(std::ceil(rect.d+inst.renderOffsetY));
        inst.renderHeight=MetersToTiles(s.kind>=4?1.8f:0.30f);inst.colorR=inst.colorG=inst.colorB=1;inst.meshRotation=static_cast<std::uint8_t>(r);
        inst.renderMeshHandle=s.kind==3?bindings->driveMidHandle:(s.kind==4?bindings->driveCapLeftHandle:(s.kind==5?bindings->driveCapRightHandle:(s.kind==1?bindings->accessDriveHandle:bindings->accessPathHandle)));
        instances.push_back(inst);
    }
}
}

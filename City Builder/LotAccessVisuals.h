#pragma once
#include "Lot.h"
#include "WorldScale.h"
#include "LotSubtileOccupancy.h"
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
enum SurfaceKind { Path=0, Apron=1, DriveMiddle=3, DriveCapLeft=4, DriveCapRight=5,
    ParkingAisle=6, ParkingStalls=7, ParkingIsland=8, ServiceYard=9,
    VehicleEntrance=10, DriveCapBoth=11, ParkingCrossing=12, PedestrianPlaza=13 };
struct Surface {Rect bounds;int kind;};
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
// Return world-space ground clearance, not the whole render tile or canopy.
inline std::vector<Rect> ModuleBlockers(const LotModule& module,const LotRenderInstance& instance){
    std::vector<Rect> result;if(module.pathPassable)return result;
    auto bounds=Bounds(instance);bool swapped=(instance.meshRotation&1)!=0;
    float localW=swapped?bounds.d:bounds.w,localD=swapped?bounds.w:bounds.d;
    auto append=[&](Rect local){result.push_back(FromFront(local,bounds.x,bounds.z,bounds.w,bounds.d,instance.meshRotation));};
    if(!module.pathBlockers.empty()){
        for(const auto& b:module.pathBlockers)append({MetersToTiles(b.xMeters)*localW/module.naturalWidth,MetersToTiles(b.zMeters)*localD/module.naturalDepth,MetersToTiles(b.widthMeters)*localW/module.naturalWidth,MetersToTiles(b.depthMeters)*localD/module.naturalDepth});
    }else append({0,0,localW,localD});
    return result;
}
inline std::vector<Surface> Plan(float width,float depth,const std::vector<Building>& buildings,const std::vector<Rect>& obstacles={}){
    std::vector<Surface> result;
    const float pathWidth=MetersToTiles(width<=3?1.0f:1.5f),driveWidth=MetersToTiles(3.0f);
    auto clear=[&](Rect rect){for(const auto& obstacle:obstacles)if(Intersects(rect,obstacle))return false;for(const auto& b:buildings)if(Intersects(rect,b.bounds))return false;return rect.x>=0&&rect.z>=0&&rect.x+rect.w<=width&&rect.z+rect.d<=depth;};
    auto physicalClear=[&](Rect rect){
        for(const auto& obstacle:obstacles)if(Intersects(rect,obstacle))return false;
        for(const auto& surface:result){
            if((surface.kind==3||surface.kind==6||surface.kind==10||surface.kind==1)&&Intersects(rect,surface.bounds))return false;
            if(surface.kind==4||surface.kind==5||surface.kind==11){
                Rect blocked=surface.bounds;blocked.d=.75f;if(Intersects(rect,blocked))return false;
                blocked=surface.bounds;blocked.z+=.75f;blocked.d=.25f;blocked.w=.5f;
                if(surface.kind==4)blocked.x+=.5f;
                if(surface.kind!=11&&Intersects(rect,blocked))return false;
            }
        }
        for(const auto& surface:result)if((surface.kind==7||surface.kind==8||surface.kind==9)&&Intersects(rect,surface.bounds))return false;
        if(rect.x<0||rect.z<0||rect.x+rect.w>width||rect.z+rect.d>depth)return false;
        return true;
    };
    auto walkClear=[&](Rect rect){
        if(!physicalClear(rect))return false;
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
        if(f!="house"&&f!="duplex"&&f!="trailer")continue;
        int preferredLength=std::min(int(depth),std::max(1,int(std::floor(b.bounds.z+0.10f))));
        for(int length=1;length<=preferredLength;++length)for(int x=0;x<int(width);++x){
            Rect strip{float(x),0,1,float(length)};if(!clear(strip))continue;
            float score=(preferredLength-length)*3.0f+std::abs(x+0.5f-(b.bounds.x+b.bounds.w/2))+(x+1>b.bounds.x&&x<b.bounds.x+b.bounds.w?2.0f:0.0f);
            if(score<bestScore){bestScore=score;parkingCap={float(x),float(length-1),1,1};parking=true;capKind=x+0.5f>b.bounds.x+b.bounds.w/2?4:5;}
        }
    }
    if(parking){
        bool left=false,right=false;
        for(const auto& b:buildings){auto f=b.module->artFamily;int count=f=="duplex"?2:1;
            for(int i=0;i<count;++i){float door=b.bounds.x+b.bounds.w*(i+.5f)/count;left|=door<parkingCap.x;right|=door>parkingCap.x+1;}}
        if(left&&right)capKind=11;
        for(int z=0;z<int(parkingCap.z);++z)result.push_back({{parkingCap.x,float(z),1,1},3});
        result.push_back({parkingCap,capKind});
    }
    const bool hasParkingCap=parking;
    bool industrial=false;
    for(const auto& b:buildings)if(b.module->artFamily=="factory"||b.module->artFamily=="warehouse"||b.module->artFamily=="workshop")industrial=true;
    if(industrial){
        // A six-metre drive aisle and a row of five-metre stalls occupy two
        // whole tiles. Leave bays out at pedestrian and loading-door approaches.
        for(const auto& b:buildings){
            int door=Clamp(int(b.bounds.x+b.bounds.w/2),0,int(width)-1);
            int loading=Clamp(int(b.bounds.x+MetersToTiles(1.7f)),0,int(width)-1);
            // Front-set buildings can still have parking courts beside them.
            // Each contiguous court gets its own entrance and pedestrian exit.
            for(int start=0;start<int(width);){
                if(!clear({float(start),0,1,2})){++start;continue;}
                int end=start;while(end+1<int(width)&&clear({float(end+1),0,1,2}))++end;
                if(end-start+1<2){start=end+1;continue;}
                int pedestrian=Clamp(door,start,end),entry=loading>=start&&loading<=end?loading:(pedestrian==start?end:start);
                if(entry==pedestrian)entry=pedestrian==start?end:start;
                for(int xx=start;xx<=end;++xx){
                    result.push_back({{float(xx),0,1,1},xx==entry?10:(xx==pedestrian?12:6)});
                    int kind=xx==pedestrian?13:((xx==loading&&b.bounds.z>=1.99f)?1:((end-start>=3&&(xx==start||xx==end))?8:7));
                    result.push_back({{float(xx),1,1,1},kind});
                }
                start=end+1;
            }
            break;
        }
        // Keep a continuous side service aisle where the parcel permits it.
        int serviceSide=-1;
        for(int xx=0;xx<int(width);++xx)if(clear({float(xx),0,1,depth})){serviceSide=xx;break;}
        if(serviceSide>=0)for(int zz=0;zz<int(depth);++zz){
            Rect cell{float(serviceSide),float(zz),1,1};
            result.erase(std::remove_if(result.begin(),result.end(),[&](const Surface& surface){return Intersects(cell,surface.bounds);}),result.end());
            result.push_back({cell,zz==0?10:1});
        }
        // Service equipment stays beside the work building, clear of the front
        // parking court and the side aisle. Each pad is independently varied.
        for(int zz=2;zz<int(depth);++zz)for(int xx=0;xx<int(width);++xx){
            Rect cell{float(xx),float(zz),1,1};if(!clear(cell)||xx==serviceSide)continue;
            if((xx+zz)%2!=0)continue;
            bool occupied=false;for(const auto& surface:result)if(Intersects(cell,surface.bounds))occupied=true;
            for(const auto& b:buildings)if(b.module->hasGarageEntrance){Rect loading{b.bounds.x+MetersToTiles(.95f),0,driveWidth,b.bounds.z+MetersToTiles(1.1f)};if(Intersects(cell,loading))occupied=true;}
            if(!occupied)result.push_back({cell,9});
        }
    }

    // Route on a 0.75m grid. Obstacles are dilated by half the path width.
    const float step=MetersToTiles(0.75f);int nx=std::max(1,int(std::ceil(width/step))),nz=std::max(1,int(std::ceil(depth/step)));
    auto route=[&](float targetX,float targetZ,float startX=-1.0f,float startZ=0.0f){
        const int tx=Clamp(int(targetX/step),0,nx-1),tz=Clamp(int(targetZ/step),0,nz-1),goal=tz*nx+tx;
        std::vector<int> previous(nx*nz,-2);std::queue<int> q;
        // Choose the legal sidewalk entry nearest the actual door.
        if(startX>=0){
            int sx=Clamp(int(startX/step),0,nx-1),sz=Clamp(int(startZ/step),0,nz-1);
            Rect start{Clamp((sx+.5f)*step-pathWidth/2,0.0f,width-pathWidth),Clamp((sz+.5f)*step-pathWidth/2,0.0f,depth-pathWidth),pathWidth,pathWidth};
            if(walkClear(start)){previous[sz*nx+sx]=-1;q.push(sz*nx+sx);}
        }
        for(int x=0;startX<0&&x<nx;++x){Rect cell{Clamp(x*step-pathWidth*0.5f+step*0.5f,0.0f,width-pathWidth),0,pathWidth,pathWidth};if(walkClear(cell)){int idx=x;previous[idx]=-1;q.push(idx);}}
        while(!q.empty()&&previous[goal]==-2){int at=q.front();q.pop();int x=at%nx,z=at/nx;
            const int dx[]={0,1,-1,0},dz[]={1,0,0,-1};
            for(int k=0;k<4;++k){int xx=x+dx[k],zz=z+dz[k];if(xx<0||zz<0||xx>=nx||zz>=nz)continue;int next=zz*nx+xx;if(previous[next]!=-2)continue;
                Rect cell{Clamp((xx+0.5f)*step-pathWidth*0.5f,0.0f,width-pathWidth),Clamp((zz+0.5f)*step-pathWidth*0.5f,0.0f,depth-pathWidth),pathWidth,pathWidth};
                if(!walkClear(cell))continue;previous[next]=at;q.push(next);
            }
        }
        if(previous[goal]==-2)return false;
        float cx=(tx+.5f)*step,cz=(tz+.5f)*step;
        Rect horizontal{std::min(cx,targetX)-pathWidth/2,cz-pathWidth/2,std::abs(targetX-cx)+pathWidth,pathWidth};
        Rect vertical{targetX-pathWidth/2,std::min(cz,targetZ),pathWidth,std::abs(targetZ-cz)+pathWidth/2};
        auto clip=[&](Rect a){float right=std::min(width,a.x+a.w),back=std::min(depth,a.z+a.d);a.x=std::max(0.f,a.x);a.z=std::max(0.f,a.z);a.w=right-a.x;a.d=back-a.z;return a;};
        horizontal=clip(horizontal);vertical=clip(vertical);
        if(!walkClear(horizontal)||!walkClear(vertical))return false;
        int current=goal;
        while(current>=0){int x=current%nx,z=current/nx;
            result.push_back({{Clamp((x+0.5f)*step-pathWidth/2,0.0f,width-pathWidth),Clamp((z+0.5f)*step-pathWidth/2,0.0f,depth-pathWidth),pathWidth,pathWidth},0});current=previous[current];
        }
        result.push_back({horizontal,0});result.push_back({vertical,0});
        return true;
    };
    // Garage access is an obstacle before pedestrian routing, not an overlay
    // added after paths have already been chosen.
    if(!parking)for(const auto& b:buildings)if(b.module->hasGarageEntrance){
        float gx=b.bounds.x+MetersToTiles(.95f);Rect approach{gx,0,driveWidth,b.bounds.z+MetersToTiles(1.1f)};
        bool blocked=false;for(const auto& other:buildings)if(&other!=&b&&Intersects(approach,other.bounds))blocked=true;
        for(const auto& obstacle:obstacles)if(Intersects(approach,obstacle))blocked=true;
        if(!blocked&&approach.x+approach.w<=width){result.push_back({approach,1});parking=true;break;}
    }
    bool linkedParking=false;
    for(const auto& b:buildings){
        auto f=b.module->artFamily;float W=b.bounds.w*6;
        int doors=f=="duplex"?2:(f=="rowhouse"?std::max(1,int(W/4.8f)):1);
        for(int i=0;i<doors;++i){
            float doorX=b.bounds.x+b.bounds.w*(i+0.5f)/doors;
            float entryZ=b.bounds.z+MetersToTiles((f=="house"||f=="duplex"||f=="rowhouse")?1.6f:1.0f);
            Rect entrance{doorX-pathWidth/2,std::max(0.0f,b.bounds.z-pathWidth),pathWidth,entryZ-std::max(0.0f,b.bounds.z-pathWidth)};
            if(!physicalClear(entrance))continue;
            if(!route(doorX,std::max(0.0f,b.bounds.z-pathWidth*0.5f)))continue;
            result.push_back({entrance,0});
            if(hasParkingCap&&(!linkedParking||capKind==11)){
                // The cap has a 1.5m-wide exit across its inner end to one side.
                float exitX=parkingCap.x+((capKind==4||(capKind==11&&doorX<parkingCap.x))?0.125f:0.875f);
                linkedParking=route(doorX,std::max(0.0f,b.bounds.z-pathWidth*0.5f),exitX,parkingCap.z+0.875f);
            }
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
    bool industrial=false;for(const auto& b:buildings)if(b.module->artFamily=="factory"||b.module->artFamily=="warehouse"||b.module->artFamily=="workshop")industrial=true;
    std::vector<Rect> obstacles;
    std::map<size_t,std::vector<Rect>> landscaping;
    if(!industrial)for(size_t i=begin;i<instances.size();++i){const auto& inst=instances[i];
        const LotModule* module=nullptr;
        for(const auto& p:lot.modules())if(p.module){
            if(p.module->renderMeshHandle==inst.renderMeshHandle)module=p.module;
            for(const auto& prop:p.module->props){if(prop.module&&prop.module->renderMeshHandle==inst.renderMeshHandle)module=prop.module;for(auto alternative:prop.alternativeModules)if(alternative&&alternative->renderMeshHandle==inst.renderMeshHandle)module=alternative;}
        }
        if(!module||module->pathPassable||module->hasPedestrianEntrance||inst.renderMeshHandle==bindings->driveMeshHandle)continue;
        for(const auto& world:ModuleBlockers(*module,inst)){
            auto front=ToFront(world,x,z,w,d,r);
            if(module->optionalLandscape)landscaping[i].push_back(front);else {
                // Fixed props own every 2m cell touched by their footprint.
                float left=std::floor(front.x*3+.001f)/3,top=std::floor(front.z*3+.001f)/3;
                obstacles.push_back({left,top,std::ceil((front.x+front.w)*3-.001f)/3-left,std::ceil((front.z+front.d)*3-.001f)/3-top});
            }
        }
    }

    auto surfaces=Plan((r&1)?d:w,(r&1)?w:d,buildings,obstacles);
    const float frontW=(r&1)?d:w,frontD=(r&1)?w:d;
    LotSubtileOccupancy ownership(frontW,frontD);
    for(const auto& b:buildings)ownership.reserve(b.bounds.x,b.bounds.z,b.bounds.w,b.bounds.d);
    for(const auto& obstacle:obstacles)ownership.reserve(obstacle.x,obstacle.z,obstacle.w,obstacle.d);
    for(const auto& surface:surfaces)ownership.reserve(surface.bounds.x,surface.bounds.z,surface.bounds.w,surface.bounds.d);
    // Optional props are admitted only after the lot owns its access sub-tiles.
    // Try nearby two-metre positions before omitting a conflicting prop. Its
    // mesh stays at its original physical scale; only its position changes.
    for(const auto& decoration:landscaping){
        auto& inst=instances[decoration.first];const auto& roots=decoration.second;
        auto bounds=ToFront(Bounds(inst),x,z,w,d,r);bool placed=false;
        bool movable=inst.renderMeshHandle!=bindings->fenceMeshHandle;
        const float offsets[]={0,-1.0f/3,1.0f/3};
        for(float dz:offsets){for(float dx:offsets){
            if(!movable&&(dx!=0||dz!=0))continue;
            bool available=true;for(const auto& root:roots)if(!ownership.free(root.x+dx,root.z+dz,root.w,root.d))available=false;
            Rect model{bounds.x+dx,bounds.z+dz,bounds.w,bounds.d};
            if(!available||model.x<0||model.z<0||model.x+model.w>frontW+.001f||model.z+model.d>frontD+.001f)continue;
            bool overlap=false;for(const auto& b:buildings)if(Intersects(model,b.bounds))overlap=true;if(overlap)continue;
            for(const auto& root:roots)ownership.reserve(root.x+dx,root.z+dz,root.w,root.d);
            auto world=FromFront(model,x,z,w,d,r);inst.originX=int(std::floor(world.x));inst.originY=int(std::floor(world.z));inst.renderOffsetX=world.x-inst.originX;inst.renderOffsetY=world.z-inst.originY;
            inst.width=int(std::ceil(inst.renderWidth+inst.renderOffsetX));inst.height=int(std::ceil(inst.renderHeightOverride+inst.renderOffsetY));placed=true;break;
        }if(placed)break;}
        if(!placed){inst.renderMeshHandle=bindings->grassMeshHandle;inst.renderHeight=MetersToTiles(.25f);inst.colorR=inst.colorG=inst.colorB=1;}
    }
    for(size_t i=begin;i<instances.size();++i){auto& inst=instances[i];
        if(industrial){
            bool primary=false;for(const auto& p:lot.modules())if(p.module&&p.module->renderMeshHandle==inst.renderMeshHandle&&(p.module->hasPedestrianEntrance||p.module->artFamily=="stack"))primary=true;
            if(!primary){inst.renderMeshHandle=bindings->concreteHandle;inst.renderHeight=MetersToTiles(.25f);inst.colorR=inst.colorG=inst.colorB=1;}
        }
        if(inst.renderMeshHandle==bindings->pathMeshHandle||inst.renderMeshHandle==bindings->driveMeshHandle){inst.renderMeshHandle=bindings->grassMeshHandle;inst.renderHeight=MetersToTiles(0.25f);inst.colorR=inst.colorG=inst.colorB=1;}

    }
    for(const auto& s:surfaces){
        auto rect=FromFront(s.bounds,x,z,w,d,r);LotRenderInstance inst;inst.lotId=lot.id();inst.originX=int(std::floor(rect.x));inst.originY=int(std::floor(rect.z));
        inst.renderOffsetX=rect.x-inst.originX;inst.renderOffsetY=rect.z-inst.originY;inst.renderWidth=rect.w;inst.renderHeightOverride=rect.d;
        inst.width=int(std::ceil(rect.w+inst.renderOffsetX));inst.height=int(std::ceil(rect.d+inst.renderOffsetY));
        inst.renderHeight=MetersToTiles(s.kind==8?5.4f:(s.kind==9?3.2f:((s.kind==4||s.kind==5||s.kind==7||s.kind==11)?1.8f:0.30f)));inst.colorR=inst.colorG=inst.colorB=1;inst.meshRotation=static_cast<std::uint8_t>(r);
        inst.renderMeshHandle=s.kind==3?bindings->driveMidHandle:(s.kind==4?bindings->driveCapLeftHandle:(s.kind==5?bindings->driveCapRightHandle:(s.kind==1?bindings->accessDriveHandle:bindings->accessPathHandle)));
        if(s.kind==12)inst.renderMeshHandle=bindings->parkingCrossingHandle;
        if(s.kind==13)inst.renderMeshHandle=bindings->concreteHandle;
        if(s.kind==11)inst.renderMeshHandle=bindings->driveCapBothHandle;
        if(s.kind==6)inst.renderMeshHandle=bindings->parkingAisleHandle;
        if(s.kind==7)inst.renderMeshHandle=bindings->parkingStallsHandle;
        if(s.kind==8)inst.renderMeshHandle=bindings->parkingIslandHandle;
        if(s.kind==9)inst.renderMeshHandle=bindings->industrialYardHandle;
        if(s.kind==10)inst.renderMeshHandle=bindings->accessDriveHandle;
        instances.push_back(inst);
    }
}
}

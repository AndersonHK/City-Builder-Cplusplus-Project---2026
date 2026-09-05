#pragma once
#include "AssetMeshBuilder.h"
#include "SimpleXml.h"
#include "AssetVariation.h"
#include "WorldScale.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace AssetArt {
struct Recipe {
    std::string id, family, path, wallMaterial="inherit", carStyle="sedan", treeStyle="oak";
    float width=1, depth=1, height=1;
    int floors=1, capacity=0, seed=0;
    C wall=C(0.62f,0.48f,0.36f);
};
inline std::string Tag(const std::string& xml,const std::string& name){auto p=xml.find("<"+name+" ");if(p==std::string::npos)return "";return xml.substr(p,xml.find('>',p)-p+1);}
inline std::vector<std::string> XmlFiles(const std::string& dir){
    std::vector<std::string> files;WIN32_FIND_DATAA f;HANDLE h=FindFirstFileA((dir+"\\*.xml").c_str(),&f);
    if(h!=INVALID_HANDLE_VALUE){do{if(!(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))files.push_back(dir+"\\"+f.cFileName);}while(FindNextFileA(h,&f));FindClose(h);}
    std::sort(files.begin(),files.end());return files;
}
inline Recipe ReadRecipe(const std::string& path){
    Recipe r;r.path=path;auto xml=XmlReadFileToString(path), t=Tag(xml,"render");
    r.id=XmlAttributeValue(Tag(xml,"module"),"id","");r.family=XmlAttributeValue(t,"family","");
    r.width=XmlAttributeFloatValue(t,"widthMeters",0);r.depth=XmlAttributeFloatValue(t,"depthMeters",0);r.height=XmlAttributeFloatValue(t,"heightMeters",0);
    r.floors=XmlAttributeIntValue(t,"floors",1);r.seed=XmlAttributeIntValue(t,"seed",0);
    r.wall=C(XmlAttributeFloatValue(t,"colorR",0.6f),XmlAttributeFloatValue(t,"colorG",0.5f),XmlAttributeFloatValue(t,"colorB",0.4f));
    r.capacity=XmlAttributeIntValue(Tag(xml,"driver"),"amount",0);
    const std::vector<std::string> families={"house","duplex","rowhouse","trailer","walkup","midrise","court","tower","warehouse","factory","workshop","stack","tree","park","fence","concrete","path","parking","driveway","loading","yard","props","garden","grass","driveway_mid","driveway_cap_left","driveway_cap_right","driveway_cap_both","parking_crossing","parking_aisle","parking_stalls","parking_island","industrial_service"};
    if(std::find(families.begin(),families.end(),r.family)==families.end())throw std::runtime_error("Unknown art family in "+path);
    if(r.family.empty()||!std::isfinite(r.width)||!std::isfinite(r.depth)||!std::isfinite(r.height)||r.width<=0||r.depth<=0||r.height<=0||r.width>100||r.depth>100||r.height>300||r.floors<1||r.floors>80)
        throw std::runtime_error("Invalid metric recipe: "+path);
    return r;
}
inline Recipe VariantRecipe(Recipe r,const AssetVariation& v){
    r.id+="__"+v.id;r.seed=v.seed;r.wallMaterial=v.wallMaterial;r.carStyle=v.carStyle;r.treeStyle=v.treeStyle;
    if(v.red>=0)r.wall.x=v.red;if(v.green>=0)r.wall.y=v.green;if(v.blue>=0)r.wall.z=v.blue;return r;
}
inline void FlatRoof(Mesh& m,float x,float z,float w,float d,float h){
    m.box(x,h,z,w,0.16f,d,roof,Roof);
    for(float zz:{z,z+d-0.18f})m.box(x,h,zz,w,0.45f,0.18f,trim,Concrete);
    for(float xx:{x,x+w-0.18f})m.box(xx,h,z,0.18f,0.45f,d,trim,Concrete);
    for(float xx=x+1.3f;xx<x+w-1.5f;xx+=5.5f){
        m.box(xx,h+0.16f,z+d*0.55f,1.2f,0.58f,1.7f,C(0.48f,0.52f,0.52f),Metal);
        m.cylinder(xx+0.6f,h+0.74f,z+d*0.55f+0.65f,0.38f,0.08f,iron,Metal,16);
    }
}
inline void Building(Mesh& m,const Recipe& r,float x,float z,float w,float d,int floors,bool pitched,bool industrial=false){
    float fh=industrial?std::max(3.6f,(r.height-2.8f)/floors):3.05f;
    float base=0.32f,h=base+fh*floors;
    m.box(x,0.12f,z,w,0.34f,d,concrete,Concrete);
    int wallMat=industrial?Brick:((r.seed%4==0)?Render:Brick);
    if(r.wallMaterial!="inherit")wallMat=r.wallMaterial=="metal"?Metal:(r.wallMaterial=="render"?Render:Brick);
    m.box(x,base,z,w,h-base,d,r.wall,wallMat);
    if(industrial&&wallMat==Metal&&m.detailed)for(int side=0;side<4;++side){float span=side%2==0?w:d;
        for(float u=.3f;u<span-.1f;u+=.45f)m.face(side,x,z,w,d,u,base,.035f,h-base,.005f,.035f,r.wall,Metal);}

    for(int side=0;side<4;++side){
        float span=side%2==0?w:d;
        int bays=std::max(1,int(span/2.8f));float step=span/bays;
        for(int floor=0;floor<floors;++floor){
            if(floor>0)m.face(side,x,z,w,d,0,base+floor*fh-0.12f,span,0.13f,0.01f,0.10f,trim,Concrete);
            for(int bay=0;bay<bays;++bay){
                if(floor==0 && side==0 && (bay==bays/2 || ((r.family=="tower" || industrial) && bay==0))) continue;
                if(industrial && floor==0 && side==2)continue;
                float u=(bay+0.5f)*step-0.62f;
                m.window(side,x,z,w,d,u,base+floor*fh+(industrial?1.65f:0.9f));
                if(!pitched && !industrial && floors>=5 && (bay+r.seed)%3==0 && floor>0){
                    m.face(side,x,z,w,d,u-0.23f,base+floor*fh-0.02f,1.70f,0.13f,0.02f,0.72f,concrete,Concrete);
                    m.face(side,x,z,w,d,u-0.23f,base+floor*fh+0.08f,1.70f,0.86f,0.65f,0.06f,iron,Metal);
                }
            }
        }
        m.face(side,x,z,w,d,0.04f,base,0.12f,h-base,0.015f,0.13f,trim);
        m.face(side,x,z,w,d,span-0.20f,base,0.10f,h-base,0.015f,0.13f,iron,Metal);
    }
    if(industrial){
        for(float y=.5f;y<3.5f;y+=.35f)m.face(0,x,z,w,d,.22f,y,3.16f,.035f,.15f,.025f,iron,Metal);
        m.box(x+.05f,3.82f,z-.85f,3.5f,.15f,.95f,iron,Metal);
        for(float u:{.10f,3.36f})m.face(0,x,z,w,d,u,.25f,.12f,1.1f,.23f,.12f,C(.74f,.59f,.20f));
    }
    float doorX=std::max(0.35f,w*0.5f-0.525f);
    m.door(0,x,z,w,d,doorX);
    if((r.family=="tower" || industrial) && w>6) {
        m.face(0,x,z,w,d,0.1f,0.25f,industrial?3.4f:2.9f,industrial?3.5f:2.6f,0.02f,0.08f,trim,Concrete);
        m.face(0,x,z,w,d,0.2f,0.25f,industrial?3.2f:2.7f,industrial?3.35f:2.45f,0.11f,0.035f,C(0.24f,0.28f,0.28f),Metal);
    }
    m.box(x+doorX-0.25f,0.08f,z-0.78f,1.55f,0.15f,0.82f,concrete,Concrete);
    m.box(x+doorX-0.2f,0.23f,z-0.42f,1.45f,0.10f,0.46f,concrete,Concrete);
    m.box(x+doorX-0.32f,2.70f,z-0.85f,1.7f,0.14f,0.9f,pitched?roof:trim,Metal);
    m.door(2,x,z,w,d,std::max(0.3f,w-1.7f));
    if(pitched){
        m.gable(x-0.22f,z-0.22f,w+0.44f,d+0.44f,h,std::min(r.height-0.15f,h+std::min(2.4f,w*0.28f)),r.wall,r.seed%3==0?C(0.43f,0.30f,0.24f):roof);
        m.box(x+w*0.73f,h-0.6f,z+d*0.65f,0.62f,r.height-h+0.50f,0.72f,r.wall,Brick);
        m.box(x+w*0.73f-0.07f,r.height-0.10f,z+d*0.65f-0.07f,0.76f,0.10f,0.86f,trim,Concrete);
    }else FlatRoof(m,x,z,w,d,h);
}
inline Mesh BuildRecipe(const Recipe& r,bool detailed=true){
    Mesh m;m.detailed=detailed;m.treeStyle=r.treeStyle;m.carStyle=r.carStyle;float W=r.width,D=r.depth,H=r.height;
    const auto& f=r.family;
    if(f=="house"||f=="duplex"||f=="rowhouse"||f=="trailer"){
        int units=f=="duplex"?2:(f=="rowhouse"?std::max(1,int(W/4.8f)):1);
        float unitW=(W-0.8f)/units;
        for(int i=0;i<units;++i)Building(m,r,0.4f+i*unitW,1.60f,unitW-0.10f,D-2.20f,r.floors,f!="trailer");
        if(f!="trailer") {
            float pw=std::min(4.5f,W-1.0f),px=(W-pw)*0.5f;
            m.box(px,0.18f,0.25f,pw,0.18f,1.45f,concrete,Concrete);
            m.box(px-0.10f,2.75f,0.15f,pw+0.2f,0.16f,1.55f,roof,Roof);
            for(float xx:{px+0.12f,px+pw-0.24f})m.box(xx,0.35f,0.35f,0.12f,2.40f,0.12f,trim,Wood);
        }
        if(f=="trailer"){
            m.box(0.3f,0.25f,0.6f,W-0.6f,0.25f,D-1.0f,iron,Metal);
            m.box(0.4f,3.47f,0.75f,W-0.8f,0.2f,D-1.3f,trim,Metal);
        }
    } else if(f=="walkup"||f=="midrise"||f=="tower"||f=="court"){
        if(f=="court"&&W>13&&D>14){
            float wing=std::max(4.5f,W*0.30f);
            Building(m,r,0.85f,0.95f,W-1.7f,5.1f,r.floors,false);
            Building(m,r,0.85f,6.3f,wing,D-7.2f,r.floors,false);
            Building(m,r,W-wing-0.85f,6.3f,wing,D-7.2f,r.floors,false);
            float inner=W-wing*2-1.7f;
            if(inner>3){m.box(wing+1.0f,0.16f,7.0f,inner-0.3f,0.1f,D-8.0f,C(0.37f,0.46f,0.25f),Grass);m.tree(W/2,D*0.70f,5.8f,std::min(1.5f,inner*0.35f));}
        }else if(f=="tower"){
            float x=1.15f,z=1.2f,w=W-2.3f,d=D-2.4f;
            Building(m,r,x,z,w,d,2,false);
            float tw=w*0.74f,td=d*0.75f;
            size_t upperStart=m.vertices.size();
            Building(m,r,(W-tw)*0.5f,(D-td)*0.5f,tw,td,r.floors-2,false);
            for(size_t i=upperStart;i<m.vertices.size();++i)m.vertices[i].y+=6.10f;
            m.box(x-0.25f,0.12f,z-0.25f,w+0.5f,0.25f,d+0.5f,trim,Concrete);
            float top=0.32f+r.floors*3.05f;
            m.box(W*0.35f,top+0.2f,D*0.35f,W*0.28f,1.6f,D*0.25f,r.wall,Render);
            m.box(W*0.35f-0.12f,top+1.8f,D*0.35f-0.12f,W*0.28f+0.24f,0.18f,D*0.25f+0.24f,trim,Concrete);
        }else Building(m,r,0.85f,0.95f,W-1.7f,D-1.7f,r.floors,false);
    } else if(f=="warehouse"||f=="factory"||f=="workshop"){
        float x=0.55f,z=1.0f,w=W-1.1f,d=D-1.6f;
        Building(m,r,x,z,w,d,r.floors,false,true);
        float h=0.32f+std::max(3.6f,(H-2.8f)/r.floors)*r.floors;
        for(float xx=1.0f;xx<w-2.3f;xx+=4.4f){
            m.face(2,x,z,w,d,xx,0.40f,2.8f,3.1f,0.02f,0.12f,C(0.34f,0.39f,0.40f),Metal);
            m.face(2,x,z,w,d,xx-0.13f,0.32f,3.06f,0.12f,0.04f,0.40f,concrete,Concrete);
            m.face(2,x,z,w,d,xx-0.18f,0.3f,0.12f,0.8f,0.3f,0.12f,C(0.67f,0.53f,0.19f));
        }
        for(float zz=2.0f;zz<d-1;zz+=4){
            m.box(x+0.8f,h+0.17f,z+zz,w-1.6f,0.18f,0.9f,iron,Metal);
            // Raised north-light strips, with glazing and metal end closures.
            float a=x+.95f,b=x+w-.95f,za=z+zz+.12f,zb=za+.65f;
            m.quad(V(a,h+.36f,za),V(a,h+.80f,zb),V(b,h+.80f,zb),V(b,h+.36f,za),C(.46f,.57f,.58f),Glass);
            m.box(a,h+.36f,zb,b-a,.44f,.08f,iron,Metal);
            for(float xx=a;xx<b-.04f;xx+=2.5f)m.box(xx,h+.36f,za,.045f,.43f,.66f,trim,Metal);
        }
        if(f=="factory"){
            m.cylinder(W-1.7f,h,D-1.9f,0.5f,1.8f,iron,Metal,24);
            m.cylinder(W-1.7f,h+1.8f,D-1.9f,0.64f,0.13f,trim,Metal,24);
        }
    } else if(f=="stack"){
        m.box(0,0.12f,0,W,0.20f,D,concrete,Concrete);
        int count=W>12?3:1;
        for(int i=0;i<count;++i){float x=W*(i+0.5f)/count,z=D*0.53f,rad=std::min(0.8f,std::min(W/count,D)*0.24f);
            m.cylinder(x,0.32f,z,rad*1.32f,0.50f,concrete,Concrete,24);
            m.cylinder(x,0.82f,z,rad,H-1.0f,r.wall,Brick,32,rad*0.72f);
            m.cylinder(x,H-0.3f,z,rad*0.85f,0.20f,iron,Metal,32);
            m.cylinder(x,H-0.095f,z,rad*0.63f,0.015f,C(0.04f,0.04f,0.04f),Plain,32);
            for(float y=1;y<H-0.5f;y+=0.45f)m.box(x+rad,y,z-0.28f,0.06f,0.05f,0.56f,iron,Metal);
        }
    } else if(f=="driveway_mid"||f=="driveway_cap_left"||f=="driveway_cap_right"||f=="driveway_cap_both"){
        m.box(0,0.12f,0,6,0.085f,6,C(0.40f,0.48f,0.29f),Grass);
        m.box(1.5f,0.12f,0,3,0.11f,6,C(0.31f,0.33f,0.33f),Asphalt);
        for(float edge:{1.4f,4.5f})m.box(edge,0.20f,0,0.1f,0.035f,6,concrete,Concrete);
        if(f!="driveway_mid"){
            m.box(f=="driveway_cap_right"?3.0f:0.0f,0.22f,4.5f,f=="driveway_cap_both"?6.0f:3.0f,0.03f,1.5f,concrete,Concrete);
            m.car(2.10f,0.20f,r.wall);
        }
    } else if(f=="parking_crossing"||f=="parking_aisle"||f=="parking_stalls"||f=="parking_island"){
        m.box(0,0.12f,0,6,0.11f,6,C(0.30f,0.32f,0.32f),Asphalt);
        if(f=="parking_crossing")for(float zz=.25f;zz<6;zz+=.85f)m.box(0,.235f,zz,6,.015f,.35f,trim);
        if(f=="parking_aisle")m.box(0,.23f,0,6,.055f,.18f,concrete,Concrete);
        if(f=="parking_stalls"){
            // Two 2.7m x 5.1m bays face the adjoining six-metre aisle.
            m.box(0,0.23f,5.35f,6,0.055f,0.65f,concrete,Concrete);
            for(float x:{0.30f,3.0f,5.70f})m.box(x,0.235f,0.12f,0.10f,0.015f,5.10f,trim);
            for(float x:{0.72f,3.42f}){
                m.box(x,0.25f,4.85f,1.75f,0.15f,0.16f,concrete,Concrete);
                if((r.seed+(x>3?2:0))%4!=0)m.car(x,0.40f,x>3?C(.57f,.58f,.55f):r.wall);
            }
        }else if(f=="parking_island"){
            m.box(0.25f,0.22f,0.25f,5.5f,0.17f,5.5f,concrete,Concrete);
            m.box(0.40f,0.39f,0.40f,5.20f,0.02f,5.20f,C(.36f,.43f,.27f),Grass);
            m.tree(3,3,4.8f,1.75f);
            m.cylinder(0.80f,.40f,1.0f,.07f,4.8f,iron,Metal,8);
            m.box(.68f,5.1f,.60f,.24f,.13f,.70f,iron,Metal);
        }
    } else if(f=="industrial_service"){
        m.box(0,.12f,0,6,.09f,6,concrete,Concrete);
        if(r.seed%3==0){
            // Container with ribs, corner castings and double rear doors.
            m.box(.45f,.35f,.55f,2.44f,2.59f,4.9f,r.wall,Metal);
            for(float zz=.65f;zz<5.4f;zz+=.30f)for(float xx:{.43f,2.88f})m.box(xx,.42f,zz,.05f,2.44f,.055f,iron,Metal);
            for(float xx:{.6f,1.7f}){m.box(xx,.45f,.50f,1.03f,2.35f,.08f,r.wall,Metal);m.box(xx+.5f,.5f,.40f,.045f,2.2f,.08f,trim,Metal);}
            for(float zz:{1.0f,2.5f,4.0f})m.cylinder(4.3f,.22f,zz,.4f,.95f,C(.48f,.30f,.18f),Metal,16);
        }else if(r.seed%3==1){
            // Pump skid and two vessels, linked by pipework.
            m.box(.45f,.21f,.45f,5.1f,.18f,5.1f,concrete,Concrete);
            for(float xx:{1.55f,4.0f}){m.cylinder(xx,.4f,3.9f,.9f,2.3f,r.wall,Metal,24);m.ellipsoid(V(xx,2.7f,3.9f),V(.9f,.35f,.9f),r.wall,Metal,5,16);}
            m.box(1.45f,.6f,1.4f,2.7f,.22f,.22f,iron,Metal);
            for(float xx:{1.5f,4.0f})m.box(xx,.6f,1.4f,.18f,.18f,2.7f,iron,Metal);
            m.box(2.3f,.4f,.6f,1.2f,.65f,.7f,C(.29f,.37f,.36f),Metal);
        }else{
            // Pallet racks, timber packs and a lidded skip.
            for(float xx:{.6f,3.2f})for(float zz:{.6f,2.4f}){
                for(float yy:{.22f,.90f}){m.box(xx,yy,zz,1.8f,.15f,1.2f,iron,Metal);m.box(xx+.1f,yy+.15f,zz+.1f,1.6f,.50f,1.0f,C(.56f,.43f,.28f),Wood);}
                for(float dx:{0.0f,1.72f})m.box(xx+dx,.22f,zz,.08f,1.5f,1.2f,iron,Metal);
            }
            m.box(.6f,.25f,4.3f,3.0f,1.3f,1.2f,r.wall,Metal);m.box(.55f,1.55f,4.25f,3.1f,.1f,1.3f,iron,Metal);
        }
    } else if(f=="tree"){
        m.box(0,0.12f,0,W,0.08f,D,C(0.40f,0.48f,0.29f),Grass);m.tree(W/2,D/2,H,std::min(W,D)*0.43f);
    } else if(f=="park"){
        m.box(0,0.12f,0,W,0.08f,D,C(0.40f,0.48f,0.29f),Grass);
        m.box(W*0.46f,0.21f,0,1.5f,0.04f,D,trim,Gravel);
        for(float x:{W*0.22f,W*0.78f})for(float z:{D*0.25f,D*0.78f})m.tree(x,z,5.5f,std::min(W,D)*0.17f);
        m.bench(W*0.60f,D*0.50f);m.bench(W*0.13f,D*0.46f);
    } else if(f=="fence"){
        for(float x=0.05f;x<W;x+=1.65f)m.box(x,0.18f,D*0.4f,0.11f,H-0.18f,0.11f,C(0.47f,0.39f,0.29f),Wood);
        for(float y:{H*0.35f,H*0.80f})m.box(0.02f,y,D*0.4f,W-0.04f,0.11f,0.08f,C(0.48f,0.40f,0.30f),Wood);
    } else {
        int mat=Grass;C col(0.40f,0.48f,0.29f);
        if(f=="concrete"||f=="path"){mat=Concrete;col=concrete;}
        if(f=="parking"||f=="driveway"||f=="loading"){mat=Asphalt;col=C(0.31f,0.33f,0.33f);}
        if(f=="yard"||f=="props"){mat=Gravel;col=C(0.47f,0.44f,0.37f);}
        m.box(0,0.12f,0,W,0.07f,D,col,mat);
        if(f=="garden"){
            m.box(0.20f,0.2f,D-1.6f,1.7f,0.06f,1.2f,C(0.30f,0.26f,0.20f),Gravel);
            for(float x:{0.65f,1.40f})m.ellipsoid(V(x,0.58f,D-0.95f),V(0.40f,0.37f,0.42f),C(0.37f,0.45f,0.24f),Foliage,5,10);
        }
        if(f=="parking"||f=="loading"){
            for(float x=0.25f;x<W;x+=2.5f)m.box(x,0.195f,0.2f,0.09f,0.015f,std::min(5.0f,D-0.4f),trim);
            if(W>=5&&D>=5)m.car(0.65f,0.45f,r.wall);
        }
        if(f=="props"){
            size_t propsStart=m.vertices.size();
            m.box(0.4f,0.2f,0.5f,1.7f,1.25f,1.2f,C(0.52f,0.41f,0.27f),Wood);
            m.box(W-2.1f,0.2f,D-1.9f,1.7f,1.35f,1.45f,C(0.28f,0.35f,0.31f),Metal);
            for(float x:{0.9f,1.65f})m.cylinder(x,0.2f,D-1,0.31f,0.85f,r.wall,Metal,16);
            for(size_t i=propsStart;i<m.vertices.size();++i){auto& v=m.vertices[i];if(r.seed%2){v.x=W-v.x;v.z=D-v.z;v.normalX=-v.normalX;v.normalZ=-v.normalZ;}}

        }
    }
    for(auto& v:m.vertices){v.x/=W;v.y/=H;v.z/=D;}
    for(const auto& v:m.vertices)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||v.x<-.002f||v.z<-.002f||v.y<-.002f||v.x>1.002f||v.z>1.002f||v.y>1.002f)
        throw std::runtime_error("Model exceeds its declared metric bounds: "+r.id+". Check width, depth, height and floor count together.");
    return m;
}
inline void WriteMesh(std::ostream& out,const std::string& id,const Mesh& mesh){
    out<<"mesh "<<id<<"\n";
    for(const auto& v:mesh.vertices)out<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<' '<<v.colorR<<' '<<v.colorG<<' '<<v.colorB<<' '<<v.normalX<<' '<<v.normalY<<' '<<v.normalZ<<' '<<v.u<<' '<<v.v<<' '<<v.material<<' '<<v.ambient<<'\n';
    out<<"endmesh\n";
}
inline void GenerateAssetCatalog(const std::string& data){
    auto dir=data+"\\Generated";CreateDirectoryA(dir.c_str(),nullptr);
    auto target=dir+"\\module_meshes.txt",tmp=target+".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
    std::ofstream out(tmp);if(!out)throw std::runtime_error("Cannot write catalog: "+tmp);
    out<<std::setprecision(7)<<"CBGM 2\n";
    Mesh fallback;fallback.box(0,0,0,1,1,1,C(1,1,1));WriteMesh(out,"box",fallback);WriteMesh(out,"flat_surface",fallback);
    Mesh missing;missing.box(0,0,0,1,1,1,C(1,0,1));WriteMesh(out,"missing_mesh_placeholder",missing);
    Mesh accessPath;accessPath.box(0,0.12f,0,1,0.13f,1,concrete,Concrete);
    for(auto& v:accessPath.vertices)v.y/=0.30f;
    WriteMesh(out,"access_path",accessPath);
    Mesh accessDrive;accessDrive.box(0,0.12f,0,1,0.11f,1,C(0.31f,0.33f,0.33f),Asphalt);
    for(auto& v:accessDrive.vertices)v.y/=0.30f;
    WriteMesh(out,"access_drive",accessDrive);
    std::ofstream report(dir+"\\asset_report.csv");report<<"id,family,width_m,depth_m,height_m,floors,capacity,triangles,distant_triangles\n";
    for(const auto& path:XmlFiles(data+"\\Modules")){
        auto r=ReadRecipe(path);auto m=BuildRecipe(r);WriteMesh(out,"metric_"+r.id,m);
        auto distant=BuildRecipe(r,false);WriteMesh(out,"metric_"+r.id+"_distant",distant);
        for(const auto& v:ReadAssetVariations(XmlReadFileToString(path))){auto vr=VariantRecipe(r,v);auto vm=BuildRecipe(vr),vd=BuildRecipe(vr,false);WriteMesh(out,"metric_"+vr.id,vm);WriteMesh(out,"metric_"+vr.id+"_distant",vd);
            report<<vr.id<<','<<vr.family<<','<<vr.width<<','<<vr.depth<<','<<vr.height<<','<<vr.floors<<','<<vr.capacity<<','<<vm.vertices.size()/3<<','<<vd.vertices.size()/3<<'\n';}
        report<<r.id<<','<<r.family<<','<<r.width<<','<<r.depth<<','<<r.height<<','<<r.floors<<','<<r.capacity<<','<<m.vertices.size()/3<<','<<distant.vertices.size()/3<<'\n';
    }
    out.close();if(!out)throw std::runtime_error("Failed writing catalog");
    bool replaced=false;
    for(int attempt=0;attempt<50;++attempt){
        if(MoveFileExA(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){replaced=true;break;}
        DWORD error=GetLastError();if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION)break;
        Sleep(100);
    }
    if(!replaced)throw std::runtime_error("Cannot replace generated catalog (Windows error "+std::to_string(GetLastError())+").");
    auto pixels=BuildLotMaterialPixels();
    std::ofstream ppm(dir+"\\materials.ppm",std::ios::binary);ppm<<"P6\n"<<kLotTextureSize<<' '<<kLotTextureSize*MaterialCount<<"\n255\n";
    for(size_t i=0;i<pixels.size();i+=4)ppm.write(reinterpret_cast<const char*>(&pixels[i]),3);
}
}

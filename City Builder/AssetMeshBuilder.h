#pragma once
#include "GeneratedMeshCatalog.h"
#include "LotMaterials.h"
#include <cmath>
#include <algorithm>

namespace AssetArt {
struct V { float x,y,z; V(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
inline V operator+(V a,V b){return V(a.x+b.x,a.y+b.y,a.z+b.z);}
inline V operator-(V a,V b){return V(a.x-b.x,a.y-b.y,a.z-b.z);}
inline V operator*(V a,float s){return V(a.x*s,a.y*s,a.z*s);}
inline float length(V a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
inline V cross(V a,V b){return V(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
inline V unit(V a){float d=length(a);return d>0?a*(1/d):V(0,1,0);}
using C=V;
const C trim(0.76f,0.73f,0.66f), glass(0.21f,0.30f,0.35f), iron(0.17f,0.20f,0.21f), roof(0.29f,0.31f,0.32f), concrete(0.57f,0.56f,0.51f);
class Mesh {
public:
    bool detailed=true;
    std::vector<GeneratedMeshVertex> vertices;
    void tri(V a,V b,V c,C color,int mat=Plain,float ao=1) {
        V n=unit(cross(b-a,c-a));
        V p[3]={a,b,c};
        for(auto q:p){
            GeneratedMeshVertex v; v.x=q.x;v.y=q.y;v.z=q.z;
            v.colorR=color.x;v.colorG=color.y;v.colorB=color.z;
            v.normalX=n.x;v.normalY=n.y;v.normalZ=n.z;
            if(std::abs(n.y)>0.65f){v.u=q.x;v.v=q.z;}else if(std::abs(n.x)>0.65f){v.u=q.z;v.v=q.y;}else{v.u=q.x;v.v=q.y;}
            v.material=float(mat);v.ambient=ao;vertices.push_back(v);
        }
    }
    void quad(V a,V b,V c,V d,C col,int mat=Plain,float ao=1){tri(a,b,c,col,mat,ao);tri(c,d,a,col,mat,ao);}
    void box(float x,float y,float z,float w,float h,float d,C c,int m=Plain,float ao=1){
        if(w<=0||h<=0||d<=0)return;
        quad(V(x,y,z),V(x,y+h,z),V(x+w,y+h,z),V(x+w,y,z),c,m,ao);
        quad(V(x+w,y,z+d),V(x+w,y+h,z+d),V(x,y+h,z+d),V(x,y,z+d),c,m,ao);
        quad(V(x,y,z+d),V(x,y+h,z+d),V(x,y+h,z),V(x,y,z),c,m,ao);
        quad(V(x+w,y,z),V(x+w,y+h,z),V(x+w,y+h,z+d),V(x+w,y,z+d),c,m,ao);
        quad(V(x,y+h,z),V(x,y+h,z+d),V(x+w,y+h,z+d),V(x+w,y+h,z),c,m,ao);
        quad(V(x,y,z+d),V(x,y,z),V(x+w,y,z),V(x+w,y,z+d),c,m,ao);
    }
    void cylinder(float x,float y,float z,float r,float h,C c,int mat=Plain,int sides=24,float topRadius=-1){
        if(topRadius<0)topRadius=r;
        for(int i=0;i<sides;++i){float a=i*6.2831853f/sides,b=(i+1)*6.2831853f/sides;
            V p(x+r*std::cos(a),y,z+r*std::sin(a)),q(x+r*std::cos(b),y,z+r*std::sin(b));
            V t(x+topRadius*std::cos(a),y+h,z+topRadius*std::sin(a)),u(x+topRadius*std::cos(b),y+h,z+topRadius*std::sin(b));
            quad(p,t,u,q,c,mat);tri(V(x,y+h,z),u,t,c,mat);
        }
    }
    void ellipsoid(V center,V radius,C col,int mat=Foliage,int rings=7,int sides=12){
        auto pt=[&](float a,float b){return center+V(radius.x*std::sin(a)*std::cos(b),radius.y*std::cos(a),radius.z*std::sin(a)*std::sin(b));};
        for(int j=0;j<rings;++j)for(int i=0;i<sides;++i){float a=j*3.14159265f/rings,b=(j+1)*3.14159265f/rings,u=i*6.2831853f/sides,v=(i+1)*6.2831853f/sides;
            C c=col;
            size_t first=vertices.size();
            if(j<rings-1)tri(pt(a,u),pt(b,v),pt(b,u),c,mat);
            if(j>0)tri(pt(a,u),pt(a,v),pt(b,v),c,mat);
            for(size_t k=first;k<vertices.size();++k){auto& vert=vertices[k];
                V n=unit(V((vert.x-center.x)/(radius.x*radius.x),(vert.y-center.y)/(radius.y*radius.y),(vert.z-center.z)/(radius.z*radius.z)));
                vert.normalX=n.x;vert.normalY=n.y;vert.normalZ=n.z;
                vert.ambient=0.83f+0.17f*std::max(0.0f,(vert.y-center.y)/radius.y);
            }
        }
    }
    void gable(float x,float z,float w,float d,float eave,float peak,C wall,C tile){
        V a(x,eave,z),b(x+w,eave,z),c(x+w,eave,z+d),e(x,eave,z+d),r(x+w/2,peak,z),s(x+w/2,peak,z+d);
        tri(a,r,b,wall,Render);tri(c,s,e,wall,Render);
        quad(a,e,s,r,tile,Roof);quad(b,r,s,c,tile,Roof);
        box(x,eave-0.12f,z,w,0.16f,0.15f,trim);box(x,eave-0.12f,z+d-0.15f,w,0.16f,0.15f,trim);
        box(x,eave-0.12f,z,0.13f,0.16f,d,trim);box(x+w-0.13f,eave-0.12f,z,0.13f,0.16f,d,trim);
    }
    // A facade-local box; 'side' wraps the same metric joinery around all walls.
    void face(int side,float x,float z,float w,float d,float u,float y,float width,float height,float outset,float thick,C c,int mat=Plain){
        if(!detailed){
            auto p=[&](float a,float b){float o=outset+thick;
                if(side==0)return V(x+a,b,z-o);
                if(side==1)return V(x+w+o,b,z+a);
                if(side==2)return V(x+w-a,b,z+d+o);
                return V(x-o,b,z+d-a);
            };
            quad(p(u,y),p(u,y+height),p(u+width,y+height),p(u+width,y),c,mat);return;
        }
        if(side==0)box(x+u,y,z-outset-thick,width,height,thick,c,mat);
        if(side==1)box(x+w+outset,y,z+u,thick,height,width,c,mat);
        if(side==2)box(x+w-u-width,y,z+d+outset,width,height,thick,c,mat);
        if(side==3)box(x-outset-thick,y,z+d-u-width,thick,height,width,c,mat);
    }
    void window(int side,float x,float z,float w,float d,float u,float y,float ww=1.25f,float wh=1.45f){
        if(!detailed){face(side,x,z,w,d,u,y,ww,wh,0.07f,0.025f,glass,Glass);return;}
        face(side,x,z,w,d,u-0.09f,y-0.09f,ww+0.18f,wh+0.18f,0.015f,0.05f,iron);
        face(side,x,z,w,d,u,y,ww,wh,0.07f,0.025f,glass,Glass);
        face(side,x,z,w,d,u-0.10f,y-0.16f,ww+0.20f,0.13f,0.035f,0.19f,trim);
        face(side,x,z,w,d,u,y+wh*0.51f,ww,0.055f,0.098f,0.035f,trim);
        face(side,x,z,w,d,u+ww*0.48f,y,0.055f,wh,0.098f,0.035f,trim);
    }
    void door(int side,float x,float z,float w,float d,float u,float y=0.32f,float ww=1.05f){
        face(side,x,z,w,d,u-0.1f,y,ww+0.2f,2.23f,0.025f,0.09f,trim);
        face(side,x,z,w,d,u,y,ww,2.12f,0.12f,0.035f,C(0.24f,0.29f,0.28f),Wood);
        face(side,x,z,w,d,u+0.15f,y+1.18f,ww-0.3f,0.62f,0.16f,0.02f,glass,Glass);
        face(side,x,z,w,d,u+ww-0.18f,y+0.93f,0.055f,0.14f,0.19f,0.05f,trim);
    }
    void tree(float x,float z,float h=7,float radius=2.25f){
        cylinder(x,0.12f,z,0.18f,h*0.68f,C(0.30f,0.25f,0.18f),Wood,10,0.09f);
        ellipsoid(V(x,h*0.79f,z),V(radius*0.70f,h*0.20f,radius*0.67f),C(0.37f,0.46f,0.25f),Foliage,detailed?7:3,detailed?14:6);
        for(int i=0;i<5;++i){float a=i*1.2566f;
            ellipsoid(V(x+std::cos(a)*radius*0.40f,h*(0.61f+0.03f*(i%3)),z+std::sin(a)*radius*0.40f),V(radius*0.59f,h*0.21f,radius*0.61f),C(0.33f+0.025f*(i%3),0.43f+0.015f*(i%2),0.23f),Foliage,detailed?7:3,detailed?14:6);
        }
    }
    void bench(float x,float z){
        box(x,0.42f,z,1.75f,0.10f,0.45f,C(0.48f,0.37f,0.24f),Wood);
        box(x,0.50f,z+0.40f,1.75f,0.38f,0.07f,C(0.48f,0.37f,0.24f),Wood);
        for(float dx:{0.18f,1.45f})box(x+dx,0.05f,z+0.12f,0.08f,0.40f,0.32f,iron);
    }
    void car(float x,float z,C c){
        box(x,0.42f,z,1.78f,0.65f,4.25f,c,Metal);
        box(x+0.13f,1.07f,z+1.05f,1.52f,0.53f,2.0f,glass,Glass);
        box(x+0.12f,1.60f,z+1.18f,1.54f,0.08f,1.70f,c,Metal);
        for(float dx:{-0.05f,1.64f})for(float dz:{0.65f,3.0f})box(x+dx,0.23f,z+dz,0.20f,0.50f,0.58f,C(0.09f,0.10f,0.10f));
        box(x+0.15f,0.8f,z-0.02f,0.35f,0.13f,0.04f,trim);box(x+1.25f,0.8f,z-0.02f,0.35f,0.13f,0.04f,trim);
    }
};
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <GL/glew.h>
#include "AssetPipeline.h"
#include "AssetEditorIO.h"
#include "LotMaterialShader.h"
#include "SimulationRuntime.h"
#include "RuntimePaths.h"
#include "ShaderProgram.h"
#include <filesystem>
#include <memory>
#include <regex>
#include <iostream>
#include <array>
#include <cstring>

namespace fs=std::filesystem;
using namespace AssetArt;
namespace {
HWND windowHandle,viewHandle,listHandle,modeHandle,statsHandle,statusHandle,xmlHandle,filterHandle;
HWND widthHandle,depthHandle,moduleHandle,fields[10];
HDC viewDc;HGLRC context;GLuint program,vao,vbo,texture;
HFONT uiFont;
std::string dataRoot,currentPath,currentXml;
std::unique_ptr<SimulationRuntime> runtime;
GeneratedMeshCatalog catalog;
std::vector<std::string> entries,visibleEntries,lotModuleIds;
std::vector<GeneratedMeshVertex> scene;
int assetIndex=0,rotation=0,variation=1,parcelW=4,parcelD=4,triangles=0;
bool distantPreview=false,moduleMode=false,wireframe=false,showScale=true,loadingUi=false,dirty=false;
float yaw=-0.72f,pitch=0.68f,zoom=1.0f,sceneSpan=10;
V sceneCenter;
POINT dragStart;bool dragging=false;
const char* fieldLabels[]={"Width (m)","Depth (m)","Height (m)","Floors","Capacity","Red (0-1)","Green (0-1)","Blue (0-1)","Pollution","Land value"};
const char* fieldAttrs[]={"widthMeters","depthMeters","heightMeters","floors","amount","colorR","colorG","colorB","airPollution","landValue"};

std::string Text(HWND h){int n=GetWindowTextLengthA(h);std::string s(n+1,'\0');GetWindowTextA(h,s.data(),n+1);s.resize(n);return s;}
void Status(const std::string& s){SetWindowTextA(statusHandle,s.c_str());}
void WriteText(const fs::path& path,const std::string& s){std::ofstream f(path,std::ios::binary);f<<s;if(!f)throw std::runtime_error("Cannot write "+path.string());}
void SetAttribute(std::string& xml,const std::string& tagName,const std::string& name,const std::string& value){
    auto tag=Tag(xml,tagName);if(tag.empty())throw std::runtime_error("Missing <"+tagName+">.");
    auto p=xml.find(tag),at=SimpleXmlDetail::FindAttributeValueStart(tag,name);
    if(at==std::string::npos){auto end=tag.find('/');if(end==std::string::npos)end=tag.size()-1;tag.insert(end," "+name+"=\""+value+"\"");}
    else tag.replace(at,tag.find('"',at)-at,value);
    auto old=Tag(xml,tagName);xml.replace(p,old.size(),tag);
}
float Number(HWND h,float min,float max){auto t=Text(h);size_t pos=0;float v=std::stof(t,&pos);if(pos!=t.size()||!std::isfinite(v)||v<min||v>max)throw std::runtime_error("Invalid value: "+t);return v;}
std::string Format(float v){std::ostringstream s;s<<std::fixed<<std::setprecision(2)<<v;return s.str();}
void LoadRuntime(){
    RuntimeOptions o;o.mapWidth=o.mapHeight=40;o.detectL2CacheSize=false;o.showNonFatalAssetWarningDialogs=false;o.assetDataDirectory=dataRoot;
    runtime=std::make_unique<SimulationRuntime>(o);
    std::string error;if(!catalog.loadFromFile(dataRoot+"\\Generated\\module_meshes.txt",error))throw std::runtime_error(error);
}
const LotModule* FindModule(const std::string& id){for(const auto& m:runtime->assetModules())if(m.id==id)return &m;return nullptr;}
void AppendMesh(const GeneratedMeshRange& range,const LotRenderInstance& inst){
    float w=inst.renderWidth,d=inst.renderHeightOverride,h=inst.renderHeight;
    for(int i=range.firstVertex;i<range.firstVertex+range.vertexCount;++i){
        auto v=catalog.vertices()[i];float x=v.x,z=v.z,nx=v.normalX,nz=v.normalZ;
        if(inst.meshRotation==1){x=1-v.z;z=v.x;v.normalX=-nz;v.normalZ=nx;}
        if(inst.meshRotation==2){x=1-v.x;z=1-v.z;v.normalX=-nx;v.normalZ=-nz;}
        if(inst.meshRotation==3){x=v.z;z=1-v.x;v.normalX=nz;v.normalZ=-nx;}
        v.x=inst.originX+inst.renderOffsetX+x*w;v.y*=h;v.z=inst.originY+inst.renderOffsetY+z*d;
        v.colorR*=inst.colorR;v.colorG*=inst.colorG;v.colorB*=inst.colorB;scene.push_back(v);
    }
}
void AddContext(float minX,float minZ,float maxX,float maxZ){
    Mesh m;float W=(maxX-minX)*6,D=(maxZ-minZ)*6;if(!moduleMode&&(rotation&1))std::swap(W,D);
    m.box(-4,-0.13f,-13,W+8,0.13f,D+17,C(0.50f,0.55f,0.43f),Grass);
    if(showScale){
        m.box(-4,0.01f,-12,W+8,0.08f,12,C(0.29f,0.31f,0.31f),Asphalt);
        m.box(-4,0.1f,-1.8f,W+8,0.16f,1.8f,concrete,Concrete);
        m.box(-4,0.1f,-12,W+8,0.16f,1.8f,concrete,Concrete);
        for(float x=-3;x<W+3;x+=6)m.box(x,0.105f,-6.06f,3,0.015f,0.12f,C(0.78f,0.67f,0.33f));
        size_t carStart=m.vertices.size();m.car(1,-5.0f,C(0.55f,0.24f,0.19f));
        for(size_t i=carStart;i<m.vertices.size();++i){auto& v=m.vertices[i];float x=v.x-1,z=v.z+5,nx=v.normalX,nz=v.normalZ;v.x=1+z;v.z=-3-x;v.normalX=nz;v.normalZ=-nx;}
        // 1.75m pedestrian beside a 2m survey pole; all geometry uses metres.
        float px=W*0.7f;
        m.cylinder(px,0.15f,-1,0.14f,1.10f,C(0.28f,0.36f,0.46f),Plain,10);
        m.ellipsoid(V(px,1.53f,-1),V(0.13f,0.17f,0.13f),C(0.68f,0.51f,0.37f),Plain,5,8);
        for(float dx:{-0.1f,0.07f})m.box(px+dx,0.15f,-1,0.08f,0.60f,0.1f,iron);
        m.box(W+1,0,-0.5f,0.10f,2,0.10f,trim);
        for(float y=0;y<2;y+=0.5f)m.box(W+0.97f,y,-0.53f,0.16f,0.12f,0.16f,C(0.65f,0.29f,0.20f));
    }
    for(auto v:m.vertices){
        float x=v.x/6,z=v.z/6,nx=v.normalX,nz=v.normalZ;
        int rot=moduleMode?0:rotation;
        if(rot==1){v.x=maxX-z;v.z=minZ+x;v.normalX=-nz;v.normalZ=nx;}
        else if(rot==2){v.x=maxX-x;v.z=maxZ-z;v.normalX=-nx;v.normalZ=-nz;}
        else if(rot==3){v.x=minX+z;v.z=maxZ-x;v.normalX=nz;v.normalZ=-nx;}
        else {v.x=minX+x;v.z=minZ+z;}
        v.y/=6;scene.push_back(v);
    }
}
void UpdateEditor(const std::string& path){
    loadingUi=true;currentPath=path;currentXml=XmlReadFileToString(path);SetWindowTextA(xmlHandle,currentXml.c_str());
    bool isModule=path.find("Modules")!=std::string::npos;
    for(int i=0;i<10;++i){auto t=Tag(currentXml,i==4?"driver":(i>=8?"effects":"render"));auto v=XmlAttributeValue(t,fieldAttrs[i],"0");SetWindowTextA(fields[i],v.c_str());EnableWindow(fields[i],isModule && (i!=4||!Tag(currentXml,"driver").empty()));}
    dirty=false;loadingUi=false;
}
void BuildScene(bool resetCamera=false){
    scene.clear();triangles=0;InvalidateRect(viewHandle,nullptr,FALSE);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,0,nullptr,GL_STATIC_DRAW);SetWindowTextA(statsHandle,"No preview");lotModuleIds.clear();SendMessageA(moduleHandle,CB_RESETCONTENT,0,0);
    if(visibleEntries.empty())return;
    auto id=visibleEntries[assetIndex];std::vector<LotRenderInstance> instances;std::ostringstream stats;
    std::string editPath;float minX=0,minZ=0,maxX=1,maxZ=1,maxY=0;
    if(moduleMode){
        auto mod=FindModule(id);if(!mod)throw std::runtime_error("Unknown module "+id);
        Lot lot(variation,id,0,0);lot.addModule(*mod,Int2(),40);lot.buildRenderInstances(instances);maxX=float(mod->width);maxZ=float(mod->height);
        editPath=dataRoot+"\\Modules\\"+id+".xml";
        auto r=ReadRecipe(editPath);stats<<id<<"\r\n"<<r.family<<"  |  "<<r.floors<<" floors  |  capacity "<<r.capacity<<"\r\n"<<Format(r.width)<<" x "<<Format(r.depth)<<" x "<<Format(r.height)<<" m";
    }else{
        Lot lot;if(!runtime->buildAssetPreview(id,parcelW,parcelD,rotation,variation,lot)){
            Status("This parcel size does not fit this lot. Change width/depth (1-8 tiles).");UpdateEditor(dataRoot+"\\Lots\\"+id+".xml");return;
        }
        lot.buildRenderInstances(instances);CityParameterRegistry registry;minX=float(lot.minimumTileX());minZ=float(lot.minimumTileY());maxX=minX+lot.footprintWidth();maxZ=minZ+lot.footprintHeight();
        stats<<id<<"\r\n"<<lot.footprintWidth()<<" x "<<lot.footprintHeight()<<" tiles = "<<lot.footprintWidth()*6<<" x "<<lot.footprintHeight()*6<<" m\r\n"<<lot.parameterSummary(registry);
        for(const auto& p:lot.modules())if(std::find(lotModuleIds.begin(),lotModuleIds.end(),p.module->id)==lotModuleIds.end()){
            lotModuleIds.push_back(p.module->id);SendMessageA(moduleHandle,CB_ADDSTRING,0,(LPARAM)p.module->id.c_str());
        }
        editPath=dataRoot+"\\Lots\\"+id+".xml";
    }
    for(const auto& i:instances){
        const GeneratedMeshRange* mesh=nullptr;for(const auto& b:runtime->assetMeshBindings())if(b.handle==i.renderMeshHandle){mesh=distantPreview?catalog.findMesh(b.key+"_distant"):nullptr;if(!mesh)mesh=catalog.findMesh(b.key);break;}
        if(!mesh)throw std::runtime_error("Missing mesh binding in "+id);AppendMesh(*mesh,i);
    }
    for(const auto& v:scene)maxY=std::max(maxY,v.y);
    triangles=int(scene.size()/3);stats<<"\r\n"<<triangles<<" triangles  |  rotation "<<rotation*90<<" deg";
    AddContext(minX,minZ,maxX,maxZ);
    sceneCenter=V((minX+maxX)/2,maxY*0.35f,(minZ+maxZ)/2-0.65f);
    sceneSpan=std::max({maxX-minX+3,maxZ-minZ+3,maxY*1.35f});
    if(resetCamera)zoom=1;
    glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,scene.size()*sizeof(GeneratedMeshVertex),scene.data(),GL_STATIC_DRAW);
    SetWindowTextA(statsHandle,stats.str().c_str());UpdateEditor(editPath);Status("6 m / tile   |   Drag to orbit, wheel to zoom   |   Front is the road side");
}
void FillList(){
    loadingUi=true;entries.clear();visibleEntries.clear();SendMessageA(listHandle,LB_RESETCONTENT,0,0);
    if(moduleMode){for(const auto& m:runtime->assetModules())entries.push_back(m.id);}else{for(const auto& l:runtime->assetLots())entries.push_back(l.id);}
    auto search=Text(filterHandle);for(const auto& id:entries)if(id.find(search)!=std::string::npos){visibleEntries.push_back(id);SendMessageA(listHandle,LB_ADDSTRING,0,(LPARAM)id.c_str());}
    assetIndex=0;SendMessageA(listHandle,LB_SETCURSEL,0,0);loadingUi=false;BuildScene(true);
}
std::string FieldXml(){
    auto xml=currentXml;
    for(int i=0;i<10;++i)if(IsWindowEnabled(fields[i])){
        float v=Number(fields[i],i<3?0.05f:0.0f,i>=5&&i<=7?1.0f:100000.0f);
        if((i==3||i==4||i>=8)&&std::floor(v)!=v)throw std::runtime_error("Floors, capacity and effects must be whole numbers.");
        SetAttribute(xml,i==4?"driver":(i>=8?"effects":"render"),fieldAttrs[i],Text(fields[i]));
    }
    SetAttribute(xml,"render","height",Format(Number(fields[2],0.05f,300)/6));
    return xml;
}
void SaveXml(const std::string& xml){
    AssetEditorIO::Save(dataRoot,currentPath,currentXml,xml);
    LoadRuntime();BuildScene();Status("Saved and regenerated source assets. Use Deploy to game, or rebuild the game.");
}
void Deploy(){
    if(dirty)throw std::runtime_error("Save or reload the current edit before deploying.");
    GenerateAssetCatalog(dataRoot);
    auto dst=RuntimeDataDirectory();if(fs::equivalent(fs::path(dst),fs::path(dataRoot))){Status("Already editing deployed game Data.");return;}
    for(const auto& folder:{"Modules","Lots"})fs::copy(fs::path(dataRoot)/folder,fs::path(dst)/folder,fs::copy_options::recursive|fs::copy_options::overwrite_existing);
    GenerateAssetCatalog(dst);Status("Deployed models and XML beside City Builder.exe. Restart the game to load them.");
}
std::array<float,16> Matrix(){
    V forward=unit(V(std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)));
    V right=unit(cross(forward,V(0,1,0))),up=cross(right,forward);
    RECT r;GetClientRect(viewHandle,&r);float aspect=float(std::max(1L,r.right))/std::max(1L,r.bottom),span=sceneSpan*0.67f/zoom;
    float sx=1/(span*aspect),sy=1/span,sz=1/(sceneSpan*8);
    auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    return {right.x*sx,up.x*sy,forward.x*sz,0,right.y*sx,up.y*sy,forward.y*sz,0,right.z*sx,up.z*sy,forward.z*sz,0,-dot(right,sceneCenter)*sx,-dot(up,sceneCenter)*sy,-dot(forward,sceneCenter)*sz,1};
}
void RenderFrame(){
    if(!context)return;wglMakeCurrent(viewDc,context);RECT r;GetClientRect(viewHandle,&r);
    glViewport(0,0,r.right,r.bottom);glClearColor(0.69f,0.73f,0.73f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glUseProgram(program);auto matrix=Matrix();glUniformMatrix4fv(glGetUniformLocation(program,"uMatrix"),1,GL_FALSE,matrix.data());
    glBindVertexArray(vao);glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    glUniform1i(glGetUniformLocation(program,"uShadow"),0);glPolygonMode(GL_FRONT_AND_BACK,wireframe?GL_LINE:GL_FILL);
    glDrawArrays(GL_TRIANGLES,0,GLsizei(scene.size()));glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    // Single-coverage planar contact shadows on the lot ground.
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_STENCIL_TEST);glStencilFunc(GL_EQUAL,0,0xff);glStencilOp(GL_KEEP,GL_KEEP,GL_INCR);
    glUniform1i(glGetUniformLocation(program,"uShadow"),1);glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES,0,triangles*3);glDepthMask(GL_TRUE);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);
    glFinish();SwapBuffers(viewDc);
}
void Capture(const std::string& path){
    if(scene.empty())throw std::runtime_error("No valid preview to export.");
    RenderFrame();RECT r;GetClientRect(viewHandle,&r);int w=r.right,h=r.bottom;
    std::vector<unsigned char> pixels(w*h*4);glReadBuffer(GL_FRONT);glReadPixels(0,0,w,h,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
    BITMAPFILEHEADER file={};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+DWORD(pixels.size());
    BITMAPINFOHEADER info={};info.biSize=sizeof(info);info.biWidth=w;info.biHeight=h;info.biPlanes=1;info.biBitCount=32;
    std::ofstream f(path,std::ios::binary);f.write((char*)&file,sizeof(file));f.write((char*)&info,sizeof(info));f.write((char*)pixels.data(),pixels.size());if(!f)throw std::runtime_error("Cannot export preview.");
}
GLuint Compile(GLenum kind,const std::string& source){GLuint s=glCreateShader(kind);const char* p=source.c_str();glShaderSource(s,1,&p,nullptr);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192];glGetShaderInfoLog(s,sizeof(log),nullptr,log);throw std::runtime_error(log);}return s;}
void InitGl(){
    viewDc=GetDC(viewHandle);PIXELFORMATDESCRIPTOR p={};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cDepthBits=24;p.cStencilBits=8;
    int format=ChoosePixelFormat(viewDc,&p);if(!format||!SetPixelFormat(viewDc,format,&p))throw std::runtime_error("OpenGL pixel format failed.");
    context=wglCreateContext(viewDc);if(!context||!wglMakeCurrent(viewDc,context)||glewInit()!=GLEW_OK)throw std::runtime_error("OpenGL initialization failed.");
    // Exercise the exact production shader in the same GPU context during review.
    {ShaderProgram gameShader;if(!gameShader.loadFromFile(RuntimeExecutableDirectory()+"\\Basic.shader"))throw std::runtime_error("Production game shader did not link.");}
    std::string vert=R"GLSL(#version 330 core
layout(location=0) in vec3 p;layout(location=1) in vec3 c;layout(location=2) in vec3 n;layout(location=3) in vec4 s;
uniform mat4 uMatrix;uniform int uShadow;out vec3 col;out vec3 normal;out vec4 surface;
void main(){vec3 q=p;if(uShadow==1){q.xz+=q.y*vec2(0.55,0.35)/0.82;q.y=0.036;}gl_Position=uMatrix*vec4(q,1);col=c;normal=n;surface=s;}
)GLSL";
    std::string frag=std::string("#version 330 core\n")+LotMaterialShaderSource()+R"GLSL(
in vec3 col;in vec3 normal;in vec4 surface;uniform int uShadow;out vec4 outColor;
void main(){if(uShadow==1){outColor=vec4(0.12,0.15,0.15,0.24);return;}outColor=vec4(shadeLotMaterial(col,normal,surface.xy,surface.z,surface.w),1);}
)GLSL";
    GLuint vs=Compile(GL_VERTEX_SHADER,vert),fs=Compile(GL_FRAGMENT_SHADER,frag);program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok)throw std::runtime_error("Asset preview shader link failed.");
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    for(int i=0;i<4;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==3?4:3,GL_FLOAT,GL_FALSE,sizeof(GeneratedMeshVertex),(void*)(size_t(i*3*sizeof(float))));}
    auto pixels=BuildLotMaterialPixels();glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,texture);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,kLotTextureSize,kLotTextureSize,MaterialCount,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"uLotMaterials"),0);
}
HWND Control(const char* cls,const char* text,int style,int x,int y,int w,int h,int id){
    HWND c=CreateWindowExA(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,windowHandle,(HMENU)(INT_PTR)id,GetModuleHandle(nullptr),nullptr);SendMessageA(c,WM_SETFONT,(WPARAM)uiFont,TRUE);return c;
}
void Layout(){RECT r;GetClientRect(windowHandle,&r);int W=r.right,H=r.bottom;MoveWindow(viewHandle,340,12,W-352,std::max(240,H-222),TRUE);MoveWindow(statsHandle,350,H-202,W-370,106,TRUE);MoveWindow(statusHandle,350,H-87,W-370,70,TRUE);MoveWindow(xmlHandle,12,675,312,std::max(60,H-688),TRUE);}
bool CanDiscard(){if(!dirty)return true;return MessageBoxA(windowHandle,"Discard the unsaved edit?","Asset Manager",MB_OKCANCEL|MB_ICONQUESTION)==IDOK;}
LRESULT CALLBACK ViewProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_LBUTTONDOWN){dragging=true;dragStart={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(h);return 0;}
    if(msg==WM_LBUTTONUP){dragging=false;ReleaseCapture();return 0;}
    if(msg==WM_MOUSEMOVE&&dragging){POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};yaw+=(p.x-dragStart.x)*0.008f;pitch=std::clamp(pitch+(p.y-dragStart.y)*0.006f,0.12f,1.50f);dragStart=p;RenderFrame();return 0;}
    if(msg==WM_MOUSEWHEEL){zoom=std::clamp(zoom*(GET_WHEEL_DELTA_WPARAM(w)>0?1.12f:0.89f),0.25f,4.0f);RenderFrame();return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);RenderFrame();return 0;}
    return DefWindowProcA(h,msg,w,l);
}
LRESULT CALLBACK WindowProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    try{
        if(msg==WM_SIZE){if(viewHandle)Layout();return 0;}
        if(msg==WM_CLOSE){if(CanDiscard())PostQuitMessage(0);return 0;}
        if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
        if(msg==WM_COMMAND&&!loadingUi){int id=LOWORD(w),event=HIWORD(w);
            if(id==10&&event==LBN_SELCHANGE){if(CanDiscard()){assetIndex=int(SendMessageA(listHandle,LB_GETCURSEL,0,0));BuildScene(true);}return 0;}
            if(id==11&&event==CBN_SELCHANGE){if(CanDiscard()){moduleMode=SendMessageA(modeHandle,CB_GETCURSEL,0,0)==1;FillList();}return 0;}
            if(id==12&&event==EN_CHANGE){if(!dirty)FillList();return 0;}
            if(id==13&&event==CBN_SELCHANGE){if(CanDiscard()){int i=int(SendMessageA(moduleHandle,CB_GETCURSEL,0,0));if(i>=0)UpdateEditor(dataRoot+"\\Modules\\"+lotModuleIds[i]+".xml");}return 0;}
            if(((id>=100&&id<110)||id==40)&&event==EN_CHANGE){dirty=true;Status("Unsaved edit. Save fields or Save XML to validate and regenerate.");return 0;}
            if(event==BN_CLICKED){
                if(id==20&&CanDiscard()){parcelW=int(Number(widthHandle,1,8));parcelD=int(Number(depthHandle,1,8));BuildScene(true);}
                if(id==21&&CanDiscard()){rotation=(rotation+1)%4;BuildScene();}
                if(id==22&&CanDiscard()){++variation;BuildScene();}
                if(id==23){wireframe=!wireframe;RenderFrame();}
                if(id==24&&CanDiscard()){showScale=!showScale;BuildScene();}
                if(id==26&&CanDiscard()){distantPreview=!distantPreview;BuildScene();}
                if(id==25){yaw=-0.72f;pitch=.68f;zoom=1;RenderFrame();}
                if(id==30){if(currentPath.find("Modules")!=std::string::npos)SaveXml(FieldXml());else Status("Select a module in 'Edit module in lot' to edit metric fields.");}
                if(id==31)SaveXml(Text(xmlHandle));
                if(id==32)Deploy();
                if(id==33){fs::create_directories(fs::path(dataRoot)/"Generated"/"Previews");auto path=dataRoot+"\\Generated\\Previews\\"+visibleEntries[assetIndex]+".bmp";Capture(path);Status("Exported: "+path);}
                if(id==34&&CanDiscard()){LoadRuntime();BuildScene();}
            }
        }
    }catch(const std::exception& e){Status(e.what());MessageBoxA(h,e.what(),"Asset validation",MB_OK|MB_ICONERROR);}
    return DefWindowProcA(h,msg,w,l);
}
void CreateUi(){
    WNDCLASSA wc={};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="CityBuilderAssetManager";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassA(&wc);
    wc.lpfnWndProc=ViewProc;wc.lpszClassName="CityBuilderAssetViewport";wc.style=CS_OWNDC;wc.hbrBackground=nullptr;RegisterClassA(&wc);
    uiFont=CreateFontA(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    windowHandle=CreateWindowA("CityBuilderAssetManager","City Builder | Asset Manager - 6 metres per tile",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,1000,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    loadingUi=true;
    Control("STATIC","ASSET LIBRARY",0,12,12,280,22,0);
    modeHandle=Control("COMBOBOX","",CBS_DROPDOWNLIST|WS_VSCROLL,12,40,130,140,11);
    SendMessageA(modeHandle,CB_ADDSTRING,0,(LPARAM)"Complete lots");SendMessageA(modeHandle,CB_ADDSTRING,0,(LPARAM)"Modules");SendMessageA(modeHandle,CB_SETCURSEL,0,0);
    filterHandle=Control("EDIT","",WS_BORDER|ES_AUTOHSCROLL,150,40,174,24,12);
    listHandle=Control("LISTBOX","",WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,12,72,312,174,10);SendMessageA(listHandle,LB_SETHORIZONTALEXTENT,420,0);
    Control("STATIC","Parcel tiles  W / D",0,12,256,130,22,0);
    widthHandle=Control("EDIT","4",WS_BORDER|ES_NUMBER,145,252,42,26,14);depthHandle=Control("EDIT","4",WS_BORDER|ES_NUMBER,193,252,42,26,15);Control("BUTTON","Fit lot",0,242,252,82,28,20);
    Control("BUTTON","Rotate lot",0,12,286,100,28,21);Control("BUTTON","Variation",0,118,286,100,28,22);Control("BUTTON","Wireframe",0,224,286,100,28,23);
    Control("BUTTON","Scale props",0,12,320,100,28,24);Control("BUTTON","Isometric",0,118,320,100,28,25);Control("BUTTON","Export BMP",0,224,320,100,28,33);
    Control("STATIC","Edit module in lot",0,12,357,190,20,0);Control("BUTTON","Distant LOD",BS_AUTOCHECKBOX,212,351,112,24,26);moduleHandle=Control("COMBOBOX","",CBS_DROPDOWNLIST|WS_VSCROLL,12,380,312,280,13);
    for(int i=0;i<10;++i){int col=i%2,row=i/2;Control("STATIC",fieldLabels[i],0,12+col*160,418+row*34,94,22,0);fields[i]=Control("EDIT","0",WS_BORDER|ES_AUTOHSCROLL,106+col*160,415+row*34,58,26,100+i);}
    Control("BUTTON","Save fields",0,12,590,100,28,30);Control("BUTTON","Reload",0,118,590,100,28,34);Control("BUTTON","Deploy",0,224,590,100,28,32);
    Control("STATIC","Lot / module XML (advanced)",0,12,637,212,22,0);Control("BUTTON","Save XML",0,235,633,90,28,31);
    xmlHandle=Control("EDIT","",WS_BORDER|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL,12,675,312,240,40);SendMessageA(xmlHandle,EM_SETLIMITTEXT,1024*1024,0);
    viewHandle=Control("CityBuilderAssetViewport","",WS_CLIPSIBLINGS|WS_CLIPCHILDREN,340,12,1000,720,50);
    statsHandle=Control("STATIC","",0,350,760,1000,100,51);statusHandle=Control("STATIC","",0,350,870,1000,65,52);
    loadingUi=false;Layout();InitGl();ShowWindow(windowHandle,SW_SHOW);UpdateWindow(windowHandle);
}

void ValidateCatalog(){
    int previews=0;
    for(const auto& module:runtime->assetModules()){
        if(!catalog.findMesh(module.renderMeshKey)||!catalog.findMesh(module.renderMeshKey+"_distant"))throw std::runtime_error("Missing detail level: "+module.id);
        auto recipe=ReadRecipe(dataRoot+"\\Modules\\"+module.id+".xml");BuildRecipe(recipe);BuildRecipe(recipe,false);
    }
    for(const auto& asset:runtime->assetLots())for(int w=1;w<=8;++w)for(int d=1;d<=8;++d)for(int rot=0;rot<4;++rot){
        Lot lot;if(!runtime->buildAssetPreview(asset.id,w,d,rot,11,lot))continue;
        std::vector<LotRenderInstance> instances;lot.buildRenderInstances(instances);
        for(const auto& i:instances)if(!std::isfinite(i.renderWidth)||!std::isfinite(i.renderHeightOverride)||i.renderWidth<=0||i.renderHeightOverride<=0)throw std::runtime_error("Invalid lot geometry: "+asset.id);
        ++previews;
    }
    std::cout<<"Validated "<<runtime->assetModules().size()<<" metric modules, both detail levels, and "<<previews<<" fitted parcel/rotation combinations.\n";
}
void EditorSelfTest(const std::string& directory){
    fs::path root=fs::absolute(directory)/("EditorTest-"+std::to_string(GetTickCount64()));fs::create_directories(root);
    for(const auto& entry:fs::directory_iterator(dataRoot))if(entry.is_directory()&&entry.path().filename()!="Generated"&&entry.path().filename()!="RetiredAssets")fs::copy(entry.path(),root/entry.path().filename(),fs::copy_options::recursive);
    auto source=root/"Modules"/"house_module.xml";auto before=XmlReadFileToString(source.string()),edited=before;
    SetAttribute(edited,"render","colorR","0.61");AssetEditorIO::Save(root,source,before,edited);
    if(XmlReadFileToString(source.string())!=edited||XmlReadFileToString(source.string()+".bak")!=before)throw std::runtime_error("Editor save/backup failed.");
    auto bad=edited;SetAttribute(bad,"render","heightMeters","0.01");bool rejected=false;
    try{AssetEditorIO::Save(root,source,edited,bad);}catch(const std::exception&){rejected=true;}
    if(!rejected||XmlReadFileToString(source.string())!=edited)throw std::runtime_error("Invalid edit was not isolated.");
    rejected=false;try{AssetEditorIO::Save(root,source,before,edited);}catch(const std::exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("Concurrent edit was not detected.");
    bad=edited;SetAttribute(bad,"module","id","renamed_house");rejected=false;
    try{AssetEditorIO::Save(root,source,edited,bad);}catch(const std::exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("Stable asset ID was not protected.");
    GeneratedMeshCatalog verify;std::string error;if(!verify.loadFromFile((root/"Generated"/"module_meshes.txt").string(),error))throw std::runtime_error(error);
    std::cout<<"Editor save, backup, invalid-edit isolation, concurrent edit detection and stable ID tests passed. Fixture: "<<root.string()<<'\n';
}

void CaptureWorkspace(const std::string& path){
    // Capture this application's own client area, then overlay its OpenGL child.
    RECT r;GetClientRect(windowHandle,&r);int w=r.right,h=r.bottom;
    BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=GetDC(windowHandle),mem=CreateCompatibleDC(dc);void* raw=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);auto old=SelectObject(mem,bitmap);
    PrintWindow(windowHandle,mem,PW_CLIENTONLY);GdiFlush();RenderFrame();
    RECT vr;GetClientRect(viewHandle,&vr);POINT offset={0,0};MapWindowPoints(viewHandle,windowHandle,&offset,1);
    std::vector<unsigned char> pixels(vr.right*vr.bottom*4);glReadBuffer(GL_FRONT);glReadPixels(0,0,vr.right,vr.bottom,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<vr.bottom;++y)std::memcpy(static_cast<unsigned char*>(raw)+((offset.y+y)*w+offset.x)*4,pixels.data()+(vr.bottom-1-y)*vr.right*4,vr.right*4);
    BITMAPFILEHEADER file={};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+w*h*4;
    std::ofstream out(path,std::ios::binary);out.write((char*)&file,sizeof(file));out.write((char*)&info.bmiHeader,sizeof(BITMAPINFOHEADER));out.write((char*)raw,w*h*4);
    SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(windowHandle,dc);
}
void CaptureCatalog(const std::string& directory){
    fs::create_directories(directory);std::ofstream report(fs::path(directory)/"review.csv");report<<"asset,view,triangles\n";
    moduleMode=true;SetWindowTextA(filterHandle,"");FillList();
    for(size_t i=0;i<visibleEntries.size();++i){assetIndex=int(i);BuildScene(true);yaw=-0.72f;Capture((fs::path(directory)/(visibleEntries[i]+".bmp")).string());report<<visibleEntries[i]<<",front,"<<triangles<<'\n';}
    moduleMode=false;FillList();
    for(size_t i=0;i<visibleEntries.size();++i){assetIndex=int(i);parcelW=parcelD=6;rotation=0;BuildScene(true);yaw=-.72f;Capture((fs::path(directory)/(visibleEntries[i]+"_front.bmp")).string());rotation=2;BuildScene(true);Capture((fs::path(directory)/(visibleEntries[i]+"_rear.bmp")).string());report<<visibleEntries[i]<<",lot,"<<triangles<<'\n';}
    auto select=[&](const std::string& id){assetIndex=int(std::find(visibleEntries.begin(),visibleEntries.end(),id)-visibleEntries.begin());SendMessageA(listHandle,LB_SETCURSEL,assetIndex,0);};
    select("rci_residential_high_garden_lot");distantPreview=true;rotation=0;BuildScene(true);Capture((fs::path(directory)/"tower_distant.bmp").string());distantPreview=false;
    select("rci_residential_low_garden_lot");
    for(auto size:std::vector<std::pair<int,int>>{{2,4},{3,6}}){parcelW=size.first;parcelD=size.second;rotation=0;BuildScene(true);Capture((fs::path(directory)/("garden_"+std::to_string(parcelW)+"x"+std::to_string(parcelD)+".bmp")).string());}
    parcelW=parcelD=6;rotation=0;BuildScene(true);SetWindowTextA(widthHandle,"6");SetWindowTextA(depthHandle,"6");
    auto module=std::find(lotModuleIds.begin(),lotModuleIds.end(),"rci_residential_house_8_module");
    if(module!=lotModuleIds.end()){SendMessageA(moduleHandle,CB_SETCURSEL,module-lotModuleIds.begin(),0);UpdateEditor(dataRoot+"\\Modules\\rci_residential_house_8_module.xml");}
    CaptureWorkspace((fs::path(directory)/"asset_manager_ui.bmp").string());

}
}
int main(int argc,char** argv){
    bool batch=argc>1;
    try{
        dataRoot=RuntimeDataDirectory();auto source=fs::path(RuntimeExecutableDirectory())/".."/".."/".."/"City Builder"/"Data";
        if(fs::exists(source/"Modules"))dataRoot=fs::weakly_canonical(source).string();
        std::string capture,selfTest;bool validate=false;for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--data"&&i+1<argc)dataRoot=fs::absolute(argv[++i]).string();else if(a=="--capture"&&i+1<argc)capture=argv[++i];else if(a=="--validate")validate=true;else if(a=="--self-test"&&i+1<argc)selfTest=argv[++i];else throw std::runtime_error("Usage: AssetManager.exe [--data Data] [--capture directory] [--validate] [--self-test directory]");}
        GenerateAssetCatalog(dataRoot);LoadRuntime();
        if(validate)ValidateCatalog();if(!selfTest.empty())EditorSelfTest(selfTest);
        if(capture.empty()&&(validate||!selfTest.empty()))return 0;
        CreateUi();FillList();
        if(!capture.empty()){CaptureCatalog(capture);}else{
            MSG msg;while(GetMessageA(&msg,nullptr,0,0)>0){if(!IsDialogMessageA(windowHandle,&msg)){TranslateMessage(&msg);DispatchMessageA(&msg);}}
        }
        wglMakeCurrent(viewDc,context);glDeleteTextures(1,&texture);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(viewHandle,viewDc);DestroyWindow(windowHandle);DeleteObject(uiFont);
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(!batch)MessageBoxA(nullptr,e.what(),"Asset Manager",MB_OK|MB_ICONERROR);return 1;}
}

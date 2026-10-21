#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define WinMain SmokeDetectorModernLegacyWinMain
#include "SmokeDetectorWin32Modern.cpp"
#undef WinMain

namespace {

using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Vec2;
using promptemon::rom::ResourceKind;
using promptemon::rom::RomInspector;
using promptemon::rom::RomModelArchive;
using promptemon::rom::RomResourceArchive;
using smoke_detector::exporter::AssetExporter;
using smoke_detector::exporter::ResourcePackageExporter;

constexpr int UI_SEARCH = 8101;
constexpr int UI_TOP = 50;
constexpr int UI_BOTTOM = 26;
constexpr int UI_GAP = 10;
constexpr int UI_LEFT = 326;
constexpr int UI_RIGHT = 356;
constexpr int UI_ROW = 29;
constexpr int UI_MIN_W = 1140;
constexpr int UI_MIN_H = 720;

constexpr COLORREF UI_BG = RGB(48,48,48);
constexpr COLORREF UI_PANEL = RGB(38,38,38);
constexpr COLORREF UI_PANEL2 = RGB(44,44,44);
constexpr COLORREF UI_FIELD = RGB(54,54,54);
constexpr COLORREF UI_BORDER = RGB(67,67,67);
constexpr COLORREF UI_TEXT = RGB(224,224,224);
constexpr COLORREF UI_MUTED = RGB(152,152,152);
constexpr COLORREF UI_ACCENT = RGB(83,124,184);
constexpr COLORREF UI_ACCENT_SOFT = RGB(55,72,96);

struct ResourceItem {
    std::string source;
    std::string virtualPath;
    std::string id;
    std::string displayName;
    std::filesystem::path romPath;
    ResourceKind kind = ResourceKind::Unknown;
    std::uint64_t size = 0;
    int category = 6;
};

struct TreeRow {
    bool folder = false;
    std::string key;
    std::string label;
    int depth = 0;
    std::size_t item = static_cast<std::size_t>(-1);
};

struct UvFix {
    int transformType = 0; // 0 Maya, 1 SoftImage, 2 3ds Max
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    float rotation = 0.0f;
};

struct Layout {
    RECT client{};
    RECT top{};
    RECT left{};
    RECT search{};
    RECT filters{};
    RECT tree{};
    RECT preview{};
    RECT right{};
    RECT status{};
    RECT open{};
    RECT exportOne{};
    RECT dump{};
};

HWND gSearch = nullptr;
HBRUSH gEditBrush2 = nullptr;
HFONT gTitle2 = nullptr;
HFONT gSection2 = nullptr;
HFONT gTiny2 = nullptr;

std::vector<ResourceItem> gItems;
std::vector<std::size_t> gFiltered;
std::vector<TreeRow> gTree;
std::unordered_set<std::string> gCollapsed;
std::size_t gSelected = static_cast<std::size_t>(-1);
std::size_t gTreeScroll = 0;
std::size_t gAnimScroll = 0;
std::size_t gTextureScroll = 0;
std::size_t gMaterial = 0;
int gCategory2 = 0;
int gTab = 0; // Properties, Animation, Textures/UV
bool gPreview = false;
std::string gLoadedModelForFixes;
std::unordered_map<std::string, std::vector<UvFix>> gFixesByModel;
std::unordered_map<std::string, std::vector<std::string>> gTexturesByModel;

std::string lo(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsI(const std::string& value, const char* suffix) {
    const std::string a = lo(value), b = lo(suffix);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
}

int categoryFor(const std::string& id, ResourceKind kind) {
    const std::string p = lo(id);
    if ((p.find("anim") != std::string::npos || p.find("motion") != std::string::npos) &&
        (p.find(".bch") != std::string::npos || p.find(".bcres") != std::string::npos)) return 2;
    if (kind == ResourceKind::Model || endsI(p,".bch") || endsI(p,".bch.cx") ||
        endsI(p,".bcres") || endsI(p,".bcmdl") || endsI(p,".cgfx")) return 1;
    if (kind == ResourceKind::Texture || kind == ResourceKind::Graphics || kind == ResourceKind::Palette ||
        endsI(p,".bclim") || endsI(p,".ctpk") || endsI(p,".png") || endsI(p,".tga") || endsI(p,".bmp")) return 3;
    if (kind == ResourceKind::Audio || endsI(p,".bcwav") || endsI(p,".bcstm") || endsI(p,".bcsar") ||
        endsI(p,".bcseq") || endsI(p,".cwav") || endsI(p,".cstm") || endsI(p,".wav") || endsI(p,".ogg")) return 4;
    if (kind == ResourceKind::Archive || endsI(p,".garc") || endsI(p,".narc") || endsI(p,".darc") ||
        endsI(p,".carc") || endsI(p,".szs") || endsI(p,".arc") || endsI(p,".cx")) return 5;
    return 6;
}

const char* badgeFor(int category) {
    switch (category) {
        case 1: return "3D"; case 2: return "AN"; case 3: return "TX";
        case 4: return "SFX"; case 5: return "ARC"; default: return "DAT";
    }
}

const char* filterName(int category) {
    switch (category) {
        case 1: return "3D"; case 2: return "Anim"; case 3: return "Tex";
        case 4: return "SFX"; case 5: return "Arc"; case 6: return "Data"; default: return "All";
    }
}

std::string sourceFile(const std::string& label) {
    const auto marker = label.find("] ");
    return marker == std::string::npos ? label : label.substr(marker + 2);
}

std::string bytesLabel(std::uint64_t size) {
    std::ostringstream out;
    if (size >= 1024ull*1024ull*1024ull) out << (size/(1024ull*1024ull*1024ull)) << " GB";
    else if (size >= 1024ull*1024ull) out << (size/(1024ull*1024ull)) << " MB";
    else if (size >= 1024ull) out << (size/1024ull) << " KB";
    else out << size << " B";
    return out.str();
}

int rw(const RECT& r) { return static_cast<int>(r.right-r.left); }
int rh(const RECT& r) { return static_cast<int>(r.bottom-r.top); }
bool inside(const RECT& r, int x, int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }

Layout makeLayout(HWND hwnd) {
    Layout l{};
    GetClientRect(hwnd,&l.client);
    const int w=rw(l.client), h=rh(l.client);
    l.top={0,0,w,UI_TOP};
    l.status={0,h-UI_BOTTOM,w,h};
    const int top=UI_TOP+UI_GAP, bottom=h-UI_BOTTOM-UI_GAP;
    l.left={UI_GAP,top,UI_GAP+UI_LEFT,bottom};
    l.right={w-UI_GAP-UI_RIGHT,top,w-UI_GAP,bottom};
    l.preview={l.left.right+UI_GAP,top,l.right.left-UI_GAP,bottom};
    l.search={l.left.left+12,l.left.top+47,l.left.right-12,l.left.top+79};
    l.filters={l.left.left+12,l.left.top+87,l.left.right-12,l.left.top+117};
    l.tree={l.left.left+8,l.left.top+126,l.left.right-8,l.left.bottom-8};
    l.dump={w-112,9,w-10,41};
    l.exportOne={w-220,9,w-120,41};
    l.open={w-328,9,w-228,41};
    return l;
}

void uiResources() {
    ensureUiResources();
    if(!gEditBrush2) gEditBrush2=CreateSolidBrush(UI_FIELD);
    if(!gTitle2) gTitle2=createUiFont(18,FW_SEMIBOLD);
    if(!gSection2) gSection2=createUiFont(13,FW_SEMIBOLD);
    if(!gTiny2) gTiny2=createUiFont(11,FW_NORMAL);
}

void freeUiResources() {
    if(gEditBrush2) DeleteObject(gEditBrush2);
    if(gTitle2) DeleteObject(gTitle2);
    if(gSection2) DeleteObject(gSection2);
    if(gTiny2) DeleteObject(gTiny2);
    gEditBrush2=nullptr; gTitle2=gSection2=gTiny2=nullptr;
    releaseUiResources();
}

void fill(HDC dc,const RECT&r,COLORREF c){ HBRUSH b=CreateSolidBrush(c); FillRect(dc,&r,b); DeleteObject(b); }
void box(HDC dc,const RECT&r,COLORREF c,COLORREF border=UI_BORDER,int radius=5){
    HBRUSH b=CreateSolidBrush(c); HPEN p=CreatePen(PS_SOLID,1,border);
    auto ob=SelectObject(dc,b); auto op=SelectObject(dc,p);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(b); DeleteObject(p);
}
void text(HDC dc,const std::string&s,RECT r,HFONT f,COLORREF c,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX){
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,c); SelectObject(dc,f); DrawTextA(dc,s.c_str(),-1,&r,flags);
}
void button(HDC dc,const RECT&r,const char*label,bool accent,bool enabled){
    box(dc,r,enabled?(accent?UI_ACCENT:UI_FIELD):UI_PANEL2,enabled?(accent?UI_ACCENT:UI_BORDER):UI_PANEL2,5);
    text(dc,label,r,gFontSmall,enabled?UI_TEXT:RGB(92,92,92),DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

std::string queryText(){ char v[1024]{}; if(gSearch) GetWindowTextA(gSearch,v,static_cast<int>(sizeof(v))); return lo(v); }

void buildItems(){
    gItems.clear();
    if(!gResourceArchive) return;
    for(const auto& src:gResourceArchive->sourceSummaries()){
        for(const auto& r:gResourceArchive->resourcesForSource(src.label)){
            ResourceItem item;
            item.source=src.label; item.virtualPath=src.label+" :: "+r.id; item.id=r.id;
            item.displayName=r.displayName; item.romPath=src.rom.path; item.kind=r.kind; item.size=r.size;
            item.category=categoryFor(item.id,item.kind); gItems.push_back(std::move(item));
        }
    }
}

std::vector<std::string> pathParts(std::string path){
    if(path.rfind("romfs/",0)==0) path.erase(0,6);
    if(path.rfind("nitrofs/",0)==0) path.erase(0,8);
    std::replace(path.begin(),path.end(),'\\','/');
    std::vector<std::string> out; std::size_t start=0;
    while(start<=path.size()){
        const auto slash=path.find('/',start);
        auto part=path.substr(start,slash==std::string::npos?std::string::npos:slash-start);
        if(!part.empty()) out.push_back(std::move(part));
        if(slash==std::string::npos) break; start=slash+1;
    }
    return out;
}

void buildTree(){
    gTree.clear(); std::unordered_set<std::string> emitted;
    for(auto idx:gFiltered){
        const auto parts=pathParts(gItems[idx].id); if(parts.empty()) continue;
        std::string current; bool hidden=false;
        for(std::size_t i=0;i+1<parts.size();++i){
            if(!current.empty()) current+='/'; current+=parts[i];
            if(hidden) continue;
            if(emitted.insert(current).second) gTree.push_back({true,current,parts[i],static_cast<int>(i),static_cast<std::size_t>(-1)});
            if(gCollapsed.count(current)) hidden=true;
        }
        if(!hidden) gTree.push_back({false,gItems[idx].id,parts.back(),static_cast<int>(parts.size()-1),idx});
    }
}

void applyFilter(){
    const std::string q=queryText(); gFiltered.clear();
    for(std::size_t i=0;i<gItems.size();++i){
        const auto& item=gItems[i]; if(gCategory2 && item.category!=gCategory2) continue;
        if(!q.empty() && lo(item.id+" "+item.displayName+" "+item.source).find(q)==std::string::npos) continue;
        gFiltered.push_back(i);
    }
    std::sort(gFiltered.begin(),gFiltered.end(),[](std::size_t a,std::size_t b){return lo(gItems[a].id)<lo(gItems[b].id);});
    if(gSelected!=static_cast<std::size_t>(-1) && std::find(gFiltered.begin(),gFiltered.end(),gSelected)==gFiltered.end()) gSelected=static_cast<std::size_t>(-1);
    if(gSelected==static_cast<std::size_t>(-1) && !gFiltered.empty()) gSelected=gFiltered.front();
    buildTree();
    if(gWindow) InvalidateRect(gWindow,nullptr,FALSE);
}

std::string modelPath(const ResourceItem& item){
    std::string resource=item.id;
    if(resource.rfind("romfs/",0)==0) resource.erase(0,6);
    if(resource.rfind("nitrofs/",0)==0) resource.erase(0,8);
    const std::string file=sourceFile(item.source);
    for(const auto& m:gModels){
        const auto split=m.find(" :: "); if(split==std::string::npos) continue;
        if(sourceFile(m.substr(0,split))==file && m.substr(split+4)==resource) return m;
    }
    return {};
}

void resetScene(){
    if(gGL) destroyGlTextures();
    gScene.reset(); gSelectedModel.clear(); gIdle=gCurrent=nullptr; gFrame=gYaw=gPitch=0.0f; gZoom=1.0f; gPreview=false;
    gLoadedModelForFixes.clear(); gMaterial=0; gAnimScroll=gTextureScroll=0;
}

std::vector<UvFix>& currentFixes(){
    static std::vector<UvFix> empty;
    if(!gScene || gSelectedModel.empty()) return empty;
    auto& fixes=gFixesByModel[gSelectedModel];
    if(fixes.size()!=gScene->materials().size()) fixes.assign(gScene->materials().size(),UvFix{});
    return fixes;
}

void restoreSavedTextures(){
    if(!gScene || gSelectedModel.empty()) return;
    auto& stored=gTexturesByModel[gSelectedModel];
    auto& mats=gScene->materialsMutable();
    if(stored.empty()){
        stored.reserve(mats.size()); for(const auto& m:mats) stored.push_back(m.textureName);
    }else{
        for(std::size_t i=0;i<mats.size()&&i<stored.size();++i) mats[i].textureName=stored[i];
    }
}

void captureLoadedModel(){
    if(!gScene || gSelectedModel.empty()) return;
    gLoadedModelForFixes=gSelectedModel;
    currentFixes(); restoreSavedTextures();
    if(!gScene->materials().empty()) gMaterial=std::min(gMaterial,gScene->materials().size()-1); else gMaterial=0;
}

void activate(std::size_t idx){
    if(idx>=gItems.size()) return; gSelected=idx;
    const auto& item=gItems[idx]; const std::string model=item.category==1?modelPath(item):std::string{};
    if(!model.empty()){
        try{ loadModel(model); gPreview=true; captureLoadedModel(); gTab=0; ShowWindow(gRenderWindow,SW_SHOW); }
        catch(const std::exception&e){ gPreview=false; ShowWindow(gRenderWindow,SW_HIDE); setStatus(std::string("3D indexed; preview unavailable: ")+e.what()); }
    }else{ gPreview=false; ShowWindow(gRenderWindow,SW_HIDE); setStatus(item.id); }
    InvalidateRect(gWindow,nullptr,FALSE);
}

std::filesystem::path chooseRom(HWND owner){
    wchar_t buffer[32768]{};
    static const wchar_t filter[]=L"ROMs (*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi)\0*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW d{}; d.lStructSize=sizeof(d); d.hwndOwner=owner; d.lpstrFilter=filter; d.lpstrFile=buffer;
    d.nMaxFile=static_cast<DWORD>(std::size(buffer)); d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_NOCHANGEDIR; d.lpstrTitle=L"Open ROM";
    return GetOpenFileNameW(&d)?std::filesystem::path(buffer):std::filesystem::path{};
}

std::filesystem::path cliRom(){
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv) return {};
    std::filesystem::path out; if(argc>1){std::error_code ec; std::filesystem::path p(argv[1]); if(std::filesystem::is_regular_file(p,ec)&&!ec) out=p;} LocalFree(argv); return out;
}

bool loadRom(const std::filesystem::path& path,bool report=true){
    try{
        auto resources=std::make_unique<RomResourceArchive>(); if(!resources->open(path)) throw std::runtime_error("The ROM could not be indexed.");
        auto models=std::make_unique<RomModelArchive>(); models->open(path); auto paths=models->modelResources();
        resetScene(); gResourceArchive=std::move(resources); gArchive=std::move(models); gModels=std::move(paths); gRomDirectory=path; gExportRoot=path.parent_path()/"smoke-detector-exports";
        buildItems(); gCategory2=0; gTreeScroll=0; if(gSearch) SetWindowTextA(gSearch,""); applyFilter();
        for(const auto& m:gModels){ try{loadModel(m); gPreview=true; captureLoadedModel(); break;}catch(...){} }
        if(!gPreview && !gFiltered.empty()) gSelected=gFiltered.front();
        const auto summaries=gResourceArchive->sourceSummaries();
        const std::string provider=summaries.empty()?std::string{}:summaries.front().provider;
        if(provider.find("Encrypted 3DS RomFS")!=std::string::npos || provider.find("key")!=std::string::npos){
            setStatus(provider);
            if(report) MessageBoxA(gWindow,(provider+"\r\n\r\nSmoke Detector does not bundle Nintendo keys. Put your own aes_keys.txt next to the ROM or the executable, then reopen the ROM.").c_str(),"Encrypted Nintendo 3DS ROM",MB_OK|MB_ICONINFORMATION);
        }else setStatus("Loaded "+path.filename().string()+" | "+std::to_string(gItems.size())+" resources");
        if(gWindow){SetWindowTextA(gWindow,("Smoke Detector - "+path.filename().string()).c_str()); ShowWindow(gRenderWindow,gPreview?SW_SHOW:SW_HIDE); InvalidateRect(gWindow,nullptr,FALSE);} return true;
    }catch(const std::exception&e){if(report&&gWindow)MessageBoxA(gWindow,e.what(),"Smoke Detector",MB_OK|MB_ICONERROR);return false;}
}

void initArchives2(){
    const auto cli=cliRom(); if(!cli.empty()&&loadRom(cli,false)) return;
    gArchive=std::make_unique<RomModelArchive>(); gResourceArchive=std::make_unique<RomResourceArchive>(); gModels.clear(); gItems.clear(); gFiltered.clear(); gTree.clear();
    gExportRoot=executableDirectory()/"exports"; setStatus("Open or drag a ROM to begin.");
}

Vec2 sourceTransform(const Material* m,Vec2 uv,int transformType){
    if(!m) return uv;
    const float sx=m->uvScaleU, sy=m->uvScaleV, tx=m->uvTranslateU, ty=m->uvTranslateV, r=m->uvRotation;
    const float ca=std::cos(r), sa=std::sin(r);
    Vec2 out{};
    out.x=uv.x*(sx*ca)+uv.y*(sx*-sa);
    out.y=uv.x*(sy*sa)+uv.y*(sy*ca);
    if(transformType==1){
        out.x+=sx*(-ca*tx-sa*ty);
        out.y+=sy*( sa*tx-ca*ty);
    }else if(transformType==2){
        out.x+=sx*ca*(-tx-0.5f)-sx*sa*(ty-0.5f)+0.5f;
        out.y+=sy*sa*(-tx-0.5f)+sy*ca*(ty-0.5f)+0.5f;
    }else{
        out.x+=sx*((0.5f*sa-0.5f*ca)+0.5f-tx);
        out.y+=sy*((-0.5f*sa-0.5f*ca)+0.5f-ty);
    }
    return out;
}

Vec2 adjustedUv(const Material* material,Vec2 uv,const UvFix* fix){
    const int type=fix?fix->transformType:0; Vec2 out=sourceTransform(material,uv,type); if(!fix) return out;
    float u=(out.x-0.5f)*fix->scaleU, v=(out.y-0.5f)*fix->scaleV;
    const float ca=std::cos(fix->rotation), sa=std::sin(fix->rotation);
    const float ru=u*ca-v*sa, rv=u*sa+v*ca;
    return {ru+0.5f+fix->offsetU,rv+0.5f+fix->offsetV};
}

const UvFix* fixForMaterial(std::size_t index){
    if(!gScene||gSelectedModel.empty()) return nullptr; auto& fixes=currentFixes(); return index<fixes.size()?&fixes[index]:nullptr;
}

void drawScene2(){
    if(!gRenderWindow||!gGL) return; RECT rc{}; GetClientRect(gRenderWindow,&rc); setupProjection(static_cast<int>(rc.right),static_cast<int>(rc.bottom));
    glClearColor(0.115f,0.115f,0.115f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); if(!gScene){SwapBuffers(gDC);return;}
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glLoadIdentity(); glTranslatef(0,0,-3.25f/std::max(0.35f,gZoom)); glRotatef(gPitch,1,0,0); glRotatef(gYaw,0,1,0); glScalef(gFitScale,gFitScale,gFitScale); glTranslatef(-gModelCenter.x,-gModelCenter.y,-gModelCenter.z);
    const auto matrices=gScene->samplePose(gCurrent,gFrame);
    for(const auto& mesh:gScene->meshes()){
        const auto* texture=gScene->textureForMaterial(mesh.materialId); const Material* material=mesh.materialId<gScene->materials().size()?&gScene->materials()[mesh.materialId]:nullptr;
        GLuint tex=0; if(texture){const auto it=gGlTextures.find(texture->name); if(it!=gGlTextures.end()) tex=it->second;}
        if(tex){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,tex);glColor4f(1,1,1,1);}else{glDisable(GL_TEXTURE_2D);glColor4f(.78f,.78f,.8f,1);}
        const UvFix* fix=fixForMaterial(mesh.materialId);
        glBegin(GL_TRIANGLES);
        for(const auto& vertex:mesh.vertices){const auto p=gScene->skin(vertex,matrices);if(tex){const auto uv=adjustedUv(material,vertex.uv,fix);glTexCoord2f(uv.x,1.0f-uv.y);}glVertex3f(p.x,p.y,p.z);} glEnd();
    }
    glBindTexture(GL_TEXTURE_2D,0); glDisable(GL_TEXTURE_2D); SwapBuffers(gDC);
}

void tick2(){
    const auto now=std::chrono::steady_clock::now(); float dt=std::chrono::duration<float>(now-gLastTick).count(); gLastTick=now; dt=std::clamp(dt,0.0f,0.1f);
    if(gCurrent){gFrame+=dt*30.0f;if(gCurrent->frames>0&&gFrame>gCurrent->frames){if(gCurrent->loop)gFrame=std::fmod(gFrame,gCurrent->frames);else{gCurrent=gIdle;gFrame=0;}}}
    drawScene2();
}

std::string safeName2(std::string v){for(char&c:v){unsigned char u=static_cast<unsigned char>(c);if(!(std::isalnum(u)||c=='-'||c=='_'||c=='.'))c='_';}while(!v.empty()&&v.front()=='.')v.erase(v.begin());return v.empty()?"asset":v;}

void writeLiveObj(const std::filesystem::path& modelDir,const Scene& scene,const std::vector<UvFix>& fixes){
    std::ofstream obj(modelDir/"model.obj"); std::ofstream mtl(modelDir/"model.mtl"); if(!obj||!mtl) throw std::runtime_error("Could not update corrected OBJ");
    obj<<"mtllib model.mtl\n"; const auto pose=scene.samplePose(nullptr,0.0f); std::size_t base=1;
    for(std::size_t mi=0;mi<scene.meshes().size();++mi){const auto& mesh=scene.meshes()[mi]; const Material* mat=mesh.materialId<scene.materials().size()?&scene.materials()[mesh.materialId]:nullptr;
        const UvFix* fix=mesh.materialId<fixes.size()?&fixes[mesh.materialId]:nullptr; const std::string mn=safeName2(mat&&!mat->name.empty()?mat->name:"material_"+std::to_string(mi));
        obj<<"o "<<safeName2(mesh.name)<<"\nusemtl "<<mn<<"\n";
        for(const auto& v:mesh.vertices){const auto p=scene.skin(v,pose);obj<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<"\n";}
        for(const auto& v:mesh.vertices){const auto uv=adjustedUv(mat,v.uv,fix);obj<<"vt "<<uv.x<<' '<<(1.0f-uv.y)<<"\n";}
        for(const auto& v:mesh.vertices)obj<<"vn "<<v.normal.x<<' '<<v.normal.y<<' '<<v.normal.z<<"\n";
        for(std::size_t i=0;i+2<mesh.vertices.size();i+=3){obj<<"f";for(int k=0;k<3;++k){const auto n=base+i+static_cast<std::size_t>(k);obj<<' '<<n<<'/'<<n<<'/'<<n;}obj<<"\n";}
        mtl<<"newmtl "<<mn<<"\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\n"; if(mat&&!mat->textureName.empty())mtl<<"map_Kd ../textures/"<<safeName2(mat->textureName)<<".tga\n"; mtl<<"\n"; base+=mesh.vertices.size();
    }
    std::ofstream uv(modelDir/"uv-adjustments.json"); if(uv){uv<<"{\n  \"materials\": [\n";for(std::size_t i=0;i<scene.materials().size();++i){const auto& m=scene.materials()[i];const auto f=i<fixes.size()?fixes[i]:UvFix{};uv<<"    {\"name\":\""<<safeName2(m.name)<<"\",\"transform\":"<<f.transformType<<",\"offset_u\":"<<f.offsetU<<",\"offset_v\":"<<f.offsetV<<",\"scale_u\":"<<f.scaleU<<",\"scale_v\":"<<f.scaleV<<",\"rotation\":"<<f.rotation<<"}"<<(i+1<scene.materials().size()?",":"")<<"\n";}uv<<"  ]\n}\n";}
}

void exportSelected2(){
    if(gSelected>=gItems.size()) return; const auto& item=gItems[gSelected]; const std::string model=item.category==1?modelPath(item):std::string{};
    if(!model.empty()&&gArchive){const auto result=AssetExporter::exportModel(*gArchive,model,gExportRoot); if(gScene&&gSelectedModel==model)writeLiveObj(result.interchangeModel.parent_path(),*gScene,currentFixes()); setStatus("Exported: "+result.interchangeModel.string()); ShellExecuteA(gWindow,"open",result.interchangeModel.parent_path().string().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}
    const auto outDir=gExportRoot/"selected"/safeName2(sourceFile(item.source)); std::filesystem::create_directories(outDir); const auto leafPos=item.id.find_last_of("/\\"); const auto outPath=outDir/safeName2(leafPos==std::string::npos?item.id:item.id.substr(leafPos+1));
    if(item.kind==ResourceKind::RawRom){std::error_code ec;std::filesystem::copy_file(item.romPath,outPath,std::filesystem::copy_options::overwrite_existing,ec);if(ec)throw std::runtime_error(ec.message());}else{const auto payload=gResourceArchive->extract(item.virtualPath);std::ofstream out(outPath,std::ios::binary);if(!out)throw std::runtime_error("Could not create export");if(!payload.bytes.empty())out.write(reinterpret_cast<const char*>(payload.bytes.data()),static_cast<std::streamsize>(payload.bytes.size()));}
    setStatus("Exported: "+outPath.string()); ShellExecuteA(gWindow,"open",outDir.string().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}

void dumpAll2(){
    if(!gResourceArchive||gDumping)return;gDumping=true;setProgress(0,1,{});setStatus("Dumping all resources...");pumpUi();
    try{const auto results=ResourcePackageExporter::exportAll(*gResourceArchive,gExportRoot,{},[](std::size_t c,std::size_t t,const std::string&,const std::string&r){setProgress(t?std::min(c+1,t):0,t,r);setStatus(r.empty()?"Finalizing dump...":"Dump: "+r);pumpUi();},false);std::size_t n=0;for(const auto&r:results)n+=r.resources;gDumping=false;setProgress(0,0,{});setStatus("Dump complete: "+std::to_string(n)+" resources");ShellExecuteA(gWindow,"open",(gExportRoot/"resources").string().c_str(),nullptr,nullptr,SW_SHOWNORMAL);}catch(...){gDumping=false;setProgress(0,0,{});throw;}
}

void layoutControls(HWND hwnd){const auto l=makeLayout(hwnd);if(gSearch)MoveWindow(gSearch,l.search.left+8,l.search.top+2,std::max(10,rw(l.search)-16),rh(l.search)-4,TRUE);if(gRenderWindow){RECT r{l.preview.left+1,l.preview.top+37,l.preview.right-1,l.preview.bottom-1};MoveWindow(gRenderWindow,r.left,r.top,std::max(1,rw(r)),std::max(1,rh(r)),TRUE);ShowWindow(gRenderWindow,gPreview?SW_SHOW:SW_HIDE);}}

void section(HDC dc,const Layout&l,int&y,const char*label){RECT r{l.right.left+10,y,l.right.right-10,y+27};fill(dc,r,UI_PANEL2);RECT a{r.left+7,r.top,r.left+23,r.bottom};text(dc,"v",a,gTiny2,UI_MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);RECT t{a.right+2,r.top,r.right-8,r.bottom};text(dc,label,t,gSection2,UI_TEXT);y+=33;}
void prop(HDC dc,const Layout&l,int&y,const std::string&k,const std::string&v){RECT a{l.right.left+18,y,l.right.left+112,y+24};RECT b{a.right,y,l.right.right-18,y+24};text(dc,k,a,gTiny2,UI_MUTED);text(dc,v,b,gTiny2,UI_TEXT);y+=25;}

void paintTop(HDC dc,const Layout&l){fill(dc,l.top,UI_PANEL);RECT t{16,0,280,UI_TOP};text(dc,"Smoke Detector",t,gTitle2,UI_TEXT);button(dc,l.open,"Open ROM",false,!gDumping);button(dc,l.exportOne,"Export",false,!gDumping&&gSelected<gItems.size());button(dc,l.dump,"Dump all",true,!gDumping&&!gItems.empty());}

void paintLeft(HDC dc,const Layout&l){
    box(dc,l.left,UI_PANEL);RECT h{l.left.left+12,l.left.top+8,l.left.right-12,l.left.top+38};text(dc,"Resources",h,gSection2,UI_TEXT);RECT c{l.left.right-70,h.top,l.left.right-12,h.bottom};text(dc,std::to_string(gFiltered.size()),c,gTiny2,UI_MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    box(dc,l.search,UI_FIELD);const int gap=4,w=(rw(l.filters)-gap*6)/7;for(int i=0;i<7;++i){RECT r{l.filters.left+i*(w+gap),l.filters.top,l.filters.left+i*(w+gap)+w,l.filters.bottom};const bool on=i==gCategory2;box(dc,r,on?UI_ACCENT_SOFT:UI_PANEL2,on?UI_ACCENT:UI_BORDER);text(dc,filterName(i),r,gTiny2,on?UI_TEXT:UI_MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    HRGN clip=CreateRectRgn(l.tree.left,l.tree.top,l.tree.right,l.tree.bottom);SelectClipRgn(dc,clip);const int visible=std::max(1,rh(l.tree)/UI_ROW);const std::size_t maxScroll=gTree.size()>static_cast<std::size_t>(visible)?gTree.size()-visible:0;gTreeScroll=std::min(gTreeScroll,maxScroll);
    for(int i=0;i<=visible;++i){const auto ri=gTreeScroll+static_cast<std::size_t>(i);if(ri>=gTree.size())break;const auto&row=gTree[ri];RECT rr{l.tree.left,l.tree.top+i*UI_ROW,l.tree.right,l.tree.top+(i+1)*UI_ROW-1};const bool sel=!row.folder&&row.item==gSelected;if(sel)fill(dc,rr,UI_ACCENT_SOFT);const int indent=std::min(76,row.depth*14);RECT icon{rr.left+6+indent,rr.top,rr.left+28+indent,rr.bottom};text(dc,row.folder?(gCollapsed.count(row.key)?">":"v"):badgeFor(gItems[row.item].category),icon,gTiny2,row.folder?UI_MUTED:UI_ACCENT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);RECT tr{icon.right+1,rr.top,rr.right-8,rr.bottom};text(dc,row.label,tr,gFontSmall,sel?UI_TEXT:(row.folder?RGB(192,192,192):UI_TEXT));}
    SelectClipRgn(dc,nullptr);DeleteObject(clip);
}

void paintPreview(HDC dc,const Layout&l){box(dc,l.preview,UI_PANEL);RECT h{l.preview.left+12,l.preview.top,l.preview.right-12,l.preview.top+36};text(dc,"Preview",h,gSection2,UI_TEXT);if(gPreview&&gScene){RECT m{l.preview.left+100,l.preview.top,l.preview.right-12,l.preview.top+36};text(dc,gScene->modelName(),m,gTiny2,UI_MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}else if(gSelected<gItems.size()){const auto&item=gItems[gSelected];const int cx=(l.preview.left+l.preview.right)/2,cy=(l.preview.top+37+l.preview.bottom)/2;RECT b{cx-34,cy-48,cx+34,cy+20};box(dc,b,UI_FIELD);text(dc,badgeFor(item.category),b,gTitle2,UI_ACCENT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);RECT n{l.preview.left+30,b.bottom+12,l.preview.right-30,b.bottom+38};text(dc,item.id,n,gFontBody,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);RECT hint{n.left,n.bottom,n.right,n.bottom+24};text(dc,"Indexed resource - export available",hint,gTiny2,UI_MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}}

std::string f3(float value){std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<value;return out.str();}
const char* transformName(int type){return type==1?"SoftImage":(type==2?"3ds Max":"Maya");}

RECT minusRect(const Layout&l,int y){return {l.right.right-120,y,l.right.right-94,y+24};}
RECT plusRect(const Layout&l,int y){return {l.right.right-36,y,l.right.right-10,y+24};}
RECT valueRect(const Layout&l,int y){return {l.right.right-92,y,l.right.right-38,y+24};}

void adjustRow(HDC dc,const Layout&l,int y,const char*label,float value){RECT name{l.right.left+18,y,l.right.left+130,y+24};text(dc,label,name,gTiny2,UI_MUTED);auto m=minusRect(l,y),p=plusRect(l,y),v=valueRect(l,y);box(dc,m,UI_FIELD);box(dc,p,UI_FIELD);box(dc,v,UI_PANEL2);text(dc,"-",m,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);text(dc,"+",p,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);text(dc,f3(value),v,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}

void paintProperties(HDC dc,const Layout&l){if(gSelected>=gItems.size())return;int y=l.right.top+52;const auto&item=gItems[gSelected];section(dc,l,y,"Resource");prop(dc,l,y,"Name",item.displayName.empty()?item.id:item.displayName);prop(dc,l,y,"Type",filterName(item.category));prop(dc,l,y,"Size",item.size?bytesLabel(item.size):"unknown");y+=5;if(gPreview&&gScene){section(dc,l,y,"Geometry");prop(dc,l,y,"Meshes",std::to_string(gScene->meshes().size()));prop(dc,l,y,"Bones",std::to_string(gScene->bones().size()));prop(dc,l,y,"Materials",std::to_string(gScene->materials().size()));prop(dc,l,y,"Textures",std::to_string(gScene->textures().size()));prop(dc,l,y,"Animations",std::to_string(gScene->animations().size()));y+=5;}section(dc,l,y,"Source");prop(dc,l,y,"ROM",sourceFile(item.source));prop(dc,l,y,"Path",item.id);}

void paintAnimations(HDC dc,const Layout&l){if(!gScene)return;const int top=l.right.top+52,rowH=30,visible=std::max(1,(l.right.bottom-top-12)/rowH);const std::size_t total=gScene->animations().size()+1,maxScroll=total>static_cast<std::size_t>(visible)?total-visible:0;gAnimScroll=std::min(gAnimScroll,maxScroll);for(int i=0;i<visible;++i){const auto idx=gAnimScroll+static_cast<std::size_t>(i);if(idx>=total)break;RECT r{l.right.left+10,top+i*rowH,l.right.right-10,top+(i+1)*rowH-2};bool active=false;std::string s;if(idx==0){s="Base pose";active=gCurrent==nullptr;}else{const auto&a=gScene->animations()[idx-1];s=a.name+"   "+std::to_string(static_cast<int>(a.frames))+"f";active=gCurrent==&a;}if(active)fill(dc,r,UI_ACCENT_SOFT);RECT t{r.left+8,r.top,r.right-8,r.bottom};text(dc,s,t,gTiny2,active?UI_TEXT:RGB(195,195,195));}}

void paintTexturesUv(HDC dc,const Layout&l){
    if(!gScene)return;auto& mats=gScene->materialsMutable();auto& fixes=currentFixes();if(!mats.empty())gMaterial=std::min(gMaterial,mats.size()-1);int y=l.right.top+52;
    section(dc,l,y,"Material");if(!mats.empty()){RECT prev{l.right.left+18,y,l.right.left+46,y+27};RECT next{l.right.right-46,y,l.right.right-18,y+27};RECT name{prev.right+5,y,next.left-5,y+27};box(dc,prev,UI_FIELD);box(dc,next,UI_FIELD);box(dc,name,UI_PANEL2);text(dc,"<",prev,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);text(dc,">",next,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);text(dc,mats[gMaterial].name.empty()?"Material "+std::to_string(gMaterial):mats[gMaterial].name,name,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);y+=36;}
    section(dc,l,y,"UV correction");if(!mats.empty()&&gMaterial<fixes.size()){auto&f=fixes[gMaterial];RECT k{l.right.left+18,y,l.right.left+130,y+24};RECT mode{l.right.right-120,y,l.right.right-10,y+24};text(dc,"Transform",k,gTiny2,UI_MUTED);box(dc,mode,UI_FIELD);text(dc,transformName(f.transformType),mode,gTiny2,UI_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);y+=28;adjustRow(dc,l,y,"Offset U",f.offsetU);y+=27;adjustRow(dc,l,y,"Offset V",f.offsetV);y+=27;adjustRow(dc,l,y,"Scale U",f.scaleU);y+=27;adjustRow(dc,l,y,"Scale V",f.scaleV);y+=27;adjustRow(dc,l,y,"Rotation",f.rotation);y+=31;RECT reset{l.right.left+18,y,l.right.right-18,y+27};button(dc,reset,"Reset this material",false,true);y+=36;}
    section(dc,l,y,"Texture");const auto&textures=gScene->textures();const int visible=std::max(1,(l.right.bottom-y-8)/UI_ROW);const std::size_t maxScroll=textures.size()>static_cast<std::size_t>(visible)?textures.size()-visible:0;gTextureScroll=std::min(gTextureScroll,maxScroll);for(int i=0;i<visible;++i){const auto idx=gTextureScroll+static_cast<std::size_t>(i);if(idx>=textures.size())break;const bool active=!mats.empty()&&mats[gMaterial].textureName==textures[idx].name;RECT r{l.right.left+10,y+i*UI_ROW,l.right.right-10,y+(i+1)*UI_ROW-1};if(active)fill(dc,r,UI_ACCENT_SOFT);RECT n{r.left+8,r.top,r.right-76,r.bottom};RECT d{r.right-72,r.top,r.right-8,r.bottom};text(dc,textures[idx].name,n,gTiny2,active?UI_TEXT:RGB(195,195,195));text(dc,std::to_string(textures[idx].width)+"x"+std::to_string(textures[idx].height),d,gTiny2,UI_MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
}

void paintRight(HDC dc,const Layout&l){box(dc,l.right,UI_PANEL);static const char*tabs[]={"Properties","Animation","Textures / UV"};const int tw=(rw(l.right)-20)/3;for(int i=0;i<3;++i){RECT r{l.right.left+10+i*tw,l.right.top+8,l.right.left+10+(i+1)*tw,l.right.top+40};const bool enabled=i==0||(gScene&&(i==1?!gScene->animations().empty():!gScene->textures().empty()));if(gTab==i)fill(dc,r,UI_FIELD);text(dc,tabs[i],r,gTiny2,enabled?(gTab==i?UI_TEXT:UI_MUTED):RGB(82,82,82),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}if(gTab==1)paintAnimations(dc,l);else if(gTab==2)paintTexturesUv(dc,l);else paintProperties(dc,l);}

void paintStatus(HDC dc,const Layout&l){fill(dc,l.status,UI_PANEL);RECT t{12,l.status.top,l.status.right-12,l.status.bottom};text(dc,gStatus,t,gTiny2,UI_MUTED);if(gProgressTotal){const double ratio=std::min(1.0,static_cast<double>(gProgressCurrent)/static_cast<double>(std::max<std::size_t>(1,gProgressTotal)));RECT r{0,l.status.bottom-2,static_cast<LONG>(rw(l.status)*ratio),l.status.bottom};fill(dc,r,UI_ACCENT);}}

void paintAll(HWND hwnd){PAINTSTRUCT ps{};HDC target=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);HDC dc=CreateCompatibleDC(target);HBITMAP bmp=CreateCompatibleBitmap(target,std::max(1,rw(client)),std::max(1,rh(client)));auto old=SelectObject(dc,bmp);fill(dc,client,UI_BG);const auto l=makeLayout(hwnd);paintTop(dc,l);paintLeft(dc,l);paintPreview(dc,l);paintRight(dc,l);paintStatus(dc,l);BitBlt(target,0,0,rw(client),rh(client),dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);EndPaint(hwnd,&ps);}

void clickTexturesUv(HWND hwnd,int x,int y){if(!gScene)return;const auto l=makeLayout(hwnd);auto&mats=gScene->materialsMutable();auto&fixes=currentFixes();if(mats.empty()||gMaterial>=mats.size()||gMaterial>=fixes.size())return;int cy=l.right.top+52+33;
    RECT prev{l.right.left+18,cy,l.right.left+46,cy+27};RECT next{l.right.right-46,cy,l.right.right-18,cy+27};if(inside(prev,x,y)){gMaterial=gMaterial?gMaterial-1:mats.size()-1;InvalidateRect(hwnd,&l.right,FALSE);return;}if(inside(next,x,y)){gMaterial=(gMaterial+1)%mats.size();InvalidateRect(hwnd,&l.right,FALSE);return;}cy+=36+33;
    RECT mode{l.right.right-120,cy,l.right.right-10,cy+24};if(inside(mode,x,y)){fixes[gMaterial].transformType=(fixes[gMaterial].transformType+1)%3;InvalidateRect(hwnd,&l.right,FALSE);return;}cy+=28;
    struct Row{float UvFix::*field;float step;};const Row rows[]={{&UvFix::offsetU,.01f},{&UvFix::offsetV,.01f},{&UvFix::scaleU,.02f},{&UvFix::scaleV,.02f},{&UvFix::rotation,.01745329252f}};
    for(const auto&r:rows){auto minus=minusRect(l,cy),plus=plusRect(l,cy);if(inside(minus,x,y)){fixes[gMaterial].*(r.field)-=r.step;InvalidateRect(hwnd,&l.right,FALSE);return;}if(inside(plus,x,y)){fixes[gMaterial].*(r.field)+=r.step;InvalidateRect(hwnd,&l.right,FALSE);return;}cy+=27;}cy+=4;RECT reset{l.right.left+18,cy,l.right.right-18,cy+27};if(inside(reset,x,y)){fixes[gMaterial]=UvFix{};auto it=gTexturesByModel.find(gSelectedModel);if(it!=gTexturesByModel.end()&&gMaterial<it->second.size())mats[gMaterial].textureName=it->second[gMaterial];InvalidateRect(hwnd,&l.right,FALSE);return;}cy+=36+33;
    if(y>=cy){const int row=(y-cy)/UI_ROW;const auto idx=gTextureScroll+static_cast<std::size_t>(row);if(idx<gScene->textures().size()){mats[gMaterial].textureName=gScene->textures()[idx].name;auto&stored=gTexturesByModel[gSelectedModel];if(stored.size()<mats.size())stored.resize(mats.size());stored[gMaterial]=mats[gMaterial].textureName;setStatus("Texture "+mats[gMaterial].textureName+" applied to "+mats[gMaterial].name);InvalidateRect(hwnd,&l.right,FALSE);}}
}

void clickRight(HWND hwnd,int x,int y){const auto l=makeLayout(hwnd);const int tw=(rw(l.right)-20)/3;for(int i=0;i<3;++i){RECT r{l.right.left+10+i*tw,l.right.top+8,l.right.left+10+(i+1)*tw,l.right.top+40};if(!inside(r,x,y))continue;if(i==0||(gScene&&(i==1?!gScene->animations().empty():!gScene->textures().empty())))gTab=i;InvalidateRect(hwnd,&l.right,FALSE);return;}if(!gScene)return;if(gTab==1){const int top=l.right.top+52;if(y<top)return;const auto idx=gAnimScroll+static_cast<std::size_t>((y-top)/30);if(idx==0){gCurrent=nullptr;gFrame=0;}else if(idx-1<gScene->animations().size())play(&gScene->animations()[idx-1]);InvalidateRect(hwnd,&l.right,FALSE);}else if(gTab==2)clickTexturesUv(hwnd,x,y);}

void clickMain(HWND hwnd,int x,int y){const auto l=makeLayout(hwnd);if(inside(l.open,x,y)){const auto p=chooseRom(hwnd);if(!p.empty())loadRom(p);return;}if(inside(l.exportOne,x,y)&&gSelected<gItems.size()){exportSelected2();return;}if(inside(l.dump,x,y)&&!gItems.empty()){dumpAll2();return;}const int gap=4,w=(rw(l.filters)-gap*6)/7;for(int i=0;i<7;++i){RECT r{l.filters.left+i*(w+gap),l.filters.top,l.filters.left+i*(w+gap)+w,l.filters.bottom};if(inside(r,x,y)){gCategory2=i;gTreeScroll=0;applyFilter();return;}}if(inside(l.tree,x,y)){const auto idx=gTreeScroll+static_cast<std::size_t>((y-l.tree.top)/UI_ROW);if(idx<gTree.size()){const auto row=gTree[idx];if(row.folder){if(gCollapsed.count(row.key))gCollapsed.erase(row.key);else gCollapsed.insert(row.key);buildTree();InvalidateRect(hwnd,&l.left,FALSE);}else activate(row.item);}return;}if(inside(l.right,x,y))clickRight(hwnd,x,y);}

LRESULT CALLBACK MainProc2(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){switch(msg){case WM_CREATE:uiResources();applyDarkTitleBar(hwnd);gWindow=hwnd;gSearch=CreateWindowExA(0,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,10,10,hwnd,controlId(UI_SEARCH),GetModuleHandleA(nullptr),nullptr);SendMessage(gSearch,WM_SETFONT,reinterpret_cast<WPARAM>(gFontSmall),TRUE);SendMessageA(gSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));gRenderWindow=CreateWindowExA(0,"SmokeDetectorRenderModern","",WS_CHILD|WS_CLIPSIBLINGS,0,0,1,1,hwnd,nullptr,GetModuleHandleA(nullptr),nullptr);if(!gRenderWindow)return-1;DragAcceptFiles(hwnd,TRUE);applyFilter();layoutControls(hwnd);return 0;case WM_SIZE:layoutControls(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={UI_MIN_W,UI_MIN_H};return 0;case WM_PAINT:paintAll(hwnd);return 0;case WM_ERASEBKGND:return 1;case WM_CTLCOLOREDIT:{HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,UI_FIELD);SetTextColor(dc,UI_TEXT);return reinterpret_cast<LRESULT>(gEditBrush2);}case WM_COMMAND:if(LOWORD(wp)==UI_SEARCH&&HIWORD(wp)==EN_CHANGE){gTreeScroll=0;applyFilter();return 0;}break;case WM_LBUTTONDOWN:if(!gDumping){try{clickMain(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));}catch(const std::exception&e){MessageBoxA(hwnd,e.what(),"Smoke Detector",MB_OK|MB_ICONERROR);}}return 0;case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);const auto l=makeLayout(hwnd);const int d=GET_WHEEL_DELTA_WPARAM(wp);if(inside(l.tree,p.x,p.y)){const int visible=std::max(1,rh(l.tree)/UI_ROW);const std::size_t maxScroll=gTree.size()>static_cast<std::size_t>(visible)?gTree.size()-visible:0;if(d<0)gTreeScroll=std::min(maxScroll,gTreeScroll+3);else gTreeScroll=gTreeScroll>3?gTreeScroll-3:0;InvalidateRect(hwnd,&l.left,FALSE);return 0;}if(inside(l.right,p.x,p.y)&&gScene){std::size_t*scroll=gTab==1?&gAnimScroll:(gTab==2?&gTextureScroll:nullptr);if(scroll){if(d<0)*scroll+=2;else *scroll=*scroll>2?*scroll-2:0;InvalidateRect(hwnd,&l.right,FALSE);return 0;}}break;}case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(wp);wchar_t path[32768]{};if(DragQueryFileW(drop,0,path,static_cast<UINT>(std::size(path)))>0)loadRom(std::filesystem::path(path));DragFinish(drop);return 0;}case WM_CLOSE:if(!gDumping)DestroyWindow(hwnd);return 0;case WM_DESTROY:shutdownOpenGL();freeUiResources();PostQuitMessage(0);return 0;}return DefWindowProcA(hwnd,msg,wp,lp);}

} // namespace

int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show){
    try{uiResources();initArchives2();WNDCLASSA rc{};rc.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;rc.lpfnWndProc=RenderProc;rc.hInstance=instance;rc.hCursor=LoadCursor(nullptr,IDC_ARROW);rc.hbrBackground=gBrushBg;rc.lpszClassName="SmokeDetectorRenderModern";if(!RegisterClassA(&rc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Could not register render surface");WNDCLASSA wc{};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=MainProc2;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=gBrushBg;wc.lpszClassName="SmokeDetectorStudio2";if(!RegisterClassA(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Could not register main window");gWindow=CreateWindowExA(0,wc.lpszClassName,"Smoke Detector",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1400,840,nullptr,nullptr,instance,nullptr);if(!gWindow||!gRenderWindow)throw std::runtime_error("Could not create Smoke Detector window");initOpenGL(gRenderWindow);if(gScene)uploadGlTextures();ShowWindow(gRenderWindow,gPreview?SW_SHOW:SW_HIDE);layoutControls(gWindow);ShowWindow(gWindow,show);UpdateWindow(gWindow);gLastTick=std::chrono::steady_clock::now();MSG msg{};bool running=true;while(running){while(PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageA(&msg);}if(running)tick2();Sleep(1);}return 0;}catch(const std::exception&e){MessageBoxA(nullptr,e.what(),"Smoke Detector error",MB_OK|MB_ICONERROR);freeUiResources();return 1;}
}

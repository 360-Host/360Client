#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  360Client.hpp — ALL custom headers in one file                     ║
// ║  Drop into: src/Client/GUI/Engine/                                 ║
// ║  Then #include "360Client.hpp" from EngineCore.hpp                 ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <array>
#include <cmath>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// ════════════════════════════════════════════════════════════════════════
// FRUSTUM CULLER
// ════════════════════════════════════════════════════════════════════════
namespace Render360 {

struct Plane {
    glm::vec3 normal; float d;
    float distanceTo(const glm::vec3& p) const noexcept { return glm::dot(normal,p)+d; }
};

struct AABB { glm::vec3 min, max; };

class Frustum {
public:
    void update(const glm::mat4& vp) noexcept {
        _p[0]=norm({vp[0][3]+vp[0][0],vp[1][3]+vp[1][0],vp[2][3]+vp[2][0],vp[3][3]+vp[3][0]});
        _p[1]=norm({vp[0][3]-vp[0][0],vp[1][3]-vp[1][0],vp[2][3]-vp[2][0],vp[3][3]-vp[3][0]});
        _p[2]=norm({vp[0][3]+vp[0][1],vp[1][3]+vp[1][1],vp[2][3]+vp[2][1],vp[3][3]+vp[3][1]});
        _p[3]=norm({vp[0][3]-vp[0][1],vp[1][3]-vp[1][1],vp[2][3]-vp[2][1],vp[3][3]-vp[3][1]});
        _p[4]=norm({vp[0][3]+vp[0][2],vp[1][3]+vp[1][2],vp[2][3]+vp[2][2],vp[3][3]+vp[3][2]});
        _p[5]=norm({vp[0][3]-vp[0][2],vp[1][3]-vp[1][2],vp[2][3]-vp[2][2],vp[3][3]-vp[3][2]});
    }
    bool containsSphere(const glm::vec3& c, float r) const noexcept {
        for(auto& p:_p) if(p.distanceTo(c)<-r) return false; return true; }
    bool containsAABB(const AABB& b) const noexcept {
        for(auto& p:_p){
            glm::vec3 pv=b.min;
            if(p.normal.x>=0)pv.x=b.max.x;
            if(p.normal.y>=0)pv.y=b.max.y;
            if(p.normal.z>=0)pv.z=b.max.z;
            if(p.distanceTo(pv)<0)return false;
        } return true; }
private:
    std::array<Plane,6> _p;
    static Plane norm(Plane p) noexcept {
        float l=glm::length(p.normal);
        if(l>0.0001f){p.normal/=l;p.d/=l;} return p; }
};

inline bool isInFOV(const glm::vec3& cam,const glm::vec3& fwd,const glm::vec3& world,float fovDeg) noexcept {
    glm::vec3 d=world-cam; float dist=glm::length(d);
    if(dist<0.001f)return true;
    return glm::dot(fwd,d/dist)>=std::cos(glm::radians(fovDeg*0.5f)); }

inline float distSq(const glm::vec3& a,const glm::vec3& b) noexcept {
    glm::vec3 d=a-b; return glm::dot(d,d); }

// ════════════════════════════════════════════════════════════════════════
// CHUNK CACHE
// ════════════════════════════════════════════════════════════════════════

struct ChunkPos {
    int x,z;
    bool operator==(const ChunkPos& o) const noexcept {return x==o.x&&z==o.z;}
};
struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& p) const noexcept {
        return std::hash<int>{}(p.x)^(std::hash<int>{}(p.z)<<32); }
};

enum class ChunkLOD : uint8_t { Full=0, Medium=1, Low=2, Hidden=3 };

struct ChunkState {
    ChunkLOD lod=ChunkLOD::Full;
    bool inFrustum=true, everLoaded=false;
    float distanceSq=0;
    std::chrono::steady_clock::time_point lastSeen;
};

class ChunkCache {
public:
    static ChunkCache& get(){static ChunkCache i;return i;}

    void update(ChunkPos pos,bool inFrustum,float dSq,int maxRd) {
        std::lock_guard<std::mutex> lk(_m);
        auto& s=_c[pos];
        s.inFrustum=inFrustum; s.distanceSq=dSq; s.everLoaded=true;
        if(inFrustum) s.lastSeen=std::chrono::steady_clock::now();
        float d=std::sqrt(dSq), mx=maxRd*16.f;
        if(!inFrustum)          s.lod=ChunkLOD::Hidden;
        else if(d<mx*0.4f)      s.lod=ChunkLOD::Full;
        else if(d<mx*0.7f)      s.lod=ChunkLOD::Medium;
        else                    s.lod=ChunkLOD::Low;
    }

    ChunkLOD getLOD(ChunkPos p) {
        std::lock_guard<std::mutex> lk(_m);
        auto it=_c.find(p); return it!=_c.end()?it->second.lod:ChunkLOD::Full; }

    bool isVisible(ChunkPos p) {
        std::lock_guard<std::mutex> lk(_m);
        auto it=_c.find(p); return it==_c.end()||it->second.lod!=ChunkLOD::Hidden; }

    void evictDistant(float px,float pz,int maxRd) {
        std::lock_guard<std::mutex> lk(_m);
        float mx=(maxRd+4)*16.f, mxSq=mx*mx;
        for(auto it=_c.begin();it!=_c.end();){
            float cx=it->first.x*16.f+8,cz=it->first.z*16.f+8;
            float dx=cx-px,dz=cz-pz;
            it=(dx*dx+dz*dz>mxSq)?_c.erase(it):++it;
        }
    }

    void clear(){std::lock_guard<std::mutex> lk(_m);_c.clear();}
    size_t size(){std::lock_guard<std::mutex> lk(_m);return _c.size();}

private:
    std::unordered_map<ChunkPos,ChunkState,ChunkPosHash> _c;
    std::mutex _m;
};

inline ChunkPos worldToChunk(float x,float z) noexcept {
    return {(int)std::floor(x/16.f),(int)std::floor(z/16.f)}; }

// ════════════════════════════════════════════════════════════════════════
// ENGINE PERF — font metrics cache + frame throttle
// ════════════════════════════════════════════════════════════════════════

struct FontMetricsKey {
    std::string name; int weight; float size, scale;
    bool operator==(const FontMetricsKey& o) const noexcept {
        return weight==o.weight&&name==o.name&&
               std::fabs(size-o.size)<0.001f&&std::fabs(scale-o.scale)<0.001f; }
};
struct FontMetricsKeyHash {
    std::size_t operator()(const FontMetricsKey& k) const noexcept {
        return std::hash<std::string>{}(k.name)
             ^(std::hash<int>{}(k.weight)<<1)
             ^(std::hash<int>{}((int)(k.size*100))<<2)
             ^(std::hash<int>{}((int)(k.scale*100))<<3); }
};
struct FontMetrics { int baseFontSize; float scaleFactor, targetFontSize; };

class FontMetricsCache {
public:
    static FontMetricsCache& get(){static FontMetricsCache c;return c;}
    const FontMetrics* find(const FontMetricsKey& k) const noexcept {
        auto it=_m.find(k); return it!=_m.end()?&it->second:nullptr; }
    void put(const FontMetricsKey& k,FontMetrics v){
        if((int)_m.size()>=512)_m.clear(); _m.emplace(k,v); }
    void invalidate(){_m.clear();}
    static FontMetrics resolve(const std::string& name,int weight,float size,float scale,bool px){
        FontMetricsKey k{name,weight,size,scale};
        if(auto* c=get().find(k))return *c;
        static constexpr int B[]={16,32,64,128,256};
        float t=(size*scale)*0.18f;
        if(px&&t>1.f)t=std::floor(t);
        int base=B[4];
        for(int b:B){if(t<=b){base=b;break;}}
        FontMetrics m{base,t/(float)base,t};
        get().put(k,m); return m; }
private:
    std::unordered_map<FontMetricsKey,FontMetrics,FontMetricsKeyHash> _m;
};

class FrameThrottle {
public:
    static void update(float fps) noexcept { _fps=fps; }
    static bool skipHeavy()   noexcept { return _fps<20.f; }
    static bool reduceBlur()  noexcept { return _fps<35.f; }
    static float blurQuality() noexcept {
        if(skipHeavy())return 0.f;
        if(!reduceBlur())return 1.f;
        return std::clamp((_fps-20.f)/15.f,0.25f,1.f); }
    static float current() noexcept { return _fps; }
    static float scaleIntensity(float v) noexcept { return v*blurQuality(); }
private:
    static inline float _fps=60.f;
};

} // namespace Render360

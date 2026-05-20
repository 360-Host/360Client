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

namespace Render360 {

// ════════════════════════════════════════════════════════════════════════
// HASH COMBINE HELPER
// Replaces the broken << 32 XOR pattern in the original, which produces
// zero for the z component on 64-bit and is UB on 32-bit.
// ════════════════════════════════════════════════════════════════════════
inline std::size_t hashCombine(std::size_t seed, std::size_t v) noexcept {
    // Boost-style golden-ratio mix
    return seed ^ (v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

// ════════════════════════════════════════════════════════════════════════
// FRUSTUM CULLER
// Fix: Plane had no 4-float constructor — the original brace-init
// {a,b,c,d} only worked if the struct had exactly that layout.
// Added an explicit constructor so extraction from VP matrix rows is clear.
// ════════════════════════════════════════════════════════════════════════
struct Plane {
    glm::vec3 normal{};
    float     d = 0.f;

    Plane() = default;
    Plane(float a, float b, float c, float w) noexcept
        : normal(a, b, c), d(w) {}

    float distanceTo(const glm::vec3& p) const noexcept {
        return glm::dot(normal, p) + d;
    }
};

struct AABB { glm::vec3 min, max; };

class Frustum {
public:
    // Extract the 6 frustum planes from a combined view-projection matrix.
    void update(const glm::mat4& vp) noexcept {
        // Gribb/Hartmann method — column-major glm layout
        _p[0] = norm(Plane( vp[0][3]+vp[0][0], vp[1][3]+vp[1][0], vp[2][3]+vp[2][0], vp[3][3]+vp[3][0] )); // Left
        _p[1] = norm(Plane( vp[0][3]-vp[0][0], vp[1][3]-vp[1][0], vp[2][3]-vp[2][0], vp[3][3]-vp[3][0] )); // Right
        _p[2] = norm(Plane( vp[0][3]+vp[0][1], vp[1][3]+vp[1][1], vp[2][3]+vp[2][1], vp[3][3]+vp[3][1] )); // Bottom
        _p[3] = norm(Plane( vp[0][3]-vp[0][1], vp[1][3]-vp[1][1], vp[2][3]-vp[2][1], vp[3][3]-vp[3][1] )); // Top
        _p[4] = norm(Plane( vp[0][3]+vp[0][2], vp[1][3]+vp[1][2], vp[2][3]+vp[2][2], vp[3][3]+vp[3][2] )); // Near
        _p[5] = norm(Plane( vp[0][3]-vp[0][2], vp[1][3]-vp[1][2], vp[2][3]-vp[2][2], vp[3][3]-vp[3][2] )); // Far
    }

    bool containsSphere(const glm::vec3& c, float r) const noexcept {
        for (const auto& p : _p)
            if (p.distanceTo(c) < -r) return false;
        return true;
    }

    bool containsAABB(const AABB& b) const noexcept {
        for (const auto& p : _p) {
            glm::vec3 pv = b.min;
            if (p.normal.x >= 0.f) pv.x = b.max.x;
            if (p.normal.y >= 0.f) pv.y = b.max.y;
            if (p.normal.z >= 0.f) pv.z = b.max.z;
            if (p.distanceTo(pv) < 0.f) return false;
        }
        return true;
    }

private:
    std::array<Plane, 6> _p;

    static Plane norm(Plane p) noexcept {
        float l = glm::length(p.normal);
        if (l > 0.0001f) { p.normal /= l; p.d /= l; }
        return p;
    }
};

inline bool isInFOV(const glm::vec3& cam, const glm::vec3& fwd,
                    const glm::vec3& world, float fovDeg) noexcept {
    glm::vec3 d = world - cam;
    float dist = glm::length(d);
    if (dist < 0.001f) return true;
    return glm::dot(fwd, d / dist) >= std::cos(glm::radians(fovDeg * 0.5f));
}

inline float distSq(const glm::vec3& a, const glm::vec3& b) noexcept {
    glm::vec3 d = a - b;
    return glm::dot(d, d);
}

// ════════════════════════════════════════════════════════════════════════
// CHUNK CACHE
// Fix: ChunkPosHash used `<< 32` which is UB on 32-bit and produces 0
// on 64-bit (shifting by the full width). Replaced with hashCombine().
// ════════════════════════════════════════════════════════════════════════
struct ChunkPos {
    int x = 0, z = 0;
    bool operator==(const ChunkPos& o) const noexcept { return x == o.x && z == o.z; }
};

struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& p) const noexcept {
        std::size_t h = std::hash<int>{}(p.x);
        return hashCombine(h, std::hash<int>{}(p.z));
    }
};

enum class ChunkLOD : uint8_t { Full = 0, Medium = 1, Low = 2, Hidden = 3 };

struct ChunkState {
    ChunkLOD   lod        = ChunkLOD::Full;
    bool       inFrustum  = true;
    bool       everLoaded = false;
    float      distanceSq = 0.f;
    std::chrono::steady_clock::time_point lastSeen;
};

class ChunkCache {
public:
    static ChunkCache& get() { static ChunkCache i; return i; }

    void update(ChunkPos pos, bool inFrustum, float dSq, int maxRd) {
        std::lock_guard<std::mutex> lk(_m);
        auto& s      = _c[pos];
        s.inFrustum  = inFrustum;
        s.distanceSq = dSq;
        s.everLoaded = true;
        if (inFrustum) s.lastSeen = std::chrono::steady_clock::now();

        float d  = std::sqrt(dSq);
        float mx = static_cast<float>(maxRd) * 16.f;
        if      (!inFrustum)   s.lod = ChunkLOD::Hidden;
        else if (d < mx * 0.4f) s.lod = ChunkLOD::Full;
        else if (d < mx * 0.7f) s.lod = ChunkLOD::Medium;
        else                    s.lod = ChunkLOD::Low;
    }

    ChunkLOD getLOD(ChunkPos p) {
        std::lock_guard<std::mutex> lk(_m);
        auto it = _c.find(p);
        return it != _c.end() ? it->second.lod : ChunkLOD::Full;
    }

    bool isVisible(ChunkPos p) {
        std::lock_guard<std::mutex> lk(_m);
        auto it = _c.find(p);
        return it == _c.end() || it->second.lod != ChunkLOD::Hidden;
    }

    void evictDistant(float px, float pz, int maxRd) {
        std::lock_guard<std::mutex> lk(_m);
        float mx   = static_cast<float>(maxRd + 4) * 16.f;
        float mxSq = mx * mx;
        for (auto it = _c.begin(); it != _c.end(); ) {
            float cx = static_cast<float>(it->first.x) * 16.f + 8.f;
            float cz = static_cast<float>(it->first.z) * 16.f + 8.f;
            float dx = cx - px, dz = cz - pz;
            it = (dx*dx + dz*dz > mxSq) ? _c.erase(it) : ++it;
        }
    }

    void   clear() { std::lock_guard<std::mutex> lk(_m); _c.clear(); }
    size_t size()  { std::lock_guard<std::mutex> lk(_m); return _c.size(); }

private:
    std::unordered_map<ChunkPos, ChunkState, ChunkPosHash> _c;
    std::mutex _m;
};

inline ChunkPos worldToChunk(float x, float z) noexcept {
    return { static_cast<int>(std::floor(x / 16.f)),
             static_cast<int>(std::floor(z / 16.f)) };
}

// ════════════════════════════════════════════════════════════════════════
// ENGINE PERF — font metrics cache + frame throttle
// Fix: FontMetricsKeyHash used the same broken XOR-shift pattern.
//      Replaced with hashCombine chain.
// ════════════════════════════════════════════════════════════════════════
struct FontMetricsKey {
    std::string name;
    int   weight = 0;
    float size   = 0.f;
    float scale  = 0.f;

    bool operator==(const FontMetricsKey& o) const noexcept {
        return weight == o.weight && name == o.name &&
               std::fabs(size  - o.size)  < 0.001f &&
               std::fabs(scale - o.scale) < 0.001f;
    }
};

struct FontMetricsKeyHash {
    std::size_t operator()(const FontMetricsKey& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.name);
        h = hashCombine(h, std::hash<int>{}(k.weight));
        h = hashCombine(h, std::hash<int>{}(static_cast<int>(k.size  * 100.f)));
        h = hashCombine(h, std::hash<int>{}(static_cast<int>(k.scale * 100.f)));
        return h;
    }
};

struct FontMetrics { int baseFontSize; float scaleFactor, targetFontSize; };

class FontMetricsCache {
public:
    static FontMetricsCache& get() { static FontMetricsCache c; return c; }

    const FontMetrics* find(const FontMetricsKey& k) const noexcept {
        auto it = _m.find(k);
        return it != _m.end() ? &it->second : nullptr;
    }

    void put(const FontMetricsKey& k, FontMetrics v) {
        if (static_cast<int>(_m.size()) >= 512) _m.clear();
        _m.emplace(k, v);
    }

    void invalidate() { _m.clear(); }

    static FontMetrics resolve(const std::string& name, int weight,
                               float size, float scale, bool px) {
        FontMetricsKey k{ name, weight, size, scale };
        if (auto* c = get().find(k)) return *c;

        static constexpr int B[] = { 16, 32, 64, 128, 256 };
        float t = (size * scale) * 0.18f;
        if (px && t > 1.f) t = std::floor(t);
        int base = B[4];
        for (int b : B) { if (t <= b) { base = b; break; } }
        FontMetrics m{ base, t / static_cast<float>(base), t };
        get().put(k, m);
        return m;
    }

private:
    std::unordered_map<FontMetricsKey, FontMetrics, FontMetricsKeyHash> _m;
};

// ════════════════════════════════════════════════════════════════════════
// FRAME THROTTLE
// ════════════════════════════════════════════════════════════════════════
class FrameThrottle {
public:
    static void  update(float fps) noexcept { _fps = fps; }
    static bool  skipHeavy()       noexcept { return _fps < 20.f; }
    static bool  reduceBlur()      noexcept { return _fps < 35.f; }
    static float current()         noexcept { return _fps; }

    static float blurQuality() noexcept {
        if (skipHeavy())  return 0.f;
        if (!reduceBlur()) return 1.f;
        return std::clamp((_fps - 20.f) / 15.f, 0.25f, 1.f);
    }

    static float scaleIntensity(float v) noexcept { return v * blurQuality(); }

private:
    static inline float _fps = 60.f;
};

} // namespace Render360

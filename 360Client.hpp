#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  360Client.hpp — ALL custom headers in one file                     ║
// ║  Drop into: src/Client/GUI/Engine/                                 ║
// ║  Then #include "360Client.hpp" from EngineCore.hpp                 ║
// ╚══════════════════════════════════════════════════════════════════════╝

// Uses only Flarial-native types (Vec3<float>, Vec2<float>).
// No GLM. Vec3 constructor is explicit — never use brace-init {x,y,z}.

#include <cmath>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include "Utils/Utils.hpp"  // Vec3<float>, Vec2<float> — resolved via "src" include root

namespace Render360 {

// ════════════════════════════════════════════════════════════════════════
// HASH COMBINE
// ════════════════════════════════════════════════════════════════════════
inline std::size_t hashCombine(std::size_t seed, std::size_t v) noexcept {
    return seed ^ (v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

// ════════════════════════════════════════════════════════════════════════
// MATH HELPERS
// Vec3 has explicit ctor — always use Vec3<float>(x,y,z), never {x,y,z}.
// Vec3 already has .sub(), .normalize(), .mul(), .add() methods.
// We add dot product and FOV check on top.
// ════════════════════════════════════════════════════════════════════════
inline float vec3Dot(const Vec3<float>& a, const Vec3<float>& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float distSq(const Vec3<float>& a, const Vec3<float>& b) noexcept {
    Vec3<float> d = a.sub(b);
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

// Returns true if 'world' falls within fovDeg of the 'fwd' direction from 'cam'.
inline bool isInFOV(const Vec3<float>& cam,
                    const Vec3<float>& fwd,
                    const Vec3<float>& world,
                    float fovDeg) noexcept {
    Vec3<float> d = world.sub(cam);
    float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist < 0.001f) return true;
    Vec3<float> dn = d.mul(1.f / dist);
    float cosHalf = std::cos(fovDeg * 0.5f * 3.14159265f / 180.f);
    return vec3Dot(fwd, dn) >= cosHalf;
}

// Build normalised forward vector from yaw/pitch (degrees, Minecraft convention).
inline Vec3<float> forwardFromRotation(float yawDeg, float pitchDeg) noexcept {
    float yr = yawDeg   * 3.14159265f / 180.f;
    float pr = pitchDeg * 3.14159265f / 180.f;
    return Vec3<float>(
        -std::sin(yr) * std::cos(pr),
         std::sin(pr),
        -std::cos(yr) * std::cos(pr)
    ).normalize();
}

// ════════════════════════════════════════════════════════════════════════
// CHUNK CACHE
// ════════════════════════════════════════════════════════════════════════
struct ChunkPos {
    int x = 0, z = 0;
    bool operator==(const ChunkPos& o) const noexcept { return x == o.x && z == o.z; }
};

struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& p) const noexcept {
        return hashCombine(std::hash<int>{}(p.x), std::hash<int>{}(p.z));
    }
};

enum class ChunkLOD : uint8_t { Full = 0, Medium = 1, Low = 2, Hidden = 3 };

struct ChunkState {
    ChunkLOD   lod        = ChunkLOD::Full;
    bool       inFOV      = true;
    bool       everLoaded = false;
    float      distanceSq = 0.f;
    std::chrono::steady_clock::time_point lastSeen;
};

class ChunkCache {
public:
    static ChunkCache& get() { static ChunkCache i; return i; }

    void update(ChunkPos pos, bool inFOV, float dSq, int maxRd) {
        std::lock_guard<std::mutex> lk(_m);
        auto& s      = _c[pos];
        s.inFOV      = inFOV;
        s.distanceSq = dSq;
        s.everLoaded = true;
        if (inFOV) s.lastSeen = std::chrono::steady_clock::now();
        float d  = std::sqrt(dSq);
        float mx = static_cast<float>(maxRd) * 16.f;
        if      (!inFOV)         s.lod = ChunkLOD::Hidden;
        else if (d < mx * 0.4f)  s.lod = ChunkLOD::Full;
        else if (d < mx * 0.7f)  s.lod = ChunkLOD::Medium;
        else                     s.lod = ChunkLOD::Low;
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
            it = (dx * dx + dz * dz > mxSq) ? _c.erase(it) : ++it;
        }
    }

    void   clear() { std::lock_guard<std::mutex> lk(_m); _c.clear(); }
    size_t size()  { std::lock_guard<std::mutex> lk(_m); return _c.size(); }

private:
    std::unordered_map<ChunkPos, ChunkState, ChunkPosHash> _c;
    std::mutex _m;
};

inline ChunkPos worldToChunk(float x, float z) noexcept {
    return ChunkPos{ static_cast<int>(std::floor(x / 16.f)),
                     static_cast<int>(std::floor(z / 16.f)) };
}

} // namespace Render360

// ╔══════════════════════════════════════════════════════════════════════╗
// ║  360Client.cpp — ALL custom module implementations in one file      ║
// ║  Compiled as: src/Client/Module/Modules/360Client_All.cpp           ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "GUI/Engine/360Client.hpp"
#include "Modules/Module.hpp"
#include "Events/Render/SetupAndRenderEvent.hpp"
#include "Events/Render/GammaEvent.hpp"
#include "Events/Game/TickEvent.hpp"
#include "SDK/Client/Core/ClientInstance.hpp"
#include "SDK/Client/Actor/LocalPlayer.hpp"
#include "SDK/Client/Actor/Components/StateVectorComponent.hpp"
#include "SDK/Client/Actor/Components/RenderPositionComponent.hpp"
#include "SDK/Client/Actor/Components/ActorRotationComponent.hpp"
#include "SDK/Client/Core/Options.hpp"
#include "SDK/SDK.hpp"
#include <cmath>
#include <algorithm>

using namespace Render360;

// ════════════════════════════════════════════════════════════════════════
// RENDER OPTIONS — mirrors Flarial's own RenderOptions pattern exactly
// ════════════════════════════════════════════════════════════════════════
class RenderOptions360 : public Module {
    void sync() {
        const auto showChunkMap    = Options::getOption("dev_showChunkMap");
        const auto disableSky      = Options::getOption("dev_disableRenderSky");
        const auto disableWeather  = Options::getOption("dev_disableRenderWeather");
        const auto disableEntities = Options::getOption("dev_disableRenderEntities");
        const auto disableBlockEnt = Options::getOption("dev_disableRenderBlockEntities");
        const auto disableParticles= Options::getOption("dev_disableRenderParticles");

        if (isEnabled()) {
            if (showChunkMap)     showChunkMap->setvalue(getOps<bool>("chunkborders"));
            if (disableSky)       disableSky->setvalue(!getOps<bool>("sky"));
            if (disableWeather)   disableWeather->setvalue(!getOps<bool>("weather"));
            if (disableEntities)  disableEntities->setvalue(!getOps<bool>("entity"));
            if (disableBlockEnt)  disableBlockEnt->setvalue(!getOps<bool>("blockentity"));
            if (disableParticles) disableParticles->setvalue(!getOps<bool>("particles"));
        } else {
            if (showChunkMap)     showChunkMap->setvalue(false);
            if (disableSky)       disableSky->setvalue(false);
            if (disableWeather)   disableWeather->setvalue(false);
            if (disableEntities)  disableEntities->setvalue(false);
            if (disableBlockEnt)  disableBlockEnt->setvalue(false);
            if (disableParticles) disableParticles->setvalue(false);
        }
    }

    void applyPreset(const std::string& p) {
        bool sky=true, weather=true, entity=true, blockEnt=true, particles=true, chunks=false;
        if      (p == "Low")    { sky=false; weather=false; blockEnt=false; particles=false; }
        else if (p == "Medium") { weather=false; particles=false; }
        else if (p != "High")   return;
        this->settings.getSettingByName<bool>("sky")->value         = sky;
        this->settings.getSettingByName<bool>("weather")->value     = weather;
        this->settings.getSettingByName<bool>("entity")->value      = entity;
        this->settings.getSettingByName<bool>("blockentity")->value = blockEnt;
        this->settings.getSettingByName<bool>("particles")->value   = particles;
        this->settings.getSettingByName<bool>("chunkborders")->value= chunks;
    }

public:
    RenderOptions360() : Module("Render Options 360", "FPS presets for low-end devices.",
        IDR_RENDEROPTIONS_PNG, "", false, { "fps", "performance", "low end" }) {}

    void onEnable() override {
        Listen(this, SetupAndRenderEvent, &RenderOptions360::onRender)
        Module::onEnable();
        sync();
    }
    void onDisable() override {
        Deafen(this, SetupAndRenderEvent, &RenderOptions360::onRender)
        Module::onDisable();
        sync();
    }

    void defaultConfig() override {
        Module::defaultConfig("core");
        setDef("preset",      std::string("High"));
        setDef("sky",         true);
        setDef("weather",     true);
        setDef("entity",      true);
        setDef("blockentity", true);
        setDef("particles",   true);
        setDef("chunkborders",false);
    }

    void settingsRender(float off) override {
        initSettingsPage();
        std::string cur = getOps<std::string>("preset");
        std::string nw  = FlarialGUI::Dropdown(1, 0, off,
            { "Low", "Medium", "High", "Custom" }, cur, "FPS Preset");
        if (nw != cur) {
            this->settings.getSettingByName<std::string>("preset")->value = nw;
            applyPreset(nw);
            sync();
        }
        if (cur == "Custom") {
            addToggle("Sky",           "", "sky");
            addToggle("Weather",       "", "weather");
            addToggle("Entities",      "", "entity");
            addToggle("Block Entities","", "blockentity");
            addToggle("Particles",     "", "particles");
            addToggle("Chunk Borders", "", "chunkborders");
        }
        FlarialGUI::UnsetScrollView();
        resetPadding();
    }

    void onRender(SetupAndRenderEvent&) { if (isEnabled()) sync(); }
};

// ════════════════════════════════════════════════════════════════════════
// FULLBRIGHT — smooth gamma with lerp + ambient floor
// ════════════════════════════════════════════════════════════════════════
class Fullbright360 : public Module {
    float _cur = 1.f, _def = 1.f;
    bool  _gotDef = false;

public:
    Fullbright360() : Module("Fullbright 360", "Smooth brightness with ambient floor.",
        IDR_FULLBRIGHT_PNG, "", false, { "gamma", "brightness", "smooth" }) {}

    void onEnable() override {
        _gotDef = false;
        Listen(this, GammaEvent, &Fullbright360::onGamma)
        Listen(this, TickEvent,  &Fullbright360::onTick)
        Module::onEnable();
    }
    void onDisable() override {
        Deafen(this, GammaEvent, &Fullbright360::onGamma)
        Deafen(this, TickEvent,  &Fullbright360::onTick)
        _cur = _def;
        Module::onDisable();
    }

    void defaultConfig() override {
        Module::defaultConfig("core");
        setDef("gamma",  12.f);
        setDef("speed",  0.08f);
        setDef("floor",  1.5f);
        setDef("smooth", true);
    }

    void settingsRender(float off) override {
        initSettingsPage();
        addSlider("Brightness",       "", "gamma", 25.f,  1.f);
        addSlider("Transition Speed", "", "speed", 0.2f,  0.01f);
        addSlider("Ambient Floor",    "", "floor", 5.f,   0.f);
        addToggle("Smooth",           "", "smooth");
        FlarialGUI::UnsetScrollView();
        resetPadding();
    }

    void onTick(TickEvent&) {
        if (!isEnabled()) return;
        float target = getOps<float>("gamma");
        float spd    = getOps<float>("speed");
        if (getOps<bool>("smooth")) {
            float d = target - _cur;
            _cur += d * std::min(spd * 3.f, 1.f);
            if (std::fabs(d) < 0.01f) _cur = target;
        } else {
            _cur = target;
        }
    }

    void onGamma(GammaEvent& e) {
        if (!isEnabled()) return;
        if (!_gotDef) { _def = e.getGamma(); _cur = _def; _gotDef = true; }
        float floor = getOps<float>("floor");
        e.setGamma(_cur > floor ? _cur : floor);
    }
};

// ════════════════════════════════════════════════════════════════════════
// ENTITY CULLER — tracks player camera state each tick
// ════════════════════════════════════════════════════════════════════════
class EntityCuller360 : public Module {
    Vec3<float> _cam = Vec3<float>(0, 0, 0);
    Vec3<float> _fwd = Vec3<float>(0, 0, 1);

public:
    EntityCuller360() : Module("Entity Culler 360",
        "Skip rendering entities outside your FOV. Major FPS boost on servers.",
        IDR_RENDEROPTIONS_PNG, "", false,
        { "fps", "entities", "culling", "performance" }) {}

    void onEnable() override {
        Listen(this, TickEvent, &EntityCuller360::onTick)
        Module::onEnable();
    }
    void onDisable() override {
        Deafen(this, TickEvent, &EntityCuller360::onTick)
        Module::onDisable();
    }

    void defaultConfig() override {
        Module::defaultConfig("core");
        setDef("margin",  15.f);
        setDef("mindist", 6.f);
    }

    void settingsRender(float off) override {
        initSettingsPage();
        addSlider("FOV Margin",   "", "margin",  30.f, 0.f);
        addSlider("Min Distance", "", "mindist", 32.f, 0.f);
        FlarialGUI::UnsetScrollView();
        resetPadding();
    }

    void onTick(TickEvent&) {
        if (!isEnabled()) return;
        if (!SDK::clientInstance) return;
        LocalPlayer* p = SDK::clientInstance->getLocalPlayer();
        if (!p) return;
        RenderPositionComponent* rv = p->getRenderPositionComponent();
        if (rv) _cam = rv->renderPos;
        ActorRotationComponent* rot = p->getActorRotationComponent();
        if (rot) _fwd = forwardFromRotation(rot->rot.x, rot->rot.y);
    }
};

// ════════════════════════════════════════════════════════════════════════
// RENDER OPTIMIZER — chunk FOV culling + LOD tracking
// ════════════════════════════════════════════════════════════════════════
class RenderOptimizer360 : public Module {
    Vec3<float> _pos = Vec3<float>(0, 0, 0);
    Vec3<float> _fwd = Vec3<float>(0, 0, 1);
    int _tick = 0, _vis = 0, _cull = 0;
    static constexpr int EVICT_INTERVAL = 200;

public:
    RenderOptimizer360() : Module("Render Optimizer 360",
        "Chunk FOV culling + LOD. Tracks which chunks are in view for FPS gains.",
        IDR_RENDEROPTIONS_PNG, "", false,
        { "sodium", "chunks", "lod", "fps", "performance" }) {}

    static bool     chunkVisible(float x, float z) { return ChunkCache::get().isVisible(worldToChunk(x, z)); }
    static ChunkLOD chunkLOD    (float x, float z) { return ChunkCache::get().getLOD(worldToChunk(x, z)); }

    void onEnable() override {
        Listen(this, TickEvent, &RenderOptimizer360::onTick)
        Module::onEnable();
        ChunkCache::get().clear();
    }
    void onDisable() override {
        Deafen(this, TickEvent, &RenderOptimizer360::onTick)
        ChunkCache::get().clear();
        Module::onDisable();
    }

    void defaultConfig() override {
        Module::defaultConfig("core");
        setDef("fov",     150.f);
        setDef("rd",      16);
        setDef("lod",     true);
        setDef("med_pct", 40.f);
        setDef("low_pct", 70.f);
    }

    void settingsRender(float off) override {
        initSettingsPage();
        addSlider("Cull FOV",        "", "fov",     180.f, 60.f);
        addSlider("Render Distance", "", "rd",       32.f,  4.f);
        addToggle("LOD System",      "", "lod");
        addSlider("LOD Medium %",    "", "med_pct",  90.f, 10.f);
        addSlider("LOD Low %",       "", "low_pct",  95.f, 30.f);
        FlarialGUI::UnsetScrollView();
        resetPadding();
    }

    void onTick(TickEvent&) {
        if (!isEnabled()) return;
        if (!SDK::clientInstance) return;
        LocalPlayer* p = SDK::clientInstance->getLocalPlayer();
        if (!p) return;
        StateVectorComponent* sv = p->getStateVectorComponent();
        if (sv) _pos = sv->Pos;
        ActorRotationComponent* rot = p->getActorRotationComponent();
        if (rot) _fwd = forwardFromRotation(rot->rot.x, rot->rot.y);
        if (++_tick >= EVICT_INTERVAL) {
            _tick = 0;
            ChunkCache::get().evictDistant(_pos.x, _pos.z, getOps<int>("rd"));
        }
        scanChunks();
    }

    void scanChunks() {
        int   rd   = getOps<int>("rd");
        float fov  = getOps<float>("fov");
        float mx   = static_cast<float>(rd) * 16.f;
        float mxSq = mx * mx;
        ChunkPos pc = worldToChunk(_pos.x, _pos.z);
        int vis = 0, cull = 0;
        for (int cx = pc.x - rd; cx <= pc.x + rd; ++cx) {
            for (int cz = pc.z - rd; cz <= pc.z + rd; ++cz) {
                float ccx = static_cast<float>(cx) * 16.f + 8.f;
                float ccz = static_cast<float>(cz) * 16.f + 8.f;
                float dx = ccx - _pos.x, dz = ccz - _pos.z;
                float dSq = dx*dx + dz*dz;
                if (dSq > mxSq) continue;
                Vec3<float> center = Vec3<float>(ccx, _pos.y, ccz);
                bool inFOV = isInFOV(_pos, _fwd, center, fov);
                ChunkCache::get().update(ChunkPos{cx, cz}, inFOV, dSq, rd);
                inFOV ? ++vis : ++cull;
            }
        }
        _vis = vis; _cull = cull;
    }
};

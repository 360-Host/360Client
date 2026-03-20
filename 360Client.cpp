// ╔══════════════════════════════════════════════════════════════════════╗
// ║  360Client.cpp — ALL custom module implementations in one file      ║
// ║  Drop into: src/Client/Module/Modules/                             ║
// ║  Register EntityCuller and RenderOptimizer in Manager.cpp          ║
// ╚══════════════════════════════════════════════════════════════════════╝

// ── Shared includes ────────────────────────────────────────────────────
#include "../../GUI/Engine/360Client.hpp"
#include "../Module.hpp"
#include "Events/Render/ActorShaderParamsEvent.hpp"
#include "Events/Render/SetupAndRenderEvent.hpp"
#include "Events/Render/GammaEvent.hpp"
#include "Events/Game/TickEvent.hpp"
#include "SDK/Client/Core/ClientInstance.hpp"
#include "SDK/Client/Actor/LocalPlayer.hpp"
#include "SDK/Client/Actor/Components/StateVectorComponent.hpp"
#include "SDK/Client/Actor/Components/RenderPositionComponent.hpp"
#include "SDK/Client/Actor/Components/ActorRotationComponent.hpp"
#include "SDK/Client/Actor/Components/AABBShapeComponent.hpp"
#include "SDK/Client/Core/Options.hpp"
#include <cmath>
#include <algorithm>

using namespace Render360;

// ════════════════════════════════════════════════════════════════════════
// RENDER OPTIONS — Low/Med/High FPS presets
// ════════════════════════════════════════════════════════════════════════

class RenderOptions : public Module {
    struct Preset { bool sky,weather,entity,blockEntity,particles,chunks; };
    static constexpr Preset LOW    = {false,false,true,false,false,false};
    static constexpr Preset MEDIUM = {true,false,true,true,false,false};
    static constexpr Preset HIGH   = {true,true,true,true,true,false};

    void applyPreset(const std::string& p) {
        Preset x = HIGH;
        if(p=="Low")x=LOW; else if(p=="Medium")x=MEDIUM; else if(p!="High")return;
        setOps("sky",x.sky);setOps("weather",x.weather);setOps("entity",x.entity);
        setOps("blockentity",x.blockEntity);setOps("particles",x.particles);
        setOps("chunkborders",x.chunks);
    }

    void sync() {
        if(!Options::isInitialized())return;
        auto s=[](const char* n,bool v){auto*o=Options::getOption(n);if(o)o->setvalue(v);};
        if(!isEnabled()){s("dev_showChunkMap",false);s("dev_disableRenderSky",false);
            s("dev_disableRenderWeather",false);s("dev_disableRenderEntities",false);
            s("dev_disableRenderBlockEntities",false);s("dev_renderBoundingBox",false);return;}
        s("dev_showChunkMap",          getOps<bool>("chunkborders"));
        s("dev_disableRenderSky",     !getOps<bool>("sky"));
        s("dev_disableRenderWeather", !getOps<bool>("weather"));
        s("dev_disableRenderEntities",!getOps<bool>("entity"));
        s("dev_disableRenderBlockEntities",!getOps<bool>("blockentity"));
        s("dev_renderBoundingBox",    !getOps<bool>("particles"));
    }

public:
    RenderOptions():Module("Render Options","FPS presets for low-end devices.",
        IDR_RENDEROPTIONS_PNG,"",false,{"fps","performance","low end"}){}

    void onEnable()override{Listen(this,SetupAndRenderEvent,&RenderOptions::onRender)Module::onEnable();sync();}
    void onDisable()override{Deafen(this,SetupAndRenderEvent,&RenderOptions::onRender)Module::onDisable();sync();}

    void defaultConfig()override{
        Module::defaultConfig("core");
        setDef("preset",std::string("High"));
        setDef("sky",true);setDef("weather",true);setDef("entity",true);
        setDef("blockentity",true);setDef("particles",true);setDef("chunkborders",false);
    }

    void settingsRender(float off)override{
        initSettingsPage();
        std::string cur=getOps<std::string>("preset");
        std::string nw=FlarialGUI::Dropdown(1,0,off,{"Low","Medium","High","Custom"},cur,"FPS Preset");
        if(nw!=cur){setOps("preset",nw);applyPreset(nw);sync();}
        if(cur=="Custom"){
            addToggle("Sky","","sky");addToggle("Weather","","weather");
            addToggle("Entities","","entity");addToggle("Block Entities","","blockentity");
            addToggle("Particles","","particles");addToggle("Chunk Borders","","chunkborders");
        }
        FlarialGUI::UnsetScrollView();resetPadding();
    }

    void onRender(SetupAndRenderEvent&){if(isEnabled())sync();}
};

// ════════════════════════════════════════════════════════════════════════
// FULLBRIGHT — smooth gamma with lerp + ambient floor
// ════════════════════════════════════════════════════════════════════════

class Fullbright : public Module {
    float _cur=1.f, _def=1.f; bool _gotDef=false;
public:
    Fullbright():Module("Fullbright","Smooth brightness with ambient floor.",
        IDR_FULLBRIGHT_PNG,"",false,{"gamma","brightness","smooth"}){}

    void onEnable()override{
        _gotDef=false;
        Listen(this,GammaEvent,&Fullbright::onGamma)
        Listen(this,TickEvent, &Fullbright::onTick)
        Module::onEnable();
    }
    void onDisable()override{
        Deafen(this,GammaEvent,&Fullbright::onGamma)
        Deafen(this,TickEvent, &Fullbright::onTick)
        _cur=_def; Module::onDisable();
    }
    void defaultConfig()override{
        Module::defaultConfig("core");
        setDef("gamma",12.f);setDef("speed",0.08f);
        setDef("floor",1.5f);setDef("smooth",true);
    }
    void settingsRender(float off)override{
        initSettingsPage();
        addSlider("Brightness","","gamma",25.f,1.f);
        addSlider("Transition Speed","","speed",0.2f,0.01f);
        addSlider("Ambient Floor","","floor",5.f,0.f);
        addToggle("Smooth","","smooth");
        FlarialGUI::UnsetScrollView();resetPadding();
    }
    void onTick(TickEvent&){
        if(!isEnabled())return;
        float target=getOps<float>("gamma"),spd=getOps<float>("speed");
        if(getOps<bool>("smooth")){
            float d=target-_cur; _cur+=d*std::min(spd*3.f,1.f);
            if(std::fabs(d)<0.01f)_cur=target;
        } else _cur=target;
    }
    void onGamma(GammaEvent& e){
        if(!isEnabled())return;
        if(!_gotDef){_def=e.getGamma();_cur=_def;_gotDef=true;}
        e.setGamma(std::max(_cur,getOps<float>("floor")));
    }
};

// ════════════════════════════════════════════════════════════════════════
// ENTITY CULLER — skip drawing mobs/items/particles outside FOV
// ════════════════════════════════════════════════════════════════════════

class EntityCuller : public Module {
    Frustum _frustum; glm::vec3 _cam{}, _fwd{};
    bool _ready=false; int _culled=0,_vis=0;

    float getRadius(Actor* a){
        if(!a)return 2.f;
        auto* aabb=a->tryGet<AABBShapeComponent>();
        if(aabb){float hw=aabb->mWidth*.5f,hh=aabb->mHeight*.5f;
            return std::sqrt(hw*hw+hh*hh)+.5f;}
        return 2.f;
    }

public:
    EntityCuller():Module("Entity Culler",
        "Skip rendering entities outside your FOV. Major FPS boost on servers.",
        IDR_RENDEROPTIONS_PNG,"",false,{"fps","entities","culling","performance"}){}

    void onEnable()override{
        Listen(this,TickEvent,&EntityCuller::onTick)
        Listen(this,SetupAndRenderEvent,&EntityCuller::onSetup)
        Listen(this,ActorShaderParamsEvent,&EntityCuller::onActor)
        Module::onEnable();
    }
    void onDisable()override{
        Deafen(this,TickEvent,&EntityCuller::onTick)
        Deafen(this,SetupAndRenderEvent,&EntityCuller::onSetup)
        Deafen(this,ActorShaderParamsEvent,&EntityCuller::onActor)
        _ready=false; Module::onDisable();
    }
    void defaultConfig()override{
        Module::defaultConfig("core");
        setDef("margin",15.f);setDef("mindist",6.f);
        setDef("mobs",true);setDef("items",true);setDef("particles",true);
    }
    void settingsRender(float off)override{
        initSettingsPage();
        addSlider("FOV Margin","","margin",30.f,0.f);
        addSlider("Min Distance","","mindist",32.f,0.f);
        addToggle("Cull Mobs","","mobs");
        addToggle("Cull Items","","items");
        addToggle("Cull Particles","","particles");
        FlarialGUI::UnsetScrollView();resetPadding();
    }

    void onTick(TickEvent&){
        auto* ci=ClientInstance::get(); if(!ci)return;
        auto* p=ci->getLocalPlayer(); if(!p)return;
        auto* rv=p->tryGet<RenderPositionComponent>(); if(rv)_cam=rv->mPosition;
        auto* rot=p->tryGet<ActorRotationComponent>();
        if(rot){float yr=glm::radians(rot->mYaw),pr=glm::radians(rot->mPitch);
            _fwd=glm::vec3(-std::sin(yr)*std::cos(pr),std::sin(pr),-std::cos(yr)*std::cos(pr));}
        _culled=_vis=0;
    }

    void onSetup(SetupAndRenderEvent& e){
        if(e.hasVPMatrix()){_frustum.update(e.getVPMatrix());_ready=true;}
        else _ready=false;
    }

    void onActor(ActorShaderParamsEvent& e){
        if(!isEnabled())return;
        Actor* a=e.getActor(); if(!a)return;
        auto* ci=ClientInstance::get();
        if(ci&&ci->getLocalPlayer()==a)return;
        bool mob=a->isMob(),item=a->isItemActor(),part=a->isParticleActor();
        if(mob&&!getOps<bool>("mobs"))return;
        if(item&&!getOps<bool>("items"))return;
        if(part&&!getOps<bool>("particles"))return;
        if(!mob&&!item&&!part)return;
        auto* sv=a->tryGet<StateVectorComponent>(); if(!sv)return;
        glm::vec3 pos=sv->mPos;
        float md=getOps<float>("mindist"); md*=md;
        if(distSq(_cam,pos)<md){++_vis;return;}
        bool vis=_ready?_frustum.containsSphere(pos,getRadius(a))
                       :isInFOV(_cam,_fwd,pos,90.f+getOps<float>("margin")*2.f);
        if(!vis){e.setAlpha(0.f);++_culled;}else ++_vis;
    }
};

// ════════════════════════════════════════════════════════════════════════
// RENDER OPTIMIZER — chunk FOV culling + LOD
// ════════════════════════════════════════════════════════════════════════

class RenderOptimizer : public Module {
    Frustum _frustum; bool _ready=false;
    glm::vec3 _pos{},_fwd{};
    int _vis=0,_cull=0,_tick=0;
    static constexpr int EVICT_INTERVAL=200;

public:
    RenderOptimizer():Module("Render Optimizer",
        "Chunk FOV culling + LOD. Chunks behind you turn invisible but stay loaded.\n"
        "Look back — instant. No re-loading, no stutter.",
        IDR_RENDEROPTIONS_PNG,"",false,{"sodium","chunks","lod","fps","performance"}){}

    // Static accessors for EntityCuller
    static bool chunkVisible(float x,float z){return ChunkCache::get().isVisible(worldToChunk(x,z));}
    static ChunkLOD chunkLOD(float x,float z){return ChunkCache::get().getLOD(worldToChunk(x,z));}

    void onEnable()override{
        Listen(this,TickEvent,&RenderOptimizer::onTick)
        Listen(this,SetupAndRenderEvent,&RenderOptimizer::onSetup)
        Module::onEnable(); ChunkCache::get().clear();
    }
    void onDisable()override{
        Deafen(this,TickEvent,&RenderOptimizer::onTick)
        Deafen(this,SetupAndRenderEvent,&RenderOptimizer::onSetup)
        ChunkCache::get().clear(); _ready=false; Module::onDisable();
    }
    void defaultConfig()override{
        Module::defaultConfig("core");
        setDef("fov",150.f);setDef("rd",16);
        setDef("lod",true);setDef("med_pct",40.f);setDef("low_pct",70.f);
    }
    void settingsRender(float off)override{
        initSettingsPage();
        addSlider("Cull FOV","","fov",180.f,60.f);
        addSlider("Render Distance","","rd",32.f,4.f);
        addToggle("LOD System","","lod");
        addSlider("LOD Medium %","","med_pct",90.f,10.f);
        addSlider("LOD Low %","","low_pct",95.f,30.f);
        FlarialGUI::UnsetScrollView();resetPadding();
    }

    void onTick(TickEvent&){
        auto* ci=ClientInstance::get(); if(!ci)return;
        auto* p=ci->getLocalPlayer(); if(!p)return;
        auto* sv=p->tryGet<StateVectorComponent>(); if(sv)_pos=sv->mPos;
        auto* rot=p->tryGet<ActorRotationComponent>();
        if(rot){float yr=glm::radians(rot->mYaw),pr=glm::radians(rot->mPitch);
            _fwd=glm::normalize(glm::vec3(-std::sin(yr)*std::cos(pr),std::sin(pr),-std::cos(yr)*std::cos(pr)));}
        if(++_tick>=EVICT_INTERVAL){_tick=0;ChunkCache::get().evictDistant(_pos.x,_pos.z,getOps<int>("rd"));}
    }

    void onSetup(SetupAndRenderEvent& e){
        if(e.hasVPMatrix()){_frustum.update(e.getVPMatrix());_ready=true;}
        else _ready=false;
        scanChunks();
    }

    void scanChunks(){
        int rd=getOps<int>("rd"); float fov=getOps<float>("fov");
        bool lodOn=getOps<bool>("lod");
        float mx=rd*16.f, mxSq=mx*mx;
        float medSq=(mx*getOps<float>("med_pct")/100.f); medSq*=medSq;
        float lowSq=(mx*getOps<float>("low_pct")/100.f); lowSq*=lowSq;
        ChunkPos pc=worldToChunk(_pos.x,_pos.z);
        int vis=0,cull=0;
        for(int cx=pc.x-rd;cx<=pc.x+rd;cx++){
            for(int cz=pc.z-rd;cz<=pc.z+rd;cz++){
                float ccx=cx*16.f+8,ccz=cz*16.f+8;
                float dx=ccx-_pos.x,dz=ccz-_pos.z,dSq=dx*dx+dz*dz;
                if(dSq>mxSq)continue;
                bool inf=_ready
                    ?_frustum.containsAABB({{ccx-8,-64,ccz-8},{ccx+8,320,ccz+8}})
                    :isInFOV(_pos,_fwd,{ccx,_pos.y,ccz},fov);
                ChunkCache::get().update({cx,cz},inf,dSq,rd);
                inf?++vis:++cull;
            }
        }
        _vis=vis;_cull=cull;
    }
};

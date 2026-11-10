#include "PCH.h"
#include "RuntimeCompatibility.h"
#include <RE/B/BSGraphics.h>
#include "AcidSoaker.h"
#include "DrinkingSession.h"
#include "ColdDrinks.h"
#include "MixedDrinkModels.h"
#include "FrozenSimulationCache.h"
#include <RE/B/BSTimer.h>
#include <RE/U/UI.h>
#include <optional>
#include "SettledTilt.h"
#include <RE/B/BSLightingShaderMaterialBase.h>
#include <RE/B/BSTextureSet.h>
#include <RE/N/NiTexture.h>
#include "SunShadowBounds.h"
#include "Tuning.h"
#include <RE/B/BGSGodRays.h>
#include <RE/T/TESWeather.h>
#include <RE/B/BSInputEnableManager.h>
#include <RE/B/BSInputEnableLayer.h>
#include "Profiles.h"
#include "BottleShapeName.h"
#include "StaticLiquidExclusion.h"
#include "Renderer.h"
#include "NativeOptics.h"
#include "GodrayShield.h"
#include "ShieldHooks.h"
#include "FrostQueue.h"
#include "Performance.h"
#include "WorldDirection.h"
#include "AssignedLights.h"
#include "AdaptiveQuality.h"
#include "ShakeMotion.h"
#include "CarryFrame.h"
#include <RE/B/BSShaderManager.h>
#include <RE/B/BSLight.h>
#include <RE/S/Sky.h>
#include <RE/S/Sun.h>
#include <RE/I/ImageSpaceManager.h>
#undef near
#undef far
#include <RE/N/NiCamera.h>
#include <RE/N/NiDirectionalLight.h>
#include <RE/B/BSGraphics.h>
#include <RE/B/BSEffectShaderMaterial.h>
#include <RE/B/BSShadowDirectionalLight.h>
#include <RE/S/ShadowSceneNode.h>
#include <RE/H/hkRefPtr.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include "BottleSpace.h"
#include <atomic>
#include <future>
#include <RE/M/Main.h>
#include <RE/S/Setting.h>
#include <MinHook.h>

namespace {
using namespace nuka;

// Apply after records load, before inventory/world models are created. Never mark forms changed.
void InitializeMixedDrinkModels() {
    auto* data=RE::TESDataHandler::GetSingleton();
    if(!data)return;
    for(const auto id:{0x58AB8u,0x101295u}) {
        auto* form=data->LookupForm(id,"Fallout4.esm");
        if(!form||form->GetFormType()!=RE::ENUM_FORM_ID::kALCH) {
            REX::WARN("Mixed drinks: missing ALCH {:08X}",id);continue;
        }
        auto* drink=static_cast<RE::AlchemyItem*>(form);
        const std::string old=drink->GetModel()?drink->GetModel():"";
        if(const auto* replacement=MixedDrinkFullModel(id,old)) {
            drink->SetModel(replacement);
            REX::INFO("Mixed drinks: {:08X} model {} -> {}",id,old,replacement);
        } else REX::INFO("Mixed drinks: {:08X} preserving model {}",id,old);
    }
}

const std::filesystem::path root=L"Data/F4SE/Plugins/DynamicBottleLiquids";
Settings settings;
bool diagnostics=false;
std::atomic<std::shared_ptr<DrinkMap>> liveDrinks;
thread_local std::shared_ptr<DrinkMap> drawDrinks;
std::atomic<std::shared_ptr<DrinkMap>> pendingDrinks;
thread_local const DrinkProfile* activeDrink=nullptr;
Renderer renderer;
std::mutex renderMutex;
std::atomic_bool resetRequested{};
ID3D11DeviceContext* hookedContext{};
using BatchFn=void (*)(RE::BSRenderPass*);
using DrawFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
BatchFn originalBatch{};
DrawFn originalDraw{};
const void* registeredGeometry=nullptr;
thread_local RE::BSRenderPass* active{};
struct Instance {
    RE::NiPointer<RE::BSGeometry> owner;
    const DrinkProfile* selectedProfile=nullptr;
    Motion motion;
    Constants constants,previous,output;
    int raySteps=96;bool detail=true;float interval=0;float motionWeight=-1;float lastWeaponFill=-1;
    AcidSoakerShotTracker shotTracker;
    Vec position{},velocity{},shakeAxis{};
    EmissionTracker emissionTracker;
    FrozenSimulationCache frozenCache;
    BubbleBudgetRequest bubbleRequest;
    BubbleLimits bubbleQuota{0,0,0};
    BubbleLimits nextBubbleQuota{0,0,0};
    bool bubbleBudgetDirty=true;
    double nextTriggerLog{},nextFoamLog{};
    float logAngularPeak{},logVerticalPeak{},logSizePeak{};
    unsigned logTriggerSources{},logBirths{};
    double time{};
    std::uint32_t frame=~0u;
};
std::unordered_map<RE::BSGeometry*,Instance> instances;
void AllocateBubblesForFrame(unsigned frame){
    static unsigned lastFrame=~0u;if(lastFrame==frame)return;lastFrame=frame;
    std::vector<BubbleBudgetRequest> requests;requests.reserve(instances.size());
    for(auto& [geometry,i]:instances){
        i.nextBubbleQuota={0,0,0};
        if(frame-i.frame<=1){
            requests.push_back(i.bubbleRequest);
            requests.back().destination=&i.nextBubbleQuota;
        }
    }
    AllocateSceneBubbles(requests);
    for(auto& [geometry,i]:instances){
        const auto quota=i.nextBubbleQuota;
        if(quota!=i.bubbleQuota){i.bubbleQuota=quota;i.bubbleBudgetDirty=true;i.frozenCache={};}
        // Reclaim all old particles before any new-frame simulation can spawn.
        ApplyBubbleLimits(i.motion,quota);
    }
}
struct LodEntry {RE::NiPointer<RE::BSGeometry> owner;DistanceLod state;SettledTiltCache tilt;ShieldMotion shieldMotion;};
std::unordered_map<RE::BSGeometry*,LodEntry> distanceLods;
void CleanInstances(double now){
    static double next=0;if(now<next)return;next=now+.5;
    std::erase_if(instances,[&](const auto& item){return now-item.second.time>2;});
    std::erase_if(distanceLods,[&](const auto& item){return now-item.second.state.time>2;});
}
bool Under(const RE::NiAVObject* object,const RE::NiAVObject* root){
    if(!root)return false;
    for(unsigned depth=0;object&&depth<128;object=object->parent,++depth)if(object==root)return true;
    return false;
}
// Only retain geometry: retaining ancestors would keep the attachment alive.
struct DrinkSessionEntry {
    RE::NiPointer<RE::BSGeometry> owner;
    std::vector<const RE::NiAVObject*> attachment;
    nuka::DrinkingSession liquid;
    std::array<float,16> canonical{};
};
std::unordered_map<RE::BSGeometry*,DrinkSessionEntry> drinkingSessions;
std::vector<const RE::NiAVObject*> Attachment(RE::NiAVObject* object){
    std::vector<const RE::NiAVObject*> result;
    for(unsigned depth=0;object&&depth<128;object=object->parent,++depth)result.push_back(object);
    if(object||result.size()<2)result.clear();
    return result;
}
void TickDrinkingSessions(){
    auto* graphics=RE::BSGraphics::State::GetSingleton();if(!graphics)return;
    static std::uint32_t lastFrame=~0u;
    if(lastFrame==graphics->frameCount)return;lastFrame=graphics->frameCount;
    std::lock_guard lock(renderMutex);
    if(resetRequested.exchange(false)){instances.clear();distanceLods.clear();drinkingSessions.clear();}
    const auto* ui=RE::UI::GetSingleton();const auto* main=RE::Main::GetSingleton();
    float dt=(!ui||!main||ui->GameIsPaused()||main->freezeTime||TuningVisible())?0.f:RE::BSTimer::QGameDeltaTime();
    for(auto it=drinkingSessions.begin();it!=drinkingSessions.end();){
        auto& session=it->second;auto* geometry=session.owner.get();
        if(!SameAttachment(static_cast<RE::NiAVObject*>(geometry),session.attachment)){
            REX::INFO("IAF drain ended: geometry={} remaining={}",static_cast<void*>(geometry),session.liquid.remaining);
            instances.erase(geometry);distanceLods.erase(geometry);it=drinkingSessions.erase(it);continue;
        }
        auto v=WorldToMeshDirection(geometry->world.rotate,{0,0,1});auto& m=session.canonical;
        auto up=Normalize(Vec{m[0]*v.x+m[1]*v.y+m[2]*v.z,m[4]*v.x+m[5]*v.y+m[6]*v.z,m[8]*v.x+m[9]*v.y+m[10]*v.z});
        session.liquid.Update(up.z,dt);++it;
    }
}
DrinkSessionEntry* RegisterDrinkingSession(RE::BSGeometry* geometry,const Settings& profile){
    if(!IsDrinkingProp(geometry->name.c_str()?geometry->name.c_str():""))return nullptr;
    auto found=drinkingSessions.find(geometry);if(found!=drinkingSessions.end())return &found->second;
    auto chain=Attachment(geometry);if(chain.empty())return nullptr;
    auto& entry=drinkingSessions[geometry];entry.owner=geometry;entry.attachment=std::move(chain);
    entry.liquid=nuka::DrinkingSession(profile.fill);
    std::copy_n(profile.base.matrix,16,entry.canonical.begin());
    REX::INFO("IAF drain started: geometry={} shape={} fill={} ancestors={}",static_cast<void*>(geometry),geometry->name.c_str(),profile.fill,entry.attachment.size());
    return &entry;
}
bool Match(RE::BSRenderPass* pass){
    if(diagnostics&&pass&&pass->geometry){
        auto* g=pass->geometry;std::string name=g->name.c_str()?g->name.c_str():"";
        std::string lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(lower.find("bottle")!=std::string::npos||lower.find("beer")!=std::string::npos||lower.find("glass")!=std::string::npos){
            static std::unordered_map<std::string,bool> seen;
            std::string key=name+std::to_string(pass->passEnum);
            if(seen.size()<256&&!seen.contains(key)){
                seen.emplace(key,true);auto it=drawDrinks->find(BottleShapeName(name));auto* tri=g->IsTriShape();
                REX::INFO("Bottle candidate name={} pass={:08X} tagged={} verts={}/{} indices={}/{} descriptor={:016X} parent={} property={} instanced={} debris={}",name,pass->passEnum,it!=drawDrinks->end(),tri?tri->numVertices:0,it!=drawDrinks->end()?it->second.vertices:0,tri?tri->numTriangles*3:0,it!=drawDrinks->end()?it->second.indices:0,g->vertexDesc.desc,bool(g->parent),bool(pass->shaderProperty),bool(pass->instanceGroupPass),bool(pass->debrisInstanceGroupPass));
            }
        }
    }
    if(!pass||!pass->geometry||!pass->shaderProperty||pass->instanceGroupPass||pass->debrisInstanceGroupPass)return false;
    auto* g=pass->geometry;
    auto it=drawDrinks->find(BottleShapeName(g->name.c_str()?g->name.c_str():""));if(it==drawDrinks->end())return false;
    auto* tri=g->IsTriShape();
    if(!tri||!FindProfile(*drawDrinks,g->name.c_str(),tri->numVertices,tri->numTriangles*3)||g->vertexDesc.desc!=0x1b00000430205ull)return false;
    if(!g->parent)return false;
    const auto d=pass->passEnum;
    // These Effect variants have ModelView directly after WorldViewProj in VS b2.
    constexpr unsigned required=(1u<<4)|(1u<<19)|(1u<<21);
    constexpr unsigned excluded=(1u<<1)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<18)|(1u<<29);
    return (d&required)&&!(d&excluded);
}
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Vec Canonical(Vec v){const auto* m=activeDrink->settings.base.matrix;return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[4]*v.x+m[5]*v.y+m[6]*v.z,m[8]*v.x+m[9]*v.y+m[10]*v.z};}
const Constants& Update(RE::BSGeometry* g,bool detail,float pixels,bool held,const CarryFrame& carry,float motionWeight,std::optional<float> weaponFill={},bool shotSampleValid=false){
    std::optional<Settings> drainingSettings;
    if(auto it=drinkingSessions.find(g);it!=drinkingSessions.end()){
        drainingSettings=activeDrink->settings;drainingSettings->fill=it->second.liquid.remaining;
    }
    if(weaponFill){drainingSettings=activeDrink->settings;drainingSettings->fill=*weaponFill;drainingSettings->shotBubbles=true;drainingSettings->shotConfig=acidShotConfig;drainingSettings->radiusMin=std::min(acidShotConfig.bigRadiusMin,acidShotConfig.smallRadiusMin);drainingSettings->radiusMax=std::max(acidShotConfig.bigRadiusMax,acidShotConfig.smallRadiusMax);}
    const auto& settings=drainingSettings?*drainingSettings:activeDrink->settings;
    const double now=Now();
    if(resetRequested.exchange(false)){instances.clear();distanceLods.clear();}
    CleanInstances(now);
    auto frame=RE::BSGraphics::State::GetSingleton()->frameCount;
    AllocateBubblesForFrame(frame);
    if(!instances.contains(g)&&instances.size()>=128)instances.erase(instances.begin());
    auto& i=instances[g];
    if(i.selectedProfile!=activeDrink){i=Instance{};i.selectedProfile=activeDrink;}
    i.bubbleRequest={reinterpret_cast<std::uintptr_t>(g),pixels,
        carry.valid?Length(Vec{g->world.translate.x,g->world.translate.y,g->world.translate.z}-carry.eye):768.f,held,
        {detail&&settings.fill>0&&settings.fill<1&&settings.shakingBubbles>0?64u:0u,
         detail&&settings.fill>0&&settings.fill<1&&settings.foamStrength>0&&settings.intensity>0?64u:0u,
         detail&&settings.fill>0?(settings.shotBubbles?24u:(settings.intensity>0?128u:0u)):0u},{0,0,0}};
    if(i.owner&&i.frame==frame)return i.output;
    const auto& world=g->world;Vec position{world.translate.x,world.translate.y,world.translate.z};
    auto up=Normalize(Canonical(WorldToMeshDirection(world.rotate,{0,0,1})));float dt=float(now-i.time);
    auto shakeAxis=Normalize(Canonical(WorldToMeshDirection(world.rotate,{1,0,0})));
    bool frozen=TuningVisible();
    bool reset=!i.owner||(!frozen&&(dt>.25f||Length(position-i.position)>256));if(frozen)dt=0;
    bool moved=reset||Length(position-i.position)>.25f||Length(up-i.motion.lastUp)>.015f||
        (settings.shakingBubbles>0&&Length(shakeAxis-i.shakeAxis)>.015f);
    float interval=SimulationInterval(pixels,moved);
    i.raySteps=RayQuality(pixels,i.raySteps);
    if(reset){i.motion=Motion{};i.motion.lastUp=up;i.velocity={};i.shakeAxis=shakeAxis;i.emissionTracker={};dt=0;
        auto seed=reinterpret_cast<std::uintptr_t>(g);i.motion.shakeRandom.seed(unsigned(seed^(seed>>32)));}
    ApplyBubbleLimits(i.motion,i.bubbleQuota);
    if(settings.shotBubbles){
        if(reset)i.shotTracker.Sample({});
        const auto count=i.shotTracker.Sample(shotSampleValid&&activeDrink->settings.fill>0?std::optional<float>(settings.fill/activeDrink->settings.fill):std::nullopt);
        i.motion.pendingShots=detail?count:0;
        if(count&&diagnostics)REX::INFO("Acid Soaker shot bubbles: roundsConsumed={} quota={} fill={} detail={}",count,i.bubbleQuota.carbonation,settings.fill,detail);
    }
    bool changedWeight=motionWeight!=i.motionWeight;
    const bool changedWeaponFill=weaponFill&&settings.fill!=i.lastWeaponFill;
    const FrozenSimulationKey frozenKey{{position.x,position.y,position.z,
        up.x,up.y,up.z,shakeAxis.x,shakeAxis.y,shakeAxis.z,world.scale},settings.fill,motionWeight,detail};
    const bool reuseFrozen=i.frozenCache.Reuse(frozen,held,frozenKey);
    if(i.motion.pendingShots||changedWeaponFill||i.bubbleBudgetDirty||(!reuseFrozen&&(frozen||reset||detail!=i.detail||changedWeight||interval==0||dt>=interval))){
        Vec acceleration{};
        i.motion.shakeOmega=!reset?ShakeRotation(i.shakeAxis,i.motion.lastUp,shakeAxis,up,dt):Vec{};
        i.motion.shakeAngularSpeed=Length(i.motion.shakeOmega);
        // Only emission uses the carrying frame. World-space acceleration,
        // physical slosh and corrected rendering transforms stay untouched.
        Vec emissionX=shakeAxis,emissionUp=up,emissionPosition=position,emissionVertical{0,0,1};bool emissionFrame=true;
        if(held){
            emissionFrame=carry.valid;
            if(carry.valid){
                const auto& r=carry.rotation;
                emissionX=Normalize(Canonical(WorldToMeshDirection(world.rotate,{r[0].x,r[0].y,r[0].z})));
                emissionUp=Normalize(Canonical(WorldToMeshDirection(world.rotate,{r[2].x,r[2].y,r[2].z})));
                emissionPosition=CarryPosition(carry,position);
                emissionVertical=WorldToMeshDirection(r,{0,0,1});
            }
        }
        emissionFrame=emissionFrame&&std::isfinite(emissionX.x+emissionX.y+emissionX.z+
            emissionUp.x+emissionUp.y+emissionUp.z+emissionPosition.x+emissionPosition.y+emissionPosition.z);
        float metricScale=Length(Canonical(WorldToMeshDirection(world.rotate,{0,0,1})))/std::max(std::abs(world.scale),.0001f);
        i.emissionTracker.Sample(i.motion,emissionX,emissionUp,emissionPosition,emissionVertical,
            metricScale,dt,held,emissionFrame&&!reset,held?carry.identity:nullptr,held?carry.originIdentity:nullptr);
        if(!reset&&dt>.00001f){Vec velocity=(position-i.position)*(1/dt);acceleration=Canonical(WorldToMeshDirection(world.rotate,(velocity-i.velocity)*(1/dt)))*(1/std::max(std::abs(world.scale),.0001f));i.velocity=velocity;}
        i.previous=i.constants;
        // Account for the complete elapsed interval despite Simulate's 50ms clamp.
        int steps=std::max(1,int(std::ceil(dt/.05f)));
        SimulationTimings simulationTimings{};
        for(int step=0;step<steps;step++){
            i.constants=Simulate(settings,i.motion,acceleration,up,dt/steps,detail,held,motionWeight,diagnostics?&simulationTimings:nullptr);
            if(diagnostics&&held){
                i.logBirths+=i.motion.shakingBirths;i.logTriggerSources|=i.motion.triggerSources;
                i.logAngularPeak=std::max(i.logAngularPeak,Length(i.motion.emissionOmega));
                i.logVerticalPeak=std::max(i.logVerticalPeak,std::abs(i.motion.emissionVerticalSpeed)/std::max(settings.base.rings[settings.base.ringCount-1].x-settings.base.rings[0].x,.001f));
                i.logSizePeak=std::max(i.logSizePeak,i.motion.triggerSizeSpeed);
            }
        }
        if(diagnostics){Perf().SetDrawInfo(frame,0);Perf().AddSimulationStages(simulationTimings);}
        if(diagnostics&&held&&now>=i.nextTriggerLog){i.nextTriggerLog=now+1;
            REX::INFO("Bubble trigger .33 {}: frame=PlayerCamera.cameraRoot valid={} carryDistance={} angularPeakDegS={} verticalPeakHeightsS={} sizePeakDegS={} sources={} births={} currentAngularDegS={} currentVerticalHeightsS={} verticalPeakAge={}",
                g->name.c_str(),carry.valid,carry.valid?Length(position-carry.eye):-1.f,
                i.logAngularPeak*57.2957795f,i.logVerticalPeak,i.logSizePeak*57.2957795f,i.logTriggerSources,i.logBirths,
                Length(i.motion.emissionOmega)*57.2957795f,i.motion.emissionVerticalSpeed/std::max(settings.base.rings[settings.base.ringCount-1].x-settings.base.rings[0].x,.001f),i.motion.verticalPeakAge);
            i.logAngularPeak=i.logVerticalPeak=i.logSizePeak=0;i.logTriggerSources=i.logBirths=0;
        }
        if(changedWeaponFill||reset||detail!=i.detail||changedWeight||interval==0||i.bubbleBudgetDirty)i.previous=i.constants;
        if(weaponFill)i.lastWeaponFill=settings.fill;
        i.bubbleBudgetDirty=false;
        i.motionWeight=motionWeight;
        i.position=position;i.shakeAxis=shakeAxis;i.time=now;i.detail=detail;
    }
    // A cache hit is still a live observation: do not evict it after two
    // seconds or accumulate paused time into the first resumed update.
    if(frozen){i.time=now;i.previous=i.constants;}
    if(diagnostics&&now>=i.nextFoamLog){i.nextFoamLog=now+5;
        REX::INFO("Bubble lifecycle shape={} foamSetting={} carbonation={} fill={} detail={} shaking={} foam={} retainedTotal={} fitRejectedTotal={} timedOutTotal={} wallBlocked={} shakeTransferred={} surfacePopped={}",g->name.c_str(),settings.foamStrength,settings.intensity,settings.fill,detail,i.motion.shaking.size(),i.motion.surfaceBubbles.size(),i.motion.foamRetained,i.motion.foamFitRejected,i.motion.shakeTimedOut,i.motion.shakeWallBlocked,i.motion.shakeTransferred,i.motion.shakeSurfacePopped);
        REX::INFO("Carbonation lifecycle shape={} live={} transfer={} expiry={} tiny={} invalid={} budget={} cleared={} containment={} (cumulative per instance)",g->name.c_str(),i.motion.bubbles.size(),i.motion.carbTransfer,i.motion.carbExpiry,i.motion.carbTiny,i.motion.carbInvalid,i.motion.carbBudget,i.motion.carbCleared,i.motion.carbContain);
    }
    i.interval=interval;i.output=interval>0&&!frozen?InterpolateSimulation(i.previous,i.constants,float(now-i.time)/interval):i.constants;
    i.output.wave.w=float(i.raySteps);i.frame=frame;i.owner=g;return i.output;

}

void PrepareSunShadow(Constants& c,RE::BSGeometry* geometry,Vec offset){
    renderer.SetSunShadow(nullptr);c.sunInfo={};
    auto pos=geometry->world.translate;pos.x+=offset.x;pos.y+=offset.y;pos.z+=offset.z;c.worldOrigin={pos.x,pos.y,pos.z,geometry->world.scale};
    auto* state=RE::BSShaderManager::State::GetSingleton();auto* scene=state?state->GetShadowSceneNode(0):nullptr;
    auto* light=scene?scene->directionalShadowLight:nullptr;
    auto* manager=RE::BSGraphics::RenderTargetManager::GetSingleton();auto* data=RE::BSGraphics::GetRendererData();
    if(!light||!light->shadowMapData||!manager||!data||light->shadowMapCount<1||light->shadowMapCount>4)return;
    auto target=light->shadowMapData[0].depthStencilTarget;if(target>=12)return;
    auto slot=manager->GetDepthStencilTargetPlatformID(target);if(slot>=std::size(data->depthStencilTargets))return;
    auto* view=reinterpret_cast<ID3D11ShaderResourceView*>(data->depthStencilTargets[slot].srViewDepth);if(!view)return;
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> tex;if(FAILED(resource.As(&tex)))return;
    D3D11_TEXTURE2D_DESC desc{};tex->GetDesc(&desc);if(desc.SampleDesc.Count!=1)return;
    float* slices=&c.sunSlices.x;
    for(unsigned i=0;i<light->shadowMapCount;i++){
        auto& entry=light->shadowMapData[i];if(entry.depthStencilTarget!=target||entry.arraySlice>=desc.ArraySize)return;
        for(unsigned j=0;j<16;j++){float v=reinterpret_cast<const float*>(entry.worldToShadow)[j];if(!std::isfinite(v))return;c.sunMatrices[i][j]=v;}
        c.sunBounds[i]=SunShadowBounds(entry.viewport,desc.Width,desc.Height);
        slices[i]=float(entry.arraySlice);
    }
    c.sunInfo={float(light->shadowMapCount),.0001f,float(desc.Width),float(desc.Height)};renderer.SetSunShadow(view);
    static bool logged=false;if(!logged){logged=true;REX::INFO("Native sun shadows: {} cascades, {}x{}, {} slices, target {}",light->shadowMapCount,desc.Width,desc.Height,desc.ArraySize,target);
        for(unsigned i=0;i<light->shadowMapCount;i++){
            auto* m=c.sunMatrices[i];float q[4]{};for(int j=0;j<4;j++)q[j]=pos.x*m[j]+pos.y*m[4+j]+pos.z*m[8+j]+m[12+j];
            auto b=c.sunBounds[i];REX::INFO("  sun cascade {} center UVZ/W=({}, {}, {}, {}) bounds=({}, {}, {}, {}) slice={}",i,q[0],q[1],q[2],q[3],b.x,b.y,b.z,b.w,slices[i]);
        }
    }
}
// Evaluate actual engine illumination, never the Effect material's PropertyColor.
V4 IncidentLight(RE::BSRenderPass* pass,Constants& c,Vec offset,const RE::NiAVObject* firstRoot){
    for(int i=0;i<4;i++){c.surfaceDirection[i]={};c.surfaceLight[i]={};}
    auto surface=[&](Vec direction,Vec rgb,bool isSun=false){
        float power=std::max({rgb.x,rgb.y,rgb.z});
        if(!std::isfinite(power)||power<=0||Length(direction)<1e-5f)return;
        direction=Normalize(direction);
        for(int i=0;i<4;i++)if(power>c.surfaceLight[i].w){
            for(int j=3;j>i;j--){c.surfaceLight[j]=c.surfaceLight[j-1];c.surfaceDirection[j]=c.surfaceDirection[j-1];}
            c.surfaceDirection[i]={direction.x,direction.y,direction.z,isSun?1.f:0.f};
            c.surfaceLight[i]={rgb.x,rgb.y,rgb.z,power};break;
        }
    };
    Vec sum{},ambient{},sunContribution{};
    auto linear=[](float x){return std::isfinite(x)?std::pow(std::max(x,0.f),2.2f):0.f;};
    if(auto* state=RE::BSShaderManager::State::GetSingleton()){
        const auto& a=state->GetDirectionalAmbientTransform();
        // Six-direction average approximates illumination of the liquid volume.
        for(int axis=0;axis<3;axis++)for(float sign:{-1.f,1.f}){
            const auto& r=a.rotate[axis];
            sum=sum+Vec{linear(a.translate.x+sign*r.x*a.scale),linear(a.translate.y+sign*r.y*a.scale),linear(a.translate.z+sign*r.z*a.scale)}*(1.f/6);
        }
    }
    ambient=sum;
    RE::NiLight* sun=nullptr;
    auto* sky=RE::Sky::GetSingleton();
    if(sky&&sky->mode==RE::Sky::Mode::kFull&&sky->sun&&sky->sun->light){
        sun=reinterpret_cast<RE::NiLight*>(sky->sun->light.get());
        // Volume-average sunlight, separate from surface/cubemap highlights.
        float scale=std::max(sun->dimmer,0.f)*.25f;
        if(auto* image=RE::ImageSpaceManager::GetSingleton())scale*=std::max(image->currentEOFData.baseData.hdrData.sunlightScale,0.f);
        sum=sum+Vec{linear(sun->diff.r),linear(sun->diff.g),linear(sun->diff.b)}*scale;
    }
    sunContribution=sum-ambient;
    c.sunVolume={sunContribution.x,sunContribution.y,sunContribution.z,0};
    if(sun){auto d=sky->sun->light->direction;surface({-d.x,-d.y,-d.z},sunContribution*4.f,true);}
    ForEachAssignedLight(pass->sceneLights,pass->numLights,[&](RE::BSLight* entry){
        if(!entry->light||entry->light.get()==sun||entry->shape==0)return;
        auto* l=entry->light.get();float radius=l->radius.x;if(!std::isfinite(radius)||radius<=0)return;
        auto d=pass->geometry->worldBound.center-l->world.translate;
        // Attached lights share the first-person origin; world lights do not.
        if(!Under(l,firstRoot)){d.x+=offset.x;d.y+=offset.y;d.z+=offset.z;}
        float distance=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z)/radius;
        float fade=std::pow(std::max(0.f,1-distance*distance),2.2f)*std::max(l->dimmer,0.f)*std::max(entry->currentFade,0.f);
        auto rgb=Vec{linear(l->diff.r),linear(l->diff.g),linear(l->diff.b)}*fade;
        sum=sum+rgb;surface({-d.x,-d.y,-d.z},rgb);
    });
    auto safe=[](float v){return std::isfinite(v)?std::clamp(v,0.f,10000.f):0.f;};
    V4 result{safe(sum.x),safe(sum.y),safe(sum.z),0};
    static unsigned events=0;
    if(!diagnostics&&events>=3)return result;
    // Optional incident-light snapshots for in-game diagnosis.
    struct Snapshot {Vec rgb{};double time{};unsigned descriptor{},count{};bool valid{};};
    static std::unordered_map<RE::BSGeometry*,Snapshot> snapshots;
    if(snapshots.size()>128)snapshots.clear();
    auto& previous=snapshots[pass->geometry];double now=Now();
    Vec rgb{result.x,result.y,result.z};
    bool changed=!previous.valid||Length(rgb-previous.rgb)>.2f*std::max(Length(previous.rgb),.01f)||previous.descriptor!=pass->passEnum||previous.count!=pass->numLights;
    if(events<3&&(!previous.valid||((changed||now-previous.time>=2)&&now-previous.time>=.1))){
        auto pos=pass->geometry->worldBound.center;
        REX::INFO("Lighting snapshot #{} geometry={:X} pass={:X} frame={} position=({}, {}, {}) RGB=({}, {}, {}) ambient=({}, {}, {}) sun=({}, {}, {}) count={} traversed={} emission=({}, {}, {})",
            ++events,reinterpret_cast<std::uintptr_t>(pass->geometry),pass->passEnum,RE::BSGraphics::State::GetSingleton()->frameCount,pos.x,pos.y,pos.z,
            rgb.x,rgb.y,rgb.z,ambient.x,ambient.y,ambient.z,sunContribution.x,sunContribution.y,sunContribution.z,pass->numLights,unsigned(pass->numLights),settings.base.emission.x,settings.base.emission.y,settings.base.emission.z);
        if(pass->sceneLights)for(unsigned k=0;k<std::min<unsigned>(pass->numLights,16);k++){
            auto* entry=pass->sceneLights[k];if(!entry||!entry->light){REX::INFO("  light[{}] null",k);continue;}
            auto* l=entry->light.get();auto d=pos-l->world.translate;float distance=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
            float radius=l->radius.x;
            float fade=std::isfinite(radius)&&radius>0?std::pow(std::max(0.f,1-distance*distance/(radius*radius)),2.2f)*std::max(l->dimmer,0.f)*std::max(entry->currentFade,0.f):0;
            REX::INFO("  light[{}] id={:X} shape={} sun={} included={} radius={} distance={} dimmer={} currentFade={} diffuse=({}, {}, {}) calculatedRGB=({}, {}, {})",
                k,reinterpret_cast<std::uintptr_t>(entry),entry->shape,l==sun,l!=sun&&entry->shape!=0,radius,distance,l->dimmer,entry->currentFade,l->diff.r,l->diff.g,l->diff.b,linear(l->diff.r)*fade,linear(l->diff.g)*fade,linear(l->diff.b)*fade);
        }
        previous={rgb,now,pass->passEnum,pass->numLights,true};
        if(events==300)REX::INFO("Lighting diagnostic limit reached; restart game for another capture.");
    }
    return result;
}
void LogSurfaceBindings(ID3D11DeviceContext* ctx){
    static std::unordered_map<const void*,bool> enbNames;
    if(enbNames.size()<128&&enbNames.emplace(active->geometry,true).second)REX::INFO("ENB identity geometry={} shape={} pass={:X} material={}",static_cast<void*>(active->geometry),active->geometry->name.c_str(),active->passEnum,static_cast<void*>(active->shaderProperty->material));
    if(!diagnostics)return;
    // One entry per profile, rather than one global success hiding other materials.
    static std::unordered_map<std::string,unsigned> logged;
    std::string name=active->geometry->name.c_str();
    if(logged.contains(name))return;
    logged.emplace(name,1);
    auto* material=active->shaderProperty->material;
    if(!material||material->GetType()!=RE::BSShaderMaterial::Type::kEffect){
        REX::WARN("Surface material {}: no Effect material",name);return;
    }
    auto* m=static_cast<RE::BSEffectShaderMaterial*>(material);
    auto textureName=[](const RE::NiPointer<RE::NiTexture>& t){return t?std::string(t->GetName()):std::string("<missing>");};
    REX::INFO("Surface material {}: base={} cube={} slot70={} slot78={} cubeScale={} lightingInfluence={} minLOD={}",name,textureName(m->baseTexture),textureName(m->environmentTexture),textureName(m->environmentMaskTexture),textureName(m->texture78),m->environmentMaskScaleOrRefractionPower,m->lightingInfluence/255.f,unsigned(m->environmentMinLOD));
    const auto& response=activeDrink->settings.base;
    REX::INFO("Surface metadata (legacy cubemap gain ignored) {}: family={} authoredCubeScale={} specular=({}, {}, {}) multiplier={} smoothness={} fresnel={}",name,response.surfaceResponse.z>.5f?"BGSM dielectric adapter":"authored Effect",response.surfaceResponse.z>.5f?response.surfaceResponse.w:m->environmentMaskScaleOrRefractionPower,response.surfaceSpecular.x,response.surfaceSpecular.y,response.surfaceSpecular.z,response.surfaceSpecular.w,response.surfaceResponse.x,response.surfaceResponse.y);
    for(UINT slot:{0u,1u,5u,7u}){
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;ctx->PSGetShaderResources(slot,1,&view);
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};if(view)view->GetDesc(&vd);
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;if(view)view->GetResource(&resource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;D3D11_TEXTURE2D_DESC td{};
        if(resource&&SUCCEEDED(resource.As(&tex)))tex->GetDesc(&td);
        REX::INFO("Surface binding {} t{}: present={} dimension={} format={} size={}x{}",name,slot,bool(view),unsigned(vd.ViewDimension),unsigned(vd.Format),td.Width,td.Height);
    }
}
thread_local bool sunsetFrost=false;
void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base){
    if(sunsetFrost && ctx==hookedContext && !NativeOptics::busy){
        // Keep intercepted ice shaders on the original context. Transferring them
        // to a deferred context crashes in VSSetShader with the ENB chain.
        originalDraw(ctx,count,start,base);
        return;
    }
    if(NativeOptics::busy){originalDraw(ctx,count,start,base);return;}
    if(ctx==hookedContext&&registeredGeometry){try{Native().DrawRegistered(ctx,registeredGeometry,count,start,base,originalDraw);}catch(const std::exception& e){Native().Fail(e.what());}return;}


    if(ctx==hookedContext&&active){
        std::lock_guard lock(renderMutex);
        try {
            double cpuStart=Now();
            LogSurfaceBindings(ctx);
            auto* graphics=RE::BSGraphics::State::GetSingleton();
            auto* camera=graphics->cameraState.referenceCamera;
            bool detail=true,effects=true;
            static RE::NiPoint3 cameraWorld{};static unsigned cameraFrame=~0u;static bool havePosition=false;
            static CarryFrame playerCarry;
            if(cameraFrame!=graphics->frameCount){cameraFrame=graphics->frameCount;auto* playerCamera=RE::PlayerCamera::GetSingleton();
                havePosition=playerCamera&&playerCamera->GetCameraPosition(cameraWorld,true)&&std::isfinite(cameraWorld.x)&&std::isfinite(cameraWorld.y)&&std::isfinite(cameraWorld.z);
                const auto* viewRoot=playerCamera?playerCamera->cameraRoot.get():nullptr;
                playerCarry=MakeCarryFrame(viewRoot?&viewRoot->world.rotate:nullptr,{cameraWorld.x,cameraWorld.y,cameraWorld.z},
                    playerCamera?playerCamera->currentState.get():nullptr,havePosition);
            }
            auto bottleWorld=active->geometry->world.translate;
            Vec bottle{bottleWorld.x,bottleWorld.y,bottleWorld.z},eye{cameraWorld.x,cameraWorld.y,cameraWorld.z};
            auto* player=RE::PlayerCharacter::GetSingleton();
            auto* firstRoot=player?player->firstPerson3D.get():nullptr;
            bool firstPerson=Under(active->geometry,firstRoot);
            Vec offset{};bool localEyeValid=havePosition;
            if(firstPerson){
                auto* anchor=player->firstPersonEye;
                localEyeValid=havePosition&&anchor;
                if(localEyeValid){auto p=anchor->world.translate;eye={p.x,p.y,p.z};localEyeValid=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
                if(localEyeValid)offset=AnimationWorldOffset({cameraWorld.x,cameraWorld.y,cameraWorld.z},eye);
            }
            if(diagnostics){
                static std::unordered_map<std::string,double> spaceLogs;
                std::string name=active->geometry->name.c_str();
                if(name.find(":iafanimobject")!=std::string::npos&&cpuStart-spaceLogs[name]>5){
                    spaceLogs[name]=cpuStart;
                    REX::INFO("Animation space {}: firstPerson={} anchorValid={} eye=({}, {}, {}) origin=({}, {}, {}) distance={} worldOffset=({}, {}, {})",name,firstPerson,localEyeValid,eye.x,eye.y,eye.z,bottle.x,bottle.y,bottle.z,localEyeValid?Length(bottle-eye):-1.f,offset.x,offset.y,offset.z);
                }
            }
            CleanInstances(cpuStart);
            if(!distanceLods.contains(active->geometry)&&distanceLods.size()>=256)distanceLods.erase(distanceLods.begin());
            auto& entry=distanceLods[active->geometry];entry.owner=active->geometry;
            // Simulation detail keeps its own distance limits; it does not fade opacity.
            entry.state.Update(localEyeValid?Length(bottle-eye):-1.f,cpuStart);
            const auto& motionRotation=active->geometry->world.rotate;
            Vec axes[3]{{motionRotation[0].x,motionRotation[0].y,motionRotation[0].z},{motionRotation[1].x,motionRotation[1].y,motionRotation[1].z},{motionRotation[2].x,motionRotation[2].y,motionRotation[2].z}};
            auto grabbed=player?player->grabbedObject.get():RE::NiPointer<RE::TESObjectREFR>{};
            bool held=firstPerson||(grabbed&&Under(active->geometry,grabbed->Get3D()))||std::string_view(active->geometry->name.c_str()).starts_with("DynamicBottleLiquids:iafanimobject");
            bool shieldActive=entry.shieldMotion.Update(bottle,axes,cpuStart,held)&&entry.state.effectWeight>0;
            if(held)TuningTarget(activeDrink->meshName);
            auto lod=entry.state;detail=lod.detailWeight>0;effects=lod.effectWeight>0;
            static double nextDistanceLog=0;
            if(diagnostics&&cpuStart>=nextDistanceLog){nextDistanceLog=cpuStart+5;
                REX::INFO("Detail distance: valid={} units={} cutoff={} enabled={} fade={} bottle=({}, {}, {}) cameraWorld=({}, {}, {}) referenceCamera=({}, {}, {})",localEyeValid,localEyeValid?Length(bottle-eye):-1.f,256.f,detail,lod.detailWeight,bottle.x,bottle.y,bottle.z,eye.x,eye.y,eye.z,camera?camera->world.translate.x:0.f,camera?camera->world.translate.y:0.f,camera?camera->world.translate.z:0.f);
            }
            // Shared static fallback: no per-instance simulation beyond the normal distance effect range.
            const auto& distantBase=activeDrink->distant;
            float pixels=1e6f;
            if(effects&&!activeDrink->empty&&camera&&localEyeValid&&!camera->viewFrustum.ortho){
                D3D11_VIEWPORT viewport{};UINT n=1;ctx->RSGetViewports(&n,&viewport);
                auto bound=active->geometry->worldBound;auto& f=camera->viewFrustum;
                pixels=ProjectedDiameter(bound.fRadius,Length(Vec{bound.center.x,bound.center.y,bound.center.z}-eye),f.top-f.bottom,f.right-f.left,viewport.Width,viewport.Height);
            }
            double simulationStart=diagnostics?Now():0;
            // Empty glass uses the near profile, not the distant constants whose
            // refraction has already been disabled by ApplyDetailDistance.
            auto carry=playerCarry;carry.eye=eye;carry.valid=carry.valid&&localEyeValid;
            // A first-person skeleton and a world-space grabbed object must not
            // share derivative history when the coordinate origin changes.
            if(firstPerson)carry.originIdentity=firstRoot;
            auto* drinkSession=activeDrink->empty?nullptr:RegisterDrinkingSession(active->geometry,activeDrink->settings);
            bool shotSampleValid=false;
            const auto weaponFill=AcidSoakerFill(active->geometry,activeDrink->settings.fill,&shotSampleValid);
            const bool staticFeed=std::string_view(active->geometry->name.c_str())=="DynamicBottleLiquids:acidsoaker-feed";
            auto constants=staticFeed?distantBase:effects?(activeDrink->empty?activeDrink->settings.base:Update(active->geometry,detail,pixels,held,carry,lod.detailWeight,weaponFill,shotSampleValid)):distantBase;
            if(!staticFeed&&!effects&&!activeDrink->empty){
                auto gravity=Normalize(Canonical(WorldToMeshDirection(active->geometry->world.rotate,{0,0,1})));
                if(weaponFill){auto depleted=activeDrink->settings;depleted.fill=*weaponFill;constants.plane=entry.tilt.Get(depleted,gravity);constants.trace.y=*weaponFill;}
                else if(drinkSession){auto depleted=activeDrink->settings;depleted.fill=drinkSession->liquid.remaining;constants.plane=entry.tilt.Get(depleted,gravity);}
                else constants.plane=entry.tilt.Get(activeDrink->settings,gravity);
            }
            if(effects)ApplyDetailDistance(constants,detail);
            // A curved feed tube is a full static optical shell, not an axisymmetric
            // bottle cavity. Mode 3 uses existing lightweight liquid/glass shading
            // and is preserved by the preview shader. No simulation or flow effect.
            if(staticFeed){constants.optics.w=3;constants.trace.y=1;constants.wave.x=0;constants.foam=0;constants.bubbleCount=0;}
            if(staticFeed||!effects||activeDrink->empty)instances.erase(active->geometry); // Empty glass needs no motion/volume solver.

            if(IsColdBottle(active->geometry)&&constants.ringCount>1){
                const float bottom=constants.rings[0].x;
                const float span=std::max(constants.rings[constants.ringCount-1].x-bottom,.001f);
                constants.wetInfo={1,bottom,1/span,1};
                constants.wetTuning.x=0;
                constants.wetTuning.y=.85f;
            }
            if(diagnostics){static double nextWetLog=0;if(cpuStart>=nextWetLog){nextWetLog=cpuStart+5;REX::INFO("Wet film: exposed={} detailFade={} held={}",constants.wetInfo.x,lod.detailWeight,held);}}
            constants.trace.z*=lod.detailWeight;
            constants.padding[1]=1-lod.detailWeight;
            constants.scatter.w*=lod.detailWeight;
            int simulatedBubbles=constants.bubbleCount;
            {
                FadeBubbles(constants,pixels,lod.detailWeight,held);
                PrepareBubbleGroups(constants);
            }
            constants.padding[0]=1-lod.effectWeight;
            // Motion is faded before the volume solve; gravity tilt is never blended
            // toward bottle-local upright. The direct route uses the cached plane.
            double simulationMs=diagnostics?(Now()-simulationStart)*1000:0;
            double lightingStart=diagnostics?Now():0;
            {
                constants.eye=IncidentLight(active,constants,offset,firstPerson?firstRoot:nullptr);
                // Read the live ordinary Effect material; external BGEM and embedded
                // NIF materials share this layout. Do not change flags or patch assets.
                const float reflectionStrength=constants.surfaceEnvironment.w*((weaponFill.has_value()||staticFeed)?2.f:1.f);
                constants.surfaceEnvironment={0,1,0,0};
                auto* material=active->shaderProperty->material;
                if(material&&material->GetType()==RE::BSShaderMaterial::Type::kEffect&&
                   active->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEnvMap)){
                    auto* effect=static_cast<RE::BSEffectShaderMaterial*>(material);
                    // Material enable/cubemap binding remain native; gain is shared.
                    constants.surfaceEnvironment={reflectionStrength,0,0,0};
                    constants.surfaceAlpha={std::isfinite(effect->baseColor.a)?std::clamp(effect->baseColor.a,0.f,1.f):1.f,
                        active->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kFalloff)?1.f:0.f,0,0};
                    auto* falloff=&constants.surfaceFalloff.x;
                    for(int i=0;i<4;i++)falloff[i]=std::isfinite(effect->falloffData[i])?effect->falloffData[i]:(i<2?float(i):1.f);
                }
                PrepareSunShadow(constants,active->geometry,offset);
                if(firstPerson&&!localEyeValid){constants.sunInfo={};renderer.SetSunShadow(nullptr);}
            }
            double lightingMs=diagnostics?(Now()-lightingStart)*1000:0;
            if(diagnostics)constants.debug=lightingProbe.load()?9+lightingProbe.load():0;
            const auto& rotation=active->geometry->world.rotate;
            constants.meshToWorld[0]={rotation[0].x,rotation[1].x,rotation[2].x,0};
            constants.meshToWorld[1]={rotation[0].y,rotation[1].y,rotation[2].y,0};
            constants.meshToWorld[2]={rotation[0].z,rotation[1].z,rotation[2].z,0};
            // Do not infer the viewing ray from variant-dependent Effect b2.
            // World draws use the true camera. First-person draws derive the exact
            // mesh-space eye from their native ModelView in VSMain (padding[2]=0).
            float scale=active->geometry->world.scale;
            if(!firstPerson&&havePosition&&std::isfinite(scale)&&std::abs(scale)>.00001f){
                auto meshEye=WorldToMeshDirection(rotation,eye-bottle)*(1.f/scale);
                constants.meshToWorld[0].w=meshEye.x;constants.meshToWorld[1].w=meshEye.y;constants.meshToWorld[2].w=meshEye.z;
                constants.padding[2]=1;
            }else constants.padding[2]=0;
            // Keep the constant-buffer layout; retired fog fade is always zero.
            constants.surfaceAlpha.z=localEyeValid?Length(bottle-eye):-1.f;
            constants.surfaceAlpha.w=0.f;
            renderer.SetExpectedIndices(activeDrink->indices);
            renderer.SetFrame(graphics->frameCount,camera);
            if(diagnostics){Perf().SetDrawInfo(RE::BSGraphics::State::GetSingleton()->frameCount,(Now()-cpuStart)*1000);Perf().AddBubbles(simulatedBubbles,constants.bubbleCount,held);}
            if(diagnostics)Perf().AddCpuStages(simulationMs,lightingMs);
            Native().BeginFrame(graphics->frameCount);Native().activeGeometry=active->geometry;Native().firstPerson=firstPerson;
            Shield().BeginFrame(graphics->frameCount);
            Shield().SetDrawEnabled(shieldActive);
            Shield().SetScreenBounds(nullptr); // Unknown/first-person projections remain full-frame.
            if(Shield().enabled&&shieldActive&&camera&&!firstPerson&&localEyeValid&&!camera->viewFrustum.ortho&&camera->port.left==0&&camera->port.right==1&&camera->port.bottom==0&&camera->port.top==1){
                auto bound=active->geometry->worldBound;float r=bound.fRadius*1.1f+1;
                float left=1,right=0,lowY=1,highY=0;bool valid=std::isfinite(r)&&r>0;
                for(int k=0;k<8&&valid;k++){
                    RE::NiPoint3 p{bound.center.x+((k&1)?r:-r),bound.center.y+((k&2)?r:-r),bound.center.z+((k&4)?r:-r)};
                    float x,y,z;valid=camera->WorldPtToScreenPt3(p,x,y,z,.0001f)&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&z>0;
                    if(valid){left=std::min(left,x);right=std::max(right,x);lowY=std::min(lowY,y);highY=std::max(highY,y);}
                }
                D3D11_VIEWPORT vp{};UINT n=1;ctx->RSGetViewports(&n,&vp);
                // Enclose both vertical-origin conventions until live projection captures
                // establish a tighter single convention for every camera path.
                float top=std::min(lowY,1-highY),bottom=std::max(highY,1-lowY);
                if(valid&&n&&left<right&&right>0&&left<1){
                    D3D11_RECT rect{LONG(std::floor(vp.TopLeftX+std::clamp(left,0.f,1.f)*vp.Width))-4,LONG(std::floor(vp.TopLeftY+std::clamp(top,0.f,1.f)*vp.Height))-4,LONG(std::ceil(vp.TopLeftX+std::clamp(right,0.f,1.f)*vp.Width))+4,LONG(std::ceil(vp.TopLeftY+std::clamp(bottom,0.f,1.f)*vp.Height))+4};
                    Shield().SetScreenBounds(&rect);
                }
            }
            bool replaced=renderer.Draw(ctx,constants,count,start,base,originalDraw);
            Native().activeGeometry=nullptr;
            if(diagnostics&&firstPerson){
                static std::unordered_map<std::string,double> drawLogs;
                std::string name=active->geometry->name.c_str();
                if(cpuStart-drawLogs[name]>5){drawLogs[name]=cpuStart;REX::INFO("First-person draw {}: replaced={} detailWeight={} effectWeight={} nativeEye={} sunCascades={}",name,replaced,lod.detailWeight,lod.effectWeight,constants.padding[2]==0,constants.sunInfo.x);}
            }
            if(diagnostics)Perf().AddCpu((Now()-cpuStart)*1000);
            if(replaced){
                static bool first=true;if(first){first=false;REX::INFO("First bottle replacement draw submitted ({} indices); this does not confirm visible pixels.",count);}return;
            }
            static bool firstFailure=true;if(firstFailure){firstFailure=false;REX::WARN("Native optics: unsupported layout or rejected capture; retaining vanilla rendering.");}
        }catch(const std::exception& e){static bool reported=false;if(!reported){reported=true;REX::ERROR("Bottle draw failed: {}",e.what());}}
    }
    originalDraw(ctx,count,start,base);
}
void EnsureRenderer(){
    if(hookedContext)return;
    static bool attempted=false;if(attempted)return;
    auto* data=RE::BSGraphics::GetRendererData();if(!data||!data->device||!data->context)return;
    attempted=true;
    auto* device=reinterpret_cast<ID3D11Device*>(data->device);auto* ctx=reinterpret_cast<ID3D11DeviceContext*>(data->context);
    if(ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)throw std::runtime_error("Expected immediate context");
    if(diagnostics)renderer.EnablePerformance();
    renderer.Initialize(device,root/"shaders",2952);
    InitializeShield(device,ctx,root/"shaders");
    InitializeTuningRenderer(reinterpret_cast<IDXGISwapChain*>(data->renderWindow[0].swapChain));
    auto** table=*reinterpret_cast<void***>(ctx);
    if(MH_CreateHook(table[12],reinterpret_cast<void*>(&Draw),reinterpret_cast<void**>(&originalDraw))!=MH_OK)throw std::runtime_error("Cannot detour DrawIndexed");
    hookedContext=ctx;
    if(MH_EnableHook(table[12])!=MH_OK){hookedContext=nullptr;MH_RemoveHook(table[12]);throw std::runtime_error("Cannot enable DrawIndexed hook");}
    REX::INFO("D3D11 renderer initialized; glass replacement is ready.");
}
// Resolve only within this bottle's immediate parent. Never search the world or another root.
const char* LiquidSourceTexture(RE::BSRenderPass* pass,const DrinkProfile& base){
    auto* property=pass->shaderProperty;
    if(!base.materialSourceShape.empty()){
        auto* parent=pass->geometry->parent;if(!parent)return nullptr;
        RE::BSGeometry* found=nullptr;unsigned visited=0;
        for(auto& child:parent->children){
            if(++visited>64)return nullptr;
            if(!child||!child->name.c_str()||DisplaySourceName(child->name.c_str())!=base.materialSourceShape)continue;
            auto* tri=child->IsTriShape();
            if(!tri||tri->numVertices!=base.materialSourceVertices||tri->numTriangles*3!=base.materialSourceIndices||found)return nullptr;
            found=tri;
        }
        if(!found)return nullptr;
        property=static_cast<RE::BSShaderProperty*>(found->properties[1].get());
    }
    if(!property||!property->material)return nullptr;
    auto* material=property->material;
    if(material->GetType()==RE::BSShaderMaterial::Type::kEffect)return static_cast<RE::BSEffectShaderMaterial*>(material)->sourceTexturePath.c_str();
    if(material->GetType()==RE::BSShaderMaterial::Type::kLighting){
        auto* lighting=static_cast<RE::BSLightingShaderMaterialBase*>(material);
        if(lighting->diffuseTexture)return lighting->diffuseTexture->name.c_str();
    }
    return nullptr;
}
bool ExcludeStaticLiquid(RE::BSRenderPass* pass){
    if(!pass||!pass->geometry||pass->instanceGroupPass||pass->debrisInstanceGroupPass)return false;
    auto* geometry=pass->geometry;const auto name=DisplaySourceName(geometry->name.c_str()?geometry->name.c_str():"");
    // Almost all game draws stop here without taking a profile snapshot or scanning siblings.
    if(name.empty()||name[0]!='I'||!IsOppressorLiquidName(name))return false;
    auto* tri=geometry->IsTriShape();
    if(!tri||!IsOppressorStaticLiquid(name,tri->numVertices,tri->numTriangles*3)||!geometry->parent)return false;
    auto profiles=liveDrinks.load();if(!profiles)return false;
    unsigned visited=0,owners=0;
    for(auto& child:geometry->parent->children){
        if(++visited>64)return false;
        if(!child||!child->name.c_str()||BottleShapeName(child->name.c_str())!="DynamicBottleLiquids:op_nukacola")continue;
        auto* shell=child->IsTriShape();if(!shell)return false;
        auto* profile=FindProfile(*profiles,child->name.c_str(),shell->numVertices,shell->numTriangles*3);
        if(!profile||!IsOppressorLiquidOwner(BottleShapeName(child->name.c_str()),shell->numVertices,shell->numTriangles*3,
            profile->materialSourceShape,profile->materialSourceVertices,profile->materialSourceIndices))return false;
        if(++owners>1)return false;
    }
    if(owners!=1)return false;
    // Do not edit culling flags, detach geometry or alter its material. Swaps and
    // LiquidSourceTexture continue to observe the same retained sibling object.
    static std::atomic<unsigned> excluded{0};auto count=excluded.fetch_add(1,std::memory_order_relaxed)+1;
    if(count==1||count==120||count==600)REX::INFO("Static liquid draw excluded: count={} shape={} pass={:08X}; material retained",count,name,pass->passEnum);
    return true;
}
bool NativeIceShellPass(RE::BSRenderPass* pass){
    if(!pass||!pass->geometry||!pass->geometry->parent||pass->instanceGroupPass||pass->debrisInstanceGroupPass)return false;
    auto* name=pass->geometry->name.c_str();
    if(!name)return false;
    auto* shape=pass->geometry->IsTriShape();if(!shape)return false;
    std::string_view family;
    if(DisplaySourceName(name)=="DesSSSSClassicBottleIceCold"&&shape->numVertices==302&&shape->numTriangles==476)
        family="DynamicBottleLiquids:des_sunset_";
    else if(DisplaySourceName(name)=="ASANMNukaShineBottle"&&shape->numVertices==795&&shape->numTriangles==1372)
        family="DynamicBottleLiquids:des_atomic_";
    else return false;
    auto profiles=liveDrinks.load();if(!profiles)return false;
    unsigned visited=0;
    for(auto& child:pass->geometry->parent->children){
        if(++visited>64)return false;
        if(!child||!child->name.c_str()||!BottleShapeName(child->name.c_str()).starts_with(family)||!std::string_view(child->name.c_str()).ends_with("icecold"))continue;
        auto* g=child->IsGeometry();auto* tri=g?g->IsTriShape():nullptr;
        if(tri&&FindProfile(*profiles,child->name.c_str(),tri->numVertices,tri->numTriangles*3))return true;
    }
    return false;
}
void BatchDraw(RE::BSRenderPass* pass){
    TickDrinkingSessions();
    struct FrostScope {bool old=sunsetFrost;~FrostScope(){sunsetFrost=old;}} frostScope;
    sunsetFrost=NativeIceShellPass(pass);
    if(sunsetFrost)return; // Condensation replaces the native ice shell.
    if(Native().IsOwnedPass(pass)){auto* previous=registeredGeometry;registeredGeometry=pass->geometry;originalBatch(pass);registeredGeometry=previous;return;}
    if(ExcludeStaticLiquid(pass))return;
    auto* previous=active;auto* previousDrink=activeDrink;active=nullptr;activeDrink=nullptr;
    // Avoid atomic profile snapshots for unrelated geometry.
    if(!previous&&(!pass||!pass->geometry||!pass->geometry->name.c_str()||!BottleShapeName(pass->geometry->name.c_str()).starts_with("DynamicBottleLiquids:"))){originalBatch(pass);active=previous;activeDrink=previousDrink;return;}
    auto previousProfiles=drawDrinks;
    if(!previous){
        if(auto ready=pendingDrinks.exchange(nullptr)){std::lock_guard lock(renderMutex);liveDrinks.store(ready);for(auto& [geometry,instance]:instances){instance.frame=~0u;instance.selectedProfile=nullptr;instance.motion.solvedFill=-1;}REX::INFO("Live tuning applied: {} profiles",ready->size());}
        drawDrinks=liveDrinks.load();
    }
    if(Match(pass)){
        std::lock_guard lock(renderMutex);
        try {EnsureRenderer();if(hookedContext){active=pass;auto* tri=pass->geometry->IsTriShape();activeDrink=FindProfile(*drawDrinks,pass->geometry->name.c_str(),tri->numVertices,tri->numTriangles*3);if(activeDrink&&!activeDrink->materialVariants.empty()){
                auto* base=activeDrink;auto* path=LiquidSourceTexture(pass,*base);activeDrink=FindMaterialProfile(*base,path?path:"");
                if(diagnostics){static std::unordered_map<std::string,bool> reported;
                    if(reported.size()<64){std::string key=std::string(pass->geometry->name.c_str())+"|"+(path?path:"");
                        if(reported.emplace(key,true).second)REX::INFO("Material rule probe: shape={} sibling={} source={} matched={}",pass->geometry->name.c_str(),base->materialSourceShape,path?path:"",activeDrink!=base);}}
            }}}
        catch(const std::exception& e){REX::ERROR("Renderer initialization failed; keeping source glass: {}",e.what());}
        static unsigned matched=0;if(++matched==1||matched==120||matched==600)REX::INFO("Target batch draw count={}",matched);
    }
    // Scope covers the actual geometry draw, independent of cached SetupGeometry/RestoreGeometry calls.
    originalBatch(pass);
    active=previous;activeDrink=previousDrink;drawDrinks=previousProfiles;
}
}
namespace {
constexpr bool SupportedRuntime(REL::Version v) {
    return nuka::AuditedRuntime(v)!=nullptr;
}
}
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* f4se,F4SE::PluginInfo* info) {
    info->infoVersion=F4SE::PluginInfo::kVersion;info->name="DynamicBottleLiquids";info->version=0x02010000;
    return !f4se->IsEditor()&&SupportedRuntime(f4se->RuntimeVersion());
}
F4SE_PLUGIN_VERSION=[]() noexcept {
    F4SE::PluginVersionData v{};v.PluginVersion({2,1,0,0});v.PluginName("DynamicBottleLiquids");v.AuthorName("DynamicBottleLiquids contributors");
    v.UsesAddressLibrary(true);v.UsesAddressLibraryNG(true);v.IsLayoutDependent(true);v.IsLayoutDependentNG(true);v.CompatibleVersions({REL::Version{1,10,163,0},REL::Version{1,10,984,0},REL::Version{1,11,240,0}});return v;
}();
F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* f4se){
    if(f4se->IsEditor()||!SupportedRuntime(f4se->RuntimeVersion()))return false;
    F4SE::Init(f4se);
    try {
        REX::INFO("Acid Soaker: native ammo/reload-linked tank and shot bubbles enabled.");
        REX::INFO("DynamicBottleLiquids runtime {}; native fog shading enabled; legacy atmospheric transparency removed; external profiles, tuning and basic health logging enabled",f4se->RuntimeVersion().string());
        nuka::VerifyRuntimeHooks(f4se->RuntimeVersion());
        settings=LoadSettings(root);
        try{acidShotConfig=LoadShotBubbleConfig(root/"acid-soaker-bubbles.json");REX::INFO("Acid Soaker bubble config: big={} small={} (restart to reload)",acidShotConfig.bigCount,acidShotConfig.smallCount);}
        catch(const std::exception& e){acidShotConfig={};REX::WARN("Acid Soaker bubble config invalid; using defaults: {}",e.what());}
        auto initial=std::make_shared<DrinkMap>(LoadDrinks(root,settings,LoadTuning(root)));liveDrinks.store(initial);REX::INFO("Loaded {} per-drink profiles",initial->size());
        auto validLogging=ConfigureLogging(root); diagnostics=extensiveLogging; perBottleGpuTiming.store(diagnostics); REX::INFO("Logging mode: {} (INI; restart required)",diagnostics?"Extensive":"Basic"); if(!validLogging)REX::WARN("Invalid Logging Mode; using build default");// Diagnostics are rendered by the Dear ImGui editor/overlay.
        StartTuning(root,[](DrinkMap updated){pendingDrinks.store(std::make_shared<DrinkMap>(std::move(updated)));},FusionPreviewSession);
        REX::INFO("Diagnostic controls: opacity={}, scatter=({}, {}, {}), refraction={}",settings.base.absorption.w,settings.base.scatter.x,settings.base.scatter.y,settings.base.scatter.z,settings.base.trace.z);
        auto result=MH_Initialize();if(result!=MH_OK&&result!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("MinHook initialization failed");
        InstallShieldShaderObserver();
        Native().inspectionProfileMatches=[](RE::BSGeometry* geometry){
            if(!geometry||!geometry->parent||!geometry->name.c_str()||geometry->vertexDesc.desc!=0x1b00000430205ull)return false;
            auto* shape=geometry->IsTriShape();if(!shape)return false;
            auto profiles=liveDrinks.load();
            return profiles&&FindProfile(*profiles,geometry->name.c_str(),shape->numVertices,shape->numTriangles*3)!=nullptr;
        };
        Native().InstallRegistration();
        auto* target=reinterpret_cast<void*>(RE::ID::BSBatchRenderer::Draw.address());
        if(MH_CreateHook(target,reinterpret_cast<void*>(&BatchDraw),reinterpret_cast<void**>(&originalBatch))!=MH_OK)throw std::runtime_error("Cannot hook BSBatchRenderer::Draw");
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);throw std::runtime_error("Cannot enable batch hook");}
        F4SE::GetMessagingInterface()->RegisterListener([](F4SE::MessagingInterface::Message* m){
            if(m->type==F4SE::MessagingInterface::kPreLoadGame||m->type==F4SE::MessagingInterface::kNewGame){CloseTuning();resetRequested=true;}
            if(m->type==F4SE::MessagingInterface::kPostPostLoad){
                try {Native().InstallPreviewHook();}
                catch(const std::exception& e){Native().Fail(e.what());}
                InitializeTuningRenderer(nullptr);
            }
            if(m->type==F4SE::MessagingInterface::kGameDataReady){InitializeMixedDrinkModels();InitializeColdDrinks();auto* data=RE::BSGraphics::GetRendererData();if(data)InitializeTuningRenderer(reinterpret_cast<IDXGISwapChain*>(data->renderWindow[0].swapChain));}
        });
        REX::INFO("DynamicBottleLiquids 2.1 RC1 loaded. Logging: DynamicBottleLiquids.ini. External profiles and material overrides enabled.");return true;
    }catch(const std::exception& e){REX::ERROR("DynamicBottleLiquids disabled: {}",e.what());return false;}
}













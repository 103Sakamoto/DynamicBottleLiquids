#include "SunShadowBounds.h"
#include "PCH.h"
#include "Profiles.h"
#include "Renderer.h"
#include "NativeOptics.h"
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
#include <RE/M/Main.h>
#include <RE/S/Setting.h>
#include <MinHook.h>

namespace {
using namespace nuka;
const std::filesystem::path root=L"Data/F4SE/Plugins/DynamicBottleLiquids";
thread_local const DrinkProfile* activeDrink=nullptr;
DrinkMap drinks;
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
    Motion motion;
    Constants constants,previous,output;
    int raySteps=96;bool detail=true;float interval=0;
    Vec position{},velocity{},shakeAxis{};
    EmissionTracker emissionTracker;
    double time{};
    std::uint32_t frame=~0u;
};
std::unordered_map<RE::BSGeometry*,Instance> instances;
struct LodEntry {RE::NiPointer<RE::BSGeometry> owner;DistanceLod state;};
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
bool Match(RE::BSRenderPass* pass){

    if(!pass||!pass->geometry||!pass->shaderProperty||pass->instanceGroupPass||pass->debrisInstanceGroupPass)return false;
    auto* g=pass->geometry;
    auto it=drinks.find(g->name.c_str()?g->name.c_str():"");if(it==drinks.end())return false;
    auto* tri=g->IsTriShape();
    if(!tri||!FindProfile(drinks,g->name.c_str(),tri->numVertices,tri->numTriangles*3)||g->vertexDesc.desc!=0x1b00000430205ull)return false;
    if(!g->parent)return false;
    const auto d=pass->passEnum;
    // These Effect variants have ModelView directly after WorldViewProj in VS b2.
    constexpr unsigned required=(1u<<4)|(1u<<19)|(1u<<21);
    constexpr unsigned excluded=(1u<<1)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<18)|(1u<<29);
    return (d&required)&&!(d&excluded);
}
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Vec Canonical(Vec v){const auto* m=activeDrink->settings.base.matrix;return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[4]*v.x+m[5]*v.y+m[6]*v.z,m[8]*v.x+m[9]*v.y+m[10]*v.z};}
const Constants& Update(RE::BSGeometry* g,bool detail,float pixels,bool held,const CarryFrame& carry){
    const auto& settings=activeDrink->settings;
    const double now=Now();
    if(resetRequested.exchange(false)){instances.clear();distanceLods.clear();}
    CleanInstances(now);
    if(!instances.contains(g)&&instances.size()>=128)instances.erase(instances.begin());
    auto& i=instances[g];auto frame=RE::BSGraphics::State::GetSingleton()->frameCount;
    if(i.owner&&i.frame==frame)return i.output;
    const auto& world=g->world;Vec position{world.translate.x,world.translate.y,world.translate.z};
    auto up=Normalize(Canonical(WorldToMeshDirection(world.rotate,{0,0,1})));float dt=float(now-i.time);Vec acceleration{};
    auto shakeAxis=Normalize(Canonical(WorldToMeshDirection(world.rotate,{1,0,0})));
    bool reset=!i.owner||dt>.25f||Length(position-i.position)>256;
    bool moved=reset||Length(position-i.position)>.25f||Length(up-i.motion.lastUp)>.015f||
        (settings.shakingBubbles>0&&Length(shakeAxis-i.shakeAxis)>.015f);
    float interval=SimulationInterval(pixels,moved);
    i.raySteps=RayQuality(pixels,i.raySteps);
    if(reset){i.motion=Motion{};i.motion.lastUp=up;i.velocity={};i.shakeAxis=shakeAxis;i.emissionTracker={};dt=0;
        auto seed=reinterpret_cast<std::uintptr_t>(g);i.motion.shakeRandom.seed(unsigned(seed^(seed>>32)));}
    if(reset||detail!=i.detail||interval==0||dt>=interval){
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
        for(int step=0;step<steps;step++){
            i.constants=Simulate(settings,i.motion,acceleration,up,dt/steps,detail,held);

        }

        if(reset||detail!=i.detail||interval==0)i.previous=i.constants;
        i.position=position;i.shakeAxis=shakeAxis;i.time=now;i.detail=detail;
    }
    i.interval=interval;i.output=interval>0?InterpolateSimulation(i.previous,i.constants,float(now-i.time)/interval):i.constants;
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

    return result;
}

void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base){
    if(NativeOptics::busy){originalDraw(ctx,count,start,base);return;}
    if(ctx==hookedContext&&registeredGeometry){try{Native().DrawRegistered(ctx,registeredGeometry,count,start,base,originalDraw);}catch(const std::exception& e){Native().Fail(e.what());}return;}

    if(ctx==hookedContext&&active){
        std::lock_guard lock(renderMutex);
        try {
            double cpuStart=Now();
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

            CleanInstances(cpuStart);
            if(!distanceLods.contains(active->geometry)&&distanceLods.size()>=256)distanceLods.erase(distanceLods.begin());
            auto& entry=distanceLods[active->geometry];entry.owner=active->geometry;
            entry.state.Update(localEyeValid?Length(bottle-eye):-1.f,cpuStart);
            const auto& motionRotation=active->geometry->world.rotate;
            Vec axes[3]{{motionRotation[0].x,motionRotation[0].y,motionRotation[0].z},{motionRotation[1].x,motionRotation[1].y,motionRotation[1].z},{motionRotation[2].x,motionRotation[2].y,motionRotation[2].z}};
            auto grabbed=player?player->grabbedObject.get():RE::NiPointer<RE::TESObjectREFR>{};
            bool held=firstPerson||(grabbed&&Under(active->geometry,grabbed->Get3D()))||std::string_view(active->geometry->name.c_str()).starts_with("DynamicBottleLiquids:iafanimobject");
            auto lod=entry.state;detail=lod.detailWeight>0;effects=lod.effectWeight>0;

            // Shared static fallback: no per-instance simulation beyond 768 units.
            const auto& distantBase=activeDrink->distant;
            float pixels=1e6f;
            if(effects&&!activeDrink->empty&&camera&&localEyeValid&&!camera->viewFrustum.ortho){
                D3D11_VIEWPORT viewport{};UINT n=1;ctx->RSGetViewports(&n,&viewport);
                auto bound=active->geometry->worldBound;auto& f=camera->viewFrustum;
                pixels=ProjectedDiameter(bound.fRadius,Length(Vec{bound.center.x,bound.center.y,bound.center.z}-eye),f.top-f.bottom,f.right-f.left,viewport.Width,viewport.Height);
            }
            // Empty glass uses the near profile, not the distant constants whose
            // refraction has already been disabled by ApplyDetailDistance.
            auto carry=playerCarry;carry.eye=eye;carry.valid=carry.valid&&localEyeValid;
            // A first-person skeleton and a world-space grabbed object must not
            // share derivative history when the coordinate origin changes.
            if(firstPerson)carry.originIdentity=firstRoot;
            auto constants=effects?(activeDrink->empty?activeDrink->settings.base:Update(active->geometry,detail,pixels,held,carry)):distantBase;
            if(effects)ApplyDetailDistance(constants,detail);
            if(!effects||activeDrink->empty)instances.erase(active->geometry); // Empty glass needs no motion/volume solver.

            constants.trace.z*=lod.detailWeight;
            constants.padding[1]=1-lod.detailWeight;
            constants.scatter.w*=lod.detailWeight;
            FadeBubbles(constants,pixels,lod.detailWeight,held);
            PrepareBubbleGroups(constants);
            constants.padding[0]=1-lod.effectWeight;
            // Fade slosh into the static upright fill before switching shaders.
            if(effects){float t=1-lod.effectWeight;
                constants.plane={std::lerp(constants.plane.x,distantBase.plane.x,t),std::lerp(constants.plane.y,distantBase.plane.y,t),std::lerp(constants.plane.z,distantBase.plane.z,t),std::lerp(constants.plane.w,distantBase.plane.w,t)};
                constants.wave.x*=lod.effectWeight;constants.foam*=lod.effectWeight;
            }
            constants.eye=IncidentLight(active,constants,offset,firstPerson?firstRoot:nullptr);
            // Read the live ordinary Effect material; external BGEM and embedded
            // NIF materials share this layout. Do not change flags or patch assets.
            constants.surfaceEnvironment={0,1,0,0};
            auto* material=active->shaderProperty->material;
            if(material&&material->GetType()==RE::BSShaderMaterial::Type::kEffect&&
               active->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEnvMap)){
                auto* effect=static_cast<RE::BSEffectShaderMaterial*>(material);
                float strength=effect->environmentMaskScaleOrRefractionPower;
                constants.surfaceEnvironment={std::isfinite(strength)?std::max(strength,0.f):0.f,effect->lightingInfluence/255.f,(active->passEnum&(1u<<10))?1.f:0.f,0};
                // Converted BGSM glass carries original response metadata loaded
                // once with profiles. Authored BGEM strength remains live.
                if(constants.surfaceResponse.z>.5f)constants.surfaceEnvironment.x=constants.surfaceResponse.w;
                constants.surfaceAlpha={std::isfinite(effect->baseColor.a)?std::clamp(effect->baseColor.a,0.f,1.f):1.f,
                    active->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kFalloff)?1.f:0.f,0,0};
                auto* falloff=&constants.surfaceFalloff.x;
                for(int i=0;i<4;i++)falloff[i]=std::isfinite(effect->falloffData[i])?effect->falloffData[i]:(i<2?float(i):1.f);
            }
            PrepareSunShadow(constants,active->geometry,offset);
            if(firstPerson&&!localEyeValid){constants.sunInfo={};renderer.SetSunShadow(nullptr);}
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
            renderer.SetExpectedIndices(activeDrink->indices);

            Native().BeginFrame(graphics->frameCount);Native().activeGeometry=active->geometry;
            bool replaced=renderer.Draw(ctx,constants,count,start,base,originalDraw);
            Native().activeGeometry=nullptr;

            if(replaced){
                return;
            }
            static bool firstFailure=true;if(firstFailure){firstFailure=false;REX::WARN("Unsupported bottle capture; retaining vanilla rendering.");}
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
    renderer.Initialize(device,root/"shaders",2952);
    auto** table=*reinterpret_cast<void***>(ctx);
    if(MH_CreateHook(table[12],reinterpret_cast<void*>(&Draw),reinterpret_cast<void**>(&originalDraw))!=MH_OK)throw std::runtime_error("Cannot detour DrawIndexed");
    hookedContext=ctx;
    if(MH_EnableHook(table[12])!=MH_OK){hookedContext=nullptr;MH_RemoveHook(table[12]);throw std::runtime_error("Cannot enable DrawIndexed hook");}
    REX::INFO("D3D11 renderer initialized; glass replacement is ready.");

}

void BatchDraw(RE::BSRenderPass* pass){
    if(Native().IsOwnedPass(pass)){auto* previous=registeredGeometry;registeredGeometry=pass->geometry;originalBatch(pass);registeredGeometry=previous;return;}
    auto* previous=active;auto* previousDrink=activeDrink;active=nullptr;activeDrink=nullptr;
    // Ignore unrelated geometry before looking up the fixed profiles.
    if(!previous&&(!pass||!pass->geometry||!pass->geometry->name.c_str()||!std::string_view(pass->geometry->name.c_str()).starts_with("DynamicBottleLiquids:"))){originalBatch(pass);active=previous;activeDrink=previousDrink;return;}

    if(Match(pass)){
        std::lock_guard lock(renderMutex);
        try {EnsureRenderer();if(hookedContext){active=pass;auto* tri=pass->geometry->IsTriShape();activeDrink=FindProfile(drinks,pass->geometry->name.c_str(),tri->numVertices,tri->numTriangles*3);}}
        catch(const std::exception& e){REX::ERROR("Renderer initialization failed; keeping source glass: {}",e.what());}

    }
    // Scope covers the actual geometry draw, independent of cached SetupGeometry/RestoreGeometry calls.
    originalBatch(pass);active=previous;activeDrink=previousDrink;
}
}
namespace {
constexpr bool SupportedRuntime(REL::Version v) {
    return v==REL::Version{1,10,163,0} || v==REL::Version{1,10,984,0} || v==REL::Version{1,11,240,0};
}
}
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* f4se,F4SE::PluginInfo* info) {
    info->infoVersion=F4SE::PluginInfo::kVersion;info->name="DynamicBottleLiquids";info->version=0x01000000;
    return !f4se->IsEditor()&&SupportedRuntime(f4se->RuntimeVersion());
}
F4SE_PLUGIN_VERSION=[]() noexcept {
    F4SE::PluginVersionData v{};v.PluginVersion({1,0,0,0});v.PluginName("DynamicBottleLiquids");v.AuthorName("DynamicBottleLiquids contributors");
    v.UsesAddressLibrary(true);v.UsesAddressLibraryNG(true);v.IsLayoutDependent(true);v.IsLayoutDependentNG(true);v.CompatibleVersions({REL::Version{1,10,163,0},REL::Version{1,10,984,0},REL::Version{1,11,240,0}});return v;
}();
F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* f4se){
    if(f4se->IsEditor()||!SupportedRuntime(f4se->RuntimeVersion()))return false;
    F4SE::Init(f4se);
    try {
        drinks=LoadDrinks();
        REX::INFO("DynamicBottleLiquids 1.0.0: runtime {}; fixed profile keys={}; basic health logging enabled",f4se->RuntimeVersion().string(),drinks.size());
        auto result=MH_Initialize();if(result!=MH_OK&&result!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("MinHook initialization failed");
        Native().InstallRegistration();
        auto* target=reinterpret_cast<void*>(RE::ID::BSBatchRenderer::Draw.address());
        if(MH_CreateHook(target,reinterpret_cast<void*>(&BatchDraw),reinterpret_cast<void**>(&originalBatch))!=MH_OK)throw std::runtime_error("Cannot hook BSBatchRenderer::Draw");
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);throw std::runtime_error("Cannot enable batch hook");}
        F4SE::GetMessagingInterface()->RegisterListener([](F4SE::MessagingInterface::Message* m){
            if(m->type==F4SE::MessagingInterface::kPreLoadGame||m->type==F4SE::MessagingInterface::kNewGame)resetRequested=true;
        });
        return true;
    }catch(const std::exception& e){REX::ERROR("DynamicBottleLiquids disabled: {}",e.what());return false;}
}

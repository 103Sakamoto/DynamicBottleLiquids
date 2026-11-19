#include "PCH.h"
#include "SceneColour.h"
#include "NativeOptics.h"
#include "BottleShapeName.h"
#include "DirectBlend.h"
#include "PreviewScope.h"
#include "InspectionTechnique.h"
#include "ForwardScene.h"
#include "DirectColourState.h"
#include "FrostQueue.h"
#include <RE/B/BSGraphics.h>
#include "OpticalViewport.h"
#include "Performance.h"
#include <RE/B/BSEffectShaderProperty.h>
#include <RE/B/BSUtilityShader.h>
#include <RE/B/BSShaderAccumulator.h>
#include <MinHook.h>
#include <RE/I/Interface3D.h>
#include <RE/N/NiNode.h>
#include <d3d11_1.h>
#include <spdlog/spdlog.h>
namespace nuka {
NativeOptics& Native(){static NativeOptics main,secondary;return NativeOptics::secondaryView?secondary:main;}
bool NativeOptics::busy=false;
namespace {
// Read-only, bounded routing-state logging. Separate coverage diagnostics below
// use asynchronous occlusion queries; these state helpers do not read pixels.
// Scoped to the interface renderer and its offscreen model, never menu-open state.
thread_local PreviewRoots<RE::NiAVObject> previewRoots;
using InterfaceRender=void(*)(RE::Interface3D::Renderer*);
InterfaceRender originalInterfaceRender=nullptr;
void RenderInterface(RE::Interface3D::Renderer* renderer){
    struct Scope {PreviewRoots<RE::NiAVObject> previous=previewRoots;~Scope(){previewRoots=previous;}} scope;
    previewRoots={};
    if(renderer){
        previewRoots.roots={renderer->offscreen3DEnabled?renderer->offscreenElement.get():nullptr,
            renderer->screenAttachedElementRoot.get(),renderer->elementRequiringFullVieport};
    }
    // Inspection can use screen-attached/full-viewport geometry rather than the
    // offscreen inventory model. Never include worldAttachedElementRoot.
    originalInterfaceRender(renderer);
}
using InspectionImmediate=bool(*)(RE::BSRenderPass*,std::uint32_t,bool);
InspectionImmediate originalInspectionImmediate=nullptr;
bool InspectionImmediateDraw(RE::BSRenderPass* pass,std::uint32_t technique,bool setup){
    return RetryInspectionTechnique(pass,technique,setup,originalInspectionImmediate,
        [](RE::BSRenderPass* p){
            if(!p->geometry||!p->shader||!p->shaderProperty||p->passType!=1||p->instanceGroupPass||p->debrisInstanceGroupPass)return false;
            // Only the native Effect shader and a validated DBL profile may retry.
            if(*reinterpret_cast<std::uintptr_t*>(p->shader)!=RE::VTABLE::BSEffectShader[0].address())return false;
            return previewRoots.Contains(static_cast<const RE::NiAVObject*>(p->geometry))&&
                Native().inspectionProfileMatches&&Native().inspectionProfileMatches(p->geometry);
        },[](std::uint32_t from,std::uint32_t to,bool retried){
            static unsigned reports=0;
            if(reports++<32)REX::INFO("Inspection technique: original {:08X} failed; ordinary {:08X} retry={}",from,to,retried);
        });
}
void InstallInspectionHook(){
    if(originalInspectionImmediate)return;
    const auto version=REX::FModule::GetExecutingModule().GetFileVersion().string("_");
    const std::uintptr_t rva=version=="1_10_163_0"?0x287f770:version=="1_10_984_0"?0x20c29c0:version=="1_11_240_0"?0x221fb60:0;
    auto* entry=reinterpret_cast<void*>(RE::ID::BSBatchRenderer::RenderPassImmediately.address());
    if(!rva||reinterpret_cast<std::uintptr_t>(entry)!=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+rva)
        throw std::runtime_error("Unaudited inspection immediate-render address");
    InspectionImmediate next=nullptr;
    if(MH_CreateHook(entry,reinterpret_cast<void*>(&InspectionImmediateDraw),reinterpret_cast<void**>(&next))!=MH_OK)
        throw std::runtime_error("Inspection technique hook creation failed");
    originalInspectionImmediate=next;
    if(MH_EnableHook(entry)!=MH_OK){originalInspectionImmediate=nullptr;MH_RemoveHook(entry);throw std::runtime_error("Inspection technique hook enable failed");}
    REX::INFO("Mirror compatibility: deferred inspection fallback installed for {}; existing entry hook chained; validated DBL previews only",version);
}
static unsigned enbLines=0;
static std::string enbResource(ID3D11Resource* resource){
    if(!resource)return "null";
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))return std::format("{} non-texture",static_cast<void*>(resource));
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    return std::format("{} {}x{} fmt={} samples={} bind={}",static_cast<void*>(resource),d.Width,d.Height,unsigned(d.Format),d.SampleDesc.Count,d.BindFlags);
}
static std::string enbView(ID3D11View* view){
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;if(view)view->GetResource(&resource);return enbResource(resource.Get());
}
static void enbState(ID3D11DeviceContext* c,const char* stage,unsigned frame,const void* geometry){
    if(!extensiveLogging||enbLines++>=600)return;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    c->OMGetRenderTargets(1,&rt,&depth);c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);
    REX::INFO("ENB trace {} frame={} geometry={} colour=[{}] depth=[{}] VS={} PS={}",stage,frame,geometry,enbView(rt.Get()),enbView(depth.Get()),static_cast<void*>(vs.Get()),static_cast<void*>(ps.Get()));
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;c->RSGetState(&raster);D3D11_RASTERIZER_DESC rd{};if(raster)raster->GetDesc(&rd);
    D3D11_RECT scissors[16]{};UINT n=16;c->RSGetScissorRects(&n,scissors);
    REX::INFO("ENB raster {} default={} cull={} frontCCW={} depthClip={} scissor={} count={} rect=({},{},{},{}) bias={} slopeBias={}",stage,!raster,unsigned(rd.CullMode),rd.FrontCounterClockwise,rd.DepthClipEnable,rd.ScissorEnable,n,scissors[0].left,scissors[0].top,scissors[0].right,scissors[0].bottom,rd.DepthBias,rd.SlopeScaledDepthBias);
    for(UINT slot:{0u,1u,7u,14u}){Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> v;c->PSGetShaderResources(slot,1,&v);REX::INFO("ENB trace {} SRV{}=[{}]",stage,slot,enbView(v.Get()));}
}
void hr(HRESULT h){if(FAILED(h))throw std::runtime_error(std::format("Native optics D3D {:08X}",unsigned(h)));}
bool tagged(RE::BSGeometry* g){return g&&g->name.c_str()&&BottleShapeName(g->name.c_str()).starts_with("DynamicBottleLiquids:");}
using Builder=RE::BSShaderProperty::RenderPassArray*(*)(RE::BSEffectShaderProperty*,RE::BSGeometry*,std::uint32_t,RE::BSShaderAccumulator*);
Builder originalBuilder{};
using HDRRender=void(*)(void*,void*,void*);
HDRRender originalHDR{},originalHDRCS{};
void ReplayFrost(){
    auto* graphics=RE::BSGraphics::State::GetSingleton();if(!graphics)return;
    auto& q=Frost();q.Replay(graphics->frameCount);
    static unsigned report=0,lastTotal=0;
    if(q.captured+q.rejected!=lastTotal && (!report || graphics->frameCount-report>=300)){
        report=graphics->frameCount;lastTotal=q.captured+q.rejected;
        REX::INFO("Sunset frost: captured={} replayed={} fallback={} expired={} lastFailure={}",q.captured,q.replayed,q.rejected,q.expired,q.lastFailure);
    }
}
void HDR(void* self,void* geometry,void* parameter){try{Native().ComposeBeforeHDR();}catch(const std::exception& e){Native().Fail(e.what());}ReplayFrost();originalHDR(self,geometry,parameter);}
void HDRCS(void* self,void* geometry,void* parameter){try{Native().ComposeBeforeHDR();}catch(const std::exception& e){Native().Fail(e.what());}ReplayFrost();originalHDRCS(self,geometry,parameter);}

RE::BSShaderProperty::RenderPassArray* Build(RE::BSEffectShaderProperty* p,RE::BSGeometry* g,std::uint32_t mode,RE::BSShaderAccumulator* a){
    auto* result=originalBuilder(p,g,mode,a);
    if(!result||!result->passList||!tagged(g))return result;
    // Only append to a list containing the supported ordinary Effect colour variant.
    bool colour=false;unsigned links=0;
    auto* utility=RE::BSUtilityShader::GetSingleton();if(!utility)return result;
    for(auto* pass=result->passList;pass&&links++<128;pass=pass->propertyNext){
        if(pass->shader==utility&&pass->passType==5&&pass->passEnum==0x61A)return result;
        if(pass->geometry==g&&pass->shader!=utility&&(pass->passEnum&((1u<<4)|(1u<<19)|(1u<<21)))&&!(pass->passEnum&((1u<<1)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<18)|(1u<<29))))colour=true;
    }
    if(!colour||links>=128)return result;
    auto* pass=result->Add(utility,p,g,0x61A,0,nullptr,nullptr,nullptr,nullptr);
    if(pass){pass->passType=5;static bool once=false;if(!once){once=true;REX::INFO("Native optics: engine-owned Utility pass appended; property flags untouched.");}}
    return result;
}
// Restore the state we change; preserve unrelated constant-buffer bindings.
struct State {
    ID3D11DeviceContext* c;bool geometry,direct;ID3DUserDefinedAnnotation* markers=nullptr;ID3D11DeviceContext1* c1=nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
    ID3D11RenderTargetView* rt[8]{};Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    ID3D11ShaderResourceView* srv[21]{};ID3D11SamplerState* samplers[16]{};
    ID3D11Buffer* vb[14]{};ID3D11Buffer* pb[14]{};UINT vf[14]{},vn[14]{},pf[14]{},pn[14]{};
    float factors[4];UINT mask=0,ref=0,nvp=16,nsc=16;D3D11_VIEWPORT vp[16]{};D3D11_RECT sc[16]{};
    D3D11_PRIMITIVE_TOPOLOGY topology;
    explicit State(ID3D11DeviceContext* x,ID3D11DeviceContext1* cached,bool g=false,bool immediate=false,ID3DUserDefinedAnnotation* m=nullptr):c(x),geometry(g),direct(immediate),markers(m),c1(cached){
        CaptureScope capture(markers,L"DBL state snapshot getters");
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->IAGetInputLayout(&layout);if(!direct)c->IAGetPrimitiveTopology(&topology);
        c->OMGetRenderTargets(8,rt,&dsv);c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&ref);if(!direct){c->RSGetState(&raster);c->RSGetViewports(&nvp,vp);c->RSGetScissorRects(&nsc,sc);}
        c->PSGetShaderResources(0,21,srv);if(geometry){for(UINT slot:{5u,10u,13u,14u})c->PSGetSamplers(slot,1,&samplers[slot]);}else c->PSGetSamplers(0,1,&samplers[0]);
        if(!geometry){if(c1)c1->PSGetConstantBuffers1(11,1,&pb[11],&pf[11],&pn[11]);else c->PSGetConstantBuffers(11,1,&pb[11]);}
        if(geometry){
            for(UINT slot:{10u,12u,13u}){if(c1)c1->VSGetConstantBuffers1(slot,1,&vb[slot],&vf[slot],&vn[slot]);else c->VSGetConstantBuffers(slot,1,&vb[slot]);}
            for(UINT slot:{11u,12u,13u}){if(c1)c1->PSGetConstantBuffers1(slot,1,&pb[slot],&pf[slot],&pn[slot]);else c->PSGetConstantBuffers(slot,1,&pb[slot]);}
        }
    }
    double* restoreTime=nullptr;
    ~State(){
        CaptureScope capture(markers,L"DBL state restoration setters");
        auto restoreStart=ProfileNow();
        ID3D11ShaderResourceView* none[21]{};c->PSSetShaderResources(0,21,none);c->OMSetRenderTargets(8,rt,dsv.Get());c->PSSetShaderResources(0,21,srv);if(geometry){for(UINT slot:{5u,10u,13u,14u})c->PSSetSamplers(slot,1,&samplers[slot]);}else c->PSSetSamplers(0,1,&samplers[0]);
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->IASetInputLayout(layout.Get());if(!direct)c->IASetPrimitiveTopology(topology);
        if(!geometry){if(c1)c1->PSSetConstantBuffers1(11,1,&pb[11],&pf[11],&pn[11]);else c->PSSetConstantBuffers(11,1,&pb[11]);}
        if(geometry){
            for(UINT slot:{10u,12u,13u}){if(c1)c1->VSSetConstantBuffers1(slot,1,&vb[slot],&vf[slot],&vn[slot]);else c->VSSetConstantBuffers(slot,1,&vb[slot]);}
            for(UINT slot:{11u,12u,13u}){if(c1)c1->PSSetConstantBuffers1(slot,1,&pb[slot],&pf[slot],&pn[slot]);else c->PSSetConstantBuffers(slot,1,&pb[slot]);}
        }
        c->OMSetBlendState(blend.Get(),factors,mask);c->OMSetDepthStencilState(depth.Get(),ref);if(!direct){c->RSSetState(raster.Get());c->RSSetViewports(nvp,vp);c->RSSetScissorRects(nsc,sc);}
        for(auto* p:rt)if(p)p->Release();for(auto* p:srv)if(p)p->Release();for(auto* p:samplers)if(p)p->Release();for(auto* p:vb)if(p)p->Release();for(auto* p:pb)if(p)p->Release();
        if(restoreTime)*restoreTime=std::chrono::duration<double,std::milli>(ProfileNow()-restoreStart).count();
    }
};
void snapshot(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Buffer* source,Microsoft::WRL::ComPtr<ID3D11Buffer>& dest){
    D3D11_BUFFER_DESC sd{},dd{};source->GetDesc(&sd);if(dest)dest->GetDesc(&dd);
    if(!dest||sd.ByteWidth!=dd.ByteWidth){dest.Reset();sd.Usage=D3D11_USAGE_DEFAULT;sd.CPUAccessFlags=0;sd.MiscFlags=0;hr(d->CreateBuffer(&sd,nullptr,&dest));}
    c->CopyResource(dest.Get(),source);
}
}
bool NativeOptics::IsSecondaryTarget(ID3D11DeviceContext* c,const RE::BSGeometry* geometry){
    if(!GetModuleHandleW(L"RealisticReflectionsMirrors.dll")||!c||!geometry||
       previewRoots.Contains(static_cast<const RE::NiAVObject*>(geometry)))return false;
    auto* manager=RE::BSGraphics::RenderTargetManager::GetSingleton();
    auto* data=RE::BSGraphics::GetRendererData();if(!manager||!data)return false;
    const auto slot=manager->GetDepthStencilTargetPlatformID(1);
    if(slot>=std::size(data->depthStencilTargets))return false;
    auto* mainView=reinterpret_cast<ID3D11ShaderResourceView*>(data->depthStencilTargets[slot].srViewDepth);
    if(!mainView)return false;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(0,nullptr,&depth);
    if(!depth)return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> current,main;depth->GetResource(&current);mainView->GetResource(&main);
    Microsoft::WRL::ComPtr<IUnknown> currentIdentity,mainIdentity;
    if(FAILED(current.As(&currentIdentity))||FAILED(main.As(&mainIdentity)))return false;
    return currentIdentity.Get()!=mainIdentity.Get();
}
bool NativeOptics::IsPreviewGeometry()const{
    if(!activeGeometry)return false;
    auto* node=static_cast<const RE::NiAVObject*>(static_cast<const RE::BSGeometry*>(activeGeometry));
    return previewRoots.Contains(node);
}
bool NativeOptics::ForwardEligible(const Constants& k)const{
    if(!activeGeometry||firstPerson||IsPreviewGeometry()||k.trace.y<=.000001f)return false;
    auto* g=static_cast<const RE::BSGeometry*>(activeGeometry);
    return g->name.c_str()&&!std::string_view(g->name.c_str()).starts_with("DynamicBottleLiquids:iafanimobject");
}
void NativeOptics::InstallDeferredInspectionHook(){ InstallInspectionHook(); }
void NativeOptics::InstallPreviewHook(){
    if(originalInterfaceRender)return;
    // F4R installs its entry detour in PostLoad. Install ours in PostPostLoad so
    // MinHook relocates the existing jump instead of F4R copying our trampoline.
    auto* target=reinterpret_cast<void*>(RE::ID::Interface3D::RenderPrepassesAndMenus.address());
    InterfaceRender next=nullptr;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&RenderInterface),reinterpret_cast<void**>(&next))!=MH_OK)
        throw std::runtime_error("Inventory preview scope hook creation failed");
    originalInterfaceRender=next;
    if(MH_EnableHook(target)!=MH_OK){
        originalInterfaceRender=nullptr;MH_RemoveHook(target);
        throw std::runtime_error("Inventory preview scope hook enable failed");
    }
    REX::INFO("Inventory preview hook installed at PostPostLoad; existing entry detour chained");
}
void NativeOptics::InstallRegistration(){
    auto* target=reinterpret_cast<void*>(RE::ID::BSEffectShaderProperty::GetRenderPasses.address());
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Build),reinterpret_cast<void**>(&originalBuilder))!=MH_OK||MH_EnableHook(target)!=MH_OK)throw std::runtime_error("Native optics pass builder hook failed");
    auto hookHDR=[](REL::ID id,HDRRender replacement,HDRRender& original){
        REL::Relocation<std::uintptr_t> table{id};auto* entry=reinterpret_cast<void**>(table.address());
        if(MH_CreateHook(entry[1],reinterpret_cast<void*>(replacement),reinterpret_cast<void**>(&original))!=MH_OK||MH_EnableHook(entry[1])!=MH_OK)throw std::runtime_error("Native optics HDR-entry hook failed");
    };
    hookHDR(RE::VTABLE::ImageSpaceEffectHDR[0],&HDR,originalHDR);
    hookHDR(RE::VTABLE::ImageSpaceEffectHDRCS[0],&HDRCS,originalHDRCS);
    REX::INFO("Native optics: HDR/HDRCS entry hooks installed before HDR shader setup.");
}
bool NativeOptics::IsOwnedPass(const RE::BSRenderPass* p)const{return p&&p->passType==5&&p->passEnum==0x61A&&p->shader==RE::BSUtilityShader::GetSingleton()&&tagged(p->geometry);}
void NativeOptics::Fail(const char* why){failed=true;REX::ERROR("Native optics disabled: {}",why);}
void NativeOptics::Initialize(ID3D11Device* d,const std::filesystem::path& path){
    backgroundView.Reset();backgroundIdentity.Reset();
    regional.Initialize(d,path);
    device=d;D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION,0};if(extensiveLogging)hr(d->CreateQuery(&query,&coverage));auto code=[&](const char* name){Microsoft::WRL::ComPtr<ID3DBlob> b;hr(D3DReadFileToBlob((path/name).c_str(),&b));return b;};
    auto scaled=code("OpticalCompose.scaled.ps.cso");hr(d->CreatePixelShader(scaled->GetBufferPointer(),scaled->GetBufferSize(),nullptr,&scaledComposePS));
    auto probe=code("OpticalCompose.probe.ps.cso"),scaledProbe=code("OpticalCompose.probe-scaled.ps.cso");
    hr(d->CreatePixelShader(probe->GetBufferPointer(),probe->GetBufferSize(),nullptr,&probePS));
    hr(d->CreatePixelShader(scaledProbe->GetBufferPointer(),scaledProbe->GetBufferSize(),nullptr,&scaledProbePS));
    auto v=code("OpticalParts.vs.cso"),p=code("OpticalParts.ps.cso"),sv=code("OpticalCompose.vs.cso"),sp=code("OpticalCompose.ps.cso");
    hr(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&producerVS));hr(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&producerPS));
    hr(d->CreateVertexShader(sv->GetBufferPointer(),sv->GetBufferSize(),nullptr,&composeVS));hr(d->CreatePixelShader(sp->GetBufferPointer(),sp->GetBufferSize(),nullptr,&composePS));
    D3D11_INPUT_ELEMENT_DESC e[]={{"POSITION",0,DXGI_FORMAT_R16G16B16A16_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},{"BINORMAL",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    hr(d->CreateInputLayout(e,4,v->GetBufferPointer(),v->GetBufferSize(),&layout));hr(constants.Initialize(d,sizeof(Constants)));
    D3D11_BLEND_DESC bd{};for(auto& b:bd.RenderTarget)b.RenderTargetWriteMask=15;hr(d->CreateBlendState(&bd,&opaque));
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;hr(d->CreateDepthStencilState(&dd,&nearest));dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;hr(d->CreateDepthStencilState(&dd,&noDepth));
    auto previewCode=code("OpticalParts.preview.ps.cso");hr(d->CreatePixelShader(previewCode->GetBufferPointer(),previewCode->GetBufferSize(),nullptr,&previewPS));
    auto opaqueForwardCode=code("OpaquePrototype.forward.ps.cso");
    hr(d->CreatePixelShader(opaqueForwardCode->GetBufferPointer(),opaqueForwardCode->GetBufferSize(),nullptr,&forwardPS));
    auto directCode=code("OpticalParts.direct.ps.cso");hr(d->CreatePixelShader(directCode->GetBufferPointer(),directCode->GetBufferSize(),nullptr,&directPS));
    auto directBD=DirectBlend(false);hr(d->CreateBlendState(&directBD,&directBlend));
    auto previewBD=DirectBlend(true);hr(d->CreateBlendState(&previewBD,&previewBlend));
    D3D11_DEPTH_STENCIL_DESC directDD{};directDD.DepthEnable=TRUE;directDD.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;directDD.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;hr(d->CreateDepthStencilState(&directDD,&directDepth));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;hr(d->CreateRasterizerState(&rd,&screen));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;hr(d->CreateSamplerState(&sd,&linear));
}
void NativeOptics::BeginFrame(unsigned f){if(frame==f)return;
    Perf().SetDrawInfo(f,0);
    if(++healthFrames>=1800){
        REX::INFO("Health (1800 observed bottle frames): captures={} opticalDraws={} distantDraws={} compositions={} fullScreen={} rejected={} records={} disabled={}",healthCaptures,healthDraws,healthDistant,healthComposes,healthFull,healthRejected,records.size(),failed);
        healthFrames=0;healthCaptures=healthDraws=healthDistant=healthComposes=healthFull=healthRejected=0;
    }

    if(captured&&(drawn==0||composedFrame!=frame)){if(++missed>=3)Fail("registration or HDR composition did not complete for three active frames; restored baseline rendering");}else missed=0;
    frame=f;drawn=0;captured=0;sourceIdentity.Reset();mainDepth.Reset();for(auto i=records.begin();i!=records.end();)if(f-i->second.frame>120)i=records.erase(i);else++i;}
void NativeOptics::Prepare(ID3D11DeviceContext* c,UINT w,UINT h,DXGI_FORMAT format){
    CaptureScope preparation(captureMarkers.Get(),L"DBL target preparation");
    if(width!=w||height!=h||sceneFormat!=format){
        CaptureScope allocation(captureMarkers.Get(),L"DBL target allocation on resize");
        // Keep signed distortion and light/transmission precision unchanged. Only
        // the final output adopts the native format for both UAV and raw copy-back.
        width=w;height=h;sceneFormat=format;preparedFrame=~0u;auto make=[&](Target& t,UINT tw,UINT th,bool writable=false){t={};D3D11_TEXTURE2D_DESC td{};td.Width=tw;td.Height=th;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=writable?sceneFormat:DXGI_FORMAT_R16G16B16A16_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(writable){UINT support=0;if(SUCCEEDED(device->CheckFormatSupport(td.Format,&support))&&(support&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;}
        auto result=device->CreateTexture2D(&td,nullptr,&t.texture);if(FAILED(result)&&writable){td.BindFlags&=~D3D11_BIND_UNORDERED_ACCESS;result=device->CreateTexture2D(&td,nullptr,&t.texture);}hr(result);hr(device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.srv));hr(device->CreateRenderTargetView(t.texture.Get(),nullptr,&t.rtv));};
        // Immutable size data is shared by all bottles until the target changes.
        float size[4]{float(w),float(h),0,0};D3D11_BUFFER_DESC sizeDesc{};sizeDesc.ByteWidth=sizeof(size);sizeDesc.Usage=D3D11_USAGE_IMMUTABLE;sizeDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sizeData{};sizeData.pSysMem=size;viewportConstants.Reset();hr(device->CreateBuffer(&sizeDesc,&sizeData,&viewportConstants));
        REX::INFO("Native optics targets: {}x{}, scene/output format={}, optical MRT format=10.",w,h,unsigned(sceneFormat));
        make(light,w,h);make(transmission,w,h);make(distortion,w,h);make(composed,w,h,true);nearestTexture.Reset();nearestDSV.Reset();nearestSRV.Reset();D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_TYPELESS;td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;hr(device->CreateTexture2D(&td,nullptr,&nearestTexture));
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;hr(device->CreateDepthStencilView(nearestTexture.Get(),&dv,&nearestDSV));
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;hr(device->CreateShaderResourceView(nearestTexture.Get(),&sv,&nearestSRV));
    }
    if(preparedFrame!=frame){
        CaptureScope clearing(captureMarkers.Get(),L"DBL once-per-frame target clears");const float z[4]{};
        {CaptureScope event(captureMarkers.Get(),L"DBL clear local light");c->ClearRenderTargetView(light.rtv.Get(),z);}
        {CaptureScope event(captureMarkers.Get(),L"DBL clear transmission");c->ClearRenderTargetView(transmission.rtv.Get(),z);}
        {CaptureScope event(captureMarkers.Get(),L"DBL clear distortion");c->ClearRenderTargetView(distortion.rtv.Get(),z);}
        {CaptureScope event(captureMarkers.Get(),L"DBL clear nearest depth");c->ClearDepthStencilView(nearestDSV.Get(),D3D11_CLEAR_DEPTH,1,0);}
        preparedFrame=frame;
    }
}
bool NativeOptics::Capture(ID3D11DeviceContext* c,const Constants& k,ID3D11ShaderResourceView* cubeView,ID3D11ShaderResourceView* sunView,ID3D11ShaderResourceView* noiseView,ID3D11SamplerState* shadowSampler,ID3D11SamplerState* noiseSamplerView){
    // Trace once per rejection kind; no change to acceptance or rendering.
    auto reject=[&](unsigned reason,const char* name){
        ++healthRejected;
        static unsigned logged=0;if(!(logged&(1u<<reason))){logged|=1u<<reason;
            D3D11_VIEWPORT view[16]{};UINT views=16;c->RSGetViewports(&views,view);
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> z;c->OMGetRenderTargets(1,&target,&z);
            D3D11_TEXTURE2D_DESC cd{},dd{};D3D11_RENDER_TARGET_VIEW_DESC cv{};D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
            auto describe=[](ID3D11View* v,D3D11_TEXTURE2D_DESC& d){if(!v)return;Microsoft::WRL::ComPtr<ID3D11Resource> r;Microsoft::WRL::ComPtr<ID3D11Texture2D> t;v->GetResource(&r);if(SUCCEEDED(r.As(&t)))t->GetDesc(&d);};
            describe(target.Get(),cd);describe(z.Get(),dd);if(target)target->GetDesc(&cv);if(z)z->GetDesc(&dv);
            REX::WARN("Capture rejection [{}] {}: frame={} geometry={} records={} failed={} immediate={} colour={}x{} format={} viewFormat={} samples={} array={} bind={} depth={}x{} format={} viewFormat={} samples={} bind={} viewports={} viewport=({}, {}, {}, {}) depthRange=({}, {})",reason,name,frame,activeGeometry,records.size(),failed,c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,cd.Width,cd.Height,unsigned(cd.Format),unsigned(cv.Format),cd.SampleDesc.Count,cd.ArraySize,cd.BindFlags,dd.Width,dd.Height,unsigned(dd.Format),unsigned(dv.Format),dd.SampleDesc.Count,dd.BindFlags,views,view[0].TopLeftX,view[0].TopLeftY,view[0].Width,view[0].Height,view[0].MinDepth,view[0].MaxDepth);
        }return false;
    };
    if(failed||!device||!activeGeometry||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return reject(0,"renderer/context/geometry unavailable");
    if(records.size()>=256&&!records.contains(activeGeometry))return reject(1,"record limit");
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&rt,&depth);if(!rt||!depth)return reject(2,"missing colour/depth view");
    if(context.Get()!=c){context=c;context1.Reset();c->QueryInterface(IID_PPV_ARGS(&context1));captureMarkers.Reset();c->QueryInterface(IID_PPV_ARGS(&captureMarkers));}
    auto& r=records[activeGeometry];rt->GetResource(r.colour.ReleaseAndGetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> colour;if(FAILED(r.colour.As(&colour)))return reject(3,"colour is not Texture2D");D3D11_TEXTURE2D_DESC td{};colour->GetDesc(&td);
    if(td.SampleDesc.Count!=1||td.ArraySize!=1||!SupportedSceneColour(td.Format))return reject(4,"colour format/sample/array mismatch");
    UINT n=1;c->RSGetViewports(&n,&r.viewport);if(n!=1||r.viewport.TopLeftX!=0||r.viewport.TopLeftY!=0||!std::isfinite(r.viewport.Width)||!std::isfinite(r.viewport.Height)||r.viewport.Width<=0||r.viewport.Height<=0||r.viewport.Width>td.Width||r.viewport.Height>td.Height)return reject(5,"viewport is outside supported colour region");
    Microsoft::WRL::ComPtr<ID3D11Buffer> g,l;auto* c1=context1.Get();
    if(c1){c1->VSGetConstantBuffers1(2,1,&g,&r.gf,&r.gn);c1->PSGetConstantBuffers1(2,1,&l,&r.lf,&r.ln);}else{c->VSGetConstantBuffers(2,1,&g);c->PSGetConstantBuffers(2,1,&l);}if(!g||!l)return reject(6,"missing transform/lighting buffer");
    Microsoft::WRL::ComPtr<ID3D11Resource> dr;depth->GetResource(&dr);
    if(dr!=r.depthResource||!r.depth){r.depth.Reset();r.depthResource=dr;D3D11_DEPTH_STENCIL_VIEW_DESC vd{};depth->GetDesc(&vd);D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        if(vd.Format==DXGI_FORMAT_D24_UNORM_S8_UINT)sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;else if(vd.Format==DXGI_FORMAT_D32_FLOAT)sd.Format=DXGI_FORMAT_R32_FLOAT;else return reject(7,"unsupported depth view format");
        if(FAILED(device->CreateShaderResourceView(dr.Get(),&sd,&r.depth))){r.depthResource.Reset();Fail("native scene depth is not shader-readable");return reject(8,"depth shader-resource view creation failed");}}
    snapshot(device.Get(),c,g.Get(),r.geometry);snapshot(device.Get(),c,l.Get(),r.lighting);
    // The appended Utility pass may run with a different PerFrame binding or
    // after the engine updates the same buffer. Preserve the BGEM draw's bytes
    // and range alongside its geometry and lighting, rather than borrowing
    // whichever weather/camera constants happen to be bound during replay.
    Microsoft::WRL::ComPtr<ID3D11Buffer> fog;
    r.fogFirst=r.fogCount=0;
    if(c1)c1->VSGetConstantBuffers1(12,1,&fog,&r.fogFirst,&r.fogCount);
    else c->VSGetConstantBuffers(12,1,&fog);
    if(fog)snapshot(device.Get(),c,fog.Get(),r.fogFrame);
    else r.fogFrame.Reset();
    r.diagnostic=extensiveLogging&&enbLines<500&&(r.diagnosticFrame==~0u||frame-r.diagnosticFrame>=300);
    if(r.diagnostic){r.diagnosticFrame=frame;enbState(c,"capture",frame,activeGeometry);REX::INFO("ENB constants geometry={} opacity={} fill={} emission=({},{},{}) geometryCB={} lightingCB={} ranges={},{}/{},{}",activeGeometry,k.absorption.w,k.trace.y,k.emission.x,k.emission.y,k.emission.z,static_cast<void*>(g.Get()),static_cast<void*>(l.Get()),r.gf,r.gn,r.lf,r.ln);}
    r.constants=k;if(refractionProbe.load()==1)r.constants.trace.z=0;r.frame=frame;r.cube=cubeView;r.sun=sunView;r.noise=noiseView;r.shadow=shadowSampler;r.noiseSampler=noiseSamplerView;
    c->PSGetShaderResources(0,1,r.base.ReleaseAndGetAddressOf());c->PSGetShaderResources(1,1,r.normal.ReleaseAndGetAddressOf());c->PSGetShaderResources(7,1,r.mask.ReleaseAndGetAddressOf());
    // Retain the original cube filtering/minimum LOD across the delayed draw.
    // The liquid continues using .24's separate s14 sampler.
    c->PSGetSamplers(5,1,r.cubeSampler.ReleaseAndGetAddressOf());
    static bool once=false;if(!once){once=true;REX::INFO("Native optics: source transform and lighting snapshotted at original colour draw; no scene-colour copy.");}++captured;++healthCaptures;return true;
}
bool NativeOptics::DrawDistant(ID3D11DeviceContext* c,const Constants& k,ID3D11ShaderResourceView* cube,ID3D11ShaderResourceView* sun,UINT count,UINT start,INT base,void(STDMETHODCALLTYPE* original)(ID3D11DeviceContext*,UINT,UINT,INT)){
    const bool preview=IsPreviewGeometry();
    const bool forwardWorld=secondaryView||ForwardEligible(k);
    if(failed||!device||!directPS||!activeGeometry||(!preview&&!forwardWorld&&k.optics.w<2))return false;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&rt,&depth);if(!rt||!depth)return false;
    if(context.Get()!=c){context=c;context1.Reset();c->QueryInterface(IID_PPV_ARGS(&context1));captureMarkers.Reset();c->QueryInterface(IID_PPV_ARGS(&captureMarkers));}
    Microsoft::WRL::ComPtr<ID3D11Buffer> g,l;UINT gf=0,gn=0,lf=0,ln=0;
    if(context1){context1->VSGetConstantBuffers1(2,1,&g,&gf,&gn);context1->PSGetConstantBuffers1(2,1,&l,&lf,&ln);}else{c->VSGetConstantBuffers(2,1,&g);c->PSGetConstantBuffers(2,1,&l);}if(!g||!l)return false;
    // Use the current colour draw's buffers directly, without delayed snapshots.
    CaptureScope directCapture(captureMarkers.Get(),L"DBL distant direct producer");
    NativeTiming timing(c,2,true);
    struct DirectStages {
        NativeTiming& timing;std::chrono::steady_clock::time_point start=ProfileNow();double draw=0,restore=0;
        ~DirectStages(){auto end=ProfileNow();double total=std::chrono::duration<double,std::milli>(end-start).count();Perf().AddProducerCpu(std::max(0.0,total-draw-restore),draw,restore,true);timing.reportingMs=std::chrono::duration<double,std::milli>(ProfileNow()-end).count();}
    } stages{timing};
    DirectColourState state(c,context1.Get());state.restoreTime=&stages.restore;busy=true;
    try{
        Constants forwardConstants=k;
        ID3D11ShaderResourceView* scene=nullptr;
        if(forwardWorld&&k.trace.z>0&&refractionProbe.load()!=1){
            static ForwardScene mainSnapshot,secondarySnapshot;auto& snapshot=secondaryView?secondarySnapshot:mainSnapshot;
            CaptureScope copyMarker(captureMarkers.Get(),L"DBL forward refraction scene snapshot (shared per view/frame)");
            scene=snapshot.Snapshot(c,rt.Get(),frame);
        }
        if(!scene||refractionProbe.load()==1)forwardConstants.trace.z=0;
        c->PSSetShaderResources(18,1,&scene);
        c->VSSetShader(producerVS.Get(),nullptr,0);c->PSSetShader(preview?previewPS.Get():(forwardWorld?forwardPS.Get():directPS.Get()),nullptr,0);c->IASetInputLayout(layout.Get());
        auto* gb=g.Get();auto* lb=l.Get();if(context1){context1->VSSetConstantBuffers1(10,1,&gb,&gf,&gn);context1->PSSetConstantBuffers1(12,1,&lb,&lf,&ln);}else{c->VSSetConstantBuffers(10,1,&gb);c->PSSetConstantBuffers(12,1,&lb);}
        ID3D11Buffer* cb=nullptr;hr(constants.Upload(c,&forwardConstants,cb));c->VSSetConstantBuffers(13,1,&cb);c->PSSetConstantBuffers(13,1,&cb);
        c->OMSetBlendState(preview?previewBlend.Get():directBlend.Get(),nullptr,~0u);c->OMSetDepthStencilState(directDepth.Get(),0);
        c->PSSetShaderResources(14,1,&cube);c->PSSetShaderResources(20,1,&sun);auto* samp=linear.Get();c->PSSetSamplers(14,1,&samp);
        // Shadow sampler comes from the current renderer binding below.
        auto submit=ProfileNow();
        original(c,count,start,base);
        stages.draw=std::chrono::duration<double,std::milli>(ProfileNow()-submit).count();records.erase(activeGeometry);++healthDistant;Perf().DirectResult();
        if(forwardWorld&&!forwardReported){forwardReported=true;REX::INFO("Forward liquid colour submitted at ordinary Effect draw; shared refraction snapshot; shield applied at native god-ray composition.");}
    }catch(...){busy=false;throw;}busy=false;return true;
}
void NativeOptics::DrawRegistered(ID3D11DeviceContext* c,const void* geometry,UINT count,UINT start,INT base,void(STDMETHODCALLTYPE* original)(ID3D11DeviceContext*,UINT,UINT,INT)){
    auto i=records.find(geometry);if(failed||i==records.end()||i->second.frame!=frame)return;auto& r=i->second;
    static unsigned coverageLogCount=0;
    if(r.coveragePending){UINT64 passed=0;HRESULT result=c->GetData(r.diagnosticCoverage.Get(),&passed,sizeof(passed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(result==S_OK){r.coveragePending=false;if(coverageLogCount++<1000)REX::INFO("ENB coverage geometry={} frame={} bypass={} samples={}",geometry,r.coverageFrame,r.coverageBypass,passed);}
        else if(FAILED(result)){r.coveragePending=false;REX::WARN("ENB coverage query failed {:X}",unsigned(result));}}
    bool measureBottle=extensiveLogging&&coverageLogCount<1000&&!r.coveragePending&&(r.lastCoverageFrame==~0u||frame-r.lastCoverageFrame>=120||r.coverageBypass!=enbDepthBypass.load());
    if(measureBottle&&!r.diagnosticCoverage){D3D11_QUERY_DESC q{D3D11_QUERY_OCCLUSION,0};if(FAILED(device->CreateQuery(&q,&r.diagnosticCoverage)))measureBottle=false;}
    if(r.diagnostic)enbState(c,"registered-entry",frame,geometry);
    CaptureScope producerCapture(captureMarkers.Get(),L"DBL optical producer");
    NativeTiming timing(c,2);
    // Destroyed after State, so restoration is included, even on exceptions.
    struct CpuStages {
        std::chrono::steady_clock::time_point start=ProfileNow();
        NativeTiming& timing;
        double draw=0,restore=0;
        ~CpuStages(){double total=std::chrono::duration<double,std::milli>(ProfileNow()-start).count();auto reportStart=ProfileNow();Perf().AddProducerCpu(std::max(0.0,total-draw-restore),draw,restore);timing.reportingMs=std::chrono::duration<double,std::milli>(ProfileNow()-reportStart).count();}
    } cpuStages{ProfileNow(),timing};
    D3D11_TEXTURE2D_DESC td{};Microsoft::WRL::ComPtr<ID3D11Texture2D> t;hr(r.colour.As(&t));t->GetDesc(&td);Prepare(c,td.Width,td.Height,td.Format);timing.Point(1);
    State state(c,context1.Get(),true,false,captureMarkers.Get());state.restoreTime=&cpuStages.restore;busy=true;try{
        const auto opticalViewport=OpticalDepthViewport(r.viewport,state.vp,state.nvp);
        CaptureScope setup(captureMarkers.Get(),L"DBL optical setup and constant upload");
        ID3D11ShaderResourceView* none[21]{};c->PSSetShaderResources(0,21,none);
        ID3D11RenderTargetView* outputs[]{light.rtv.Get(),transmission.rtv.Get(),distortion.rtv.Get()};c->OMSetRenderTargets(3,outputs,nearestDSV.Get());c->OMSetBlendState(opaque.Get(),nullptr,~0u);c->OMSetDepthStencilState(nearest.Get(),0);
        c->RSSetViewports(1,&opticalViewport);c->VSSetShader(producerVS.Get(),nullptr,0);c->PSSetShader(producerPS.Get(),nullptr,0);c->IASetInputLayout(layout.Get());
        auto* fog=r.fogFrame.Get();
        if(state.c1)state.c1->VSSetConstantBuffers1(12,1,&fog,&r.fogFirst,&r.fogCount);
        else c->VSSetConstantBuffers(12,1,&fog);
        auto* g=r.geometry.Get();auto* l=r.lighting.Get();if(state.c1){state.c1->VSSetConstantBuffers1(10,1,&g,&r.gf,&r.gn);state.c1->PSSetConstantBuffers1(12,1,&l,&r.lf,&r.ln);}else{c->VSSetConstantBuffers(10,1,&g);c->PSSetConstantBuffers(12,1,&l);}
        ID3D11Buffer* cb=nullptr;{CaptureScope upload(captureMarkers.Get(),L"DBL constants Map WRITE_DISCARD and upload");hr(constants.Upload(c,&r.constants,cb));}c->VSSetConstantBuffers(13,1,&cb);c->PSSetConstantBuffers(13,1,&cb);
        cb=viewportConstants.Get();c->PSSetConstantBuffers(11,1,&cb);
        ID3D11ShaderResourceView* resources[21]{};resources[0]=r.base.Get();resources[1]=r.normal.Get();resources[7]=r.mask.Get();resources[14]=r.cube.Get();resources[16]=r.noise.Get();resources[17]=r.depth.Get();resources[20]=r.sun.Get();c->PSSetShaderResources(0,21,resources);
        auto* s=linear.Get();c->PSSetSamplers(14,1,&s);s=r.shadow.Get();c->PSSetSamplers(10,1,&s);s=r.noiseSampler.Get();c->PSSetSamplers(13,1,&s);s=r.cubeSampler.Get();c->PSSetSamplers(5,1,&s);
        if(timing.Measured()&&coveragePending){UINT64 samples=0;auto status=c->GetData(coverage.Get(),&samples,sizeof(samples),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(status==S_OK){coveragePending=false;++coverageReports;REX::INFO("Native optics GPU coverage: {} samples passed native foreground and nearest-bottle depth.",samples);}}
        bool measure=timing.Measured()&&!coveragePending&&coverageReports<3;if(measure)c->Begin(coverage.Get());
        setup.End();
        CaptureScope drawCapture(captureMarkers.Get(),L"DBL optical shading and depth draw");
        timing.Point(2);auto submitStart=ProfileNow();
        if(r.diagnostic)enbState(c,"producer-bound",frame,geometry);
        if(measureBottle){r.coverageFrame=r.lastCoverageFrame=frame;r.coverageBypass=enbDepthBypass.load();c->Begin(r.diagnosticCoverage.Get());}
        original(c,count,start,base);
        if(measureBottle){c->End(r.diagnosticCoverage.Get());r.coveragePending=true;}
        r.producedFrame=frame;timing.Point(3);
        cpuStages.draw=std::chrono::duration<double,std::milli>(ProfileNow()-submitStart).count();
        drawCapture.End();
        if(measure){c->End(coverage.Get());coveragePending=true;}sourceIdentity=r.colour;mainDepth=r.depth;sceneViewport=opticalViewport;++drawn;++healthDraws;
        static bool once=false;if(!once){once=true;REX::INFO("Native optics: registered refractive-group draw emitted L/T/distortion MRTs, native geometry b2 bypassed.");}
    }catch(...){busy=false;throw;}busy=false;
}
bool NativeOptics::ComposeBeforeHDR(){
    auto* c=context.Get();
    if(c)Perf().Poll(c);
    struct FinishFrame {~FinishFrame(){Perf().CloseFrame();}} finishFrame;
    if(!c||busy||failed||!drawn||composedFrame==frame)return false;
    bool traceComposition=false;
    for(const auto& [geometry,r]:records)if(r.frame==frame&&r.diagnostic){traceComposition=true;if(enbLines++<600)REX::INFO("ENB route frame={} geometry={} produced={} capturedColour=[{}] selectedCompositionColour=[{}] sameColour={}",frame,geometry,r.producedFrame==frame,enbResource(r.colour.Get()),enbResource(sourceIdentity.Get()),r.colour.Get()==sourceIdentity.Get());}
    if(traceComposition)enbState(c,"HDR-entry",frame,nullptr);
    CaptureScope composeCapture(captureMarkers.Get(),L"DBL HDR regional or full-screen composition");
    NativeTiming timing(c,3);
    // Entry is before the engine binds its HDR replacement shader. Keep the
    // native source resource identity so HDR provenance/parameter checks survive.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;hr(sourceIdentity.As(&source));
    D3D11_TEXTURE2D_DESC sd{};source->GetDesc(&sd);
    if(sd.Width!=width||sd.Height!=height||sd.Format!=sceneFormat||!SupportedSceneColour(sd.Format)||sd.SampleDesc.Count!=1)return false;
    if(backgroundIdentity.Get()!=sourceIdentity.Get()||!backgroundView){
        backgroundView.Reset();backgroundIdentity.Reset();
        hr(device->CreateShaderResourceView(source.Get(),nullptr,&backgroundView));backgroundIdentity=sourceIdentity;
    }
    bool scaledViewport=sceneViewport.Width!=float(width)||sceneViewport.Height!=float(height);
    if(scaledViewport){static bool logged=false;if(!logged){logged=true;REX::INFO("Scaled viewport optics active: colour={}x{} viewport={}x{}; native allocation coordinates retained.",width,height,sceneViewport.Width,sceneViewport.Height);}}
    if(scaledViewport && (!activeViewportBuffer||activeWidth!=sceneViewport.Width/width||activeHeight!=sceneViewport.Height/height)){
        activeWidth=sceneViewport.Width/width;activeHeight=sceneViewport.Height/height;
        float region[4]{activeWidth,activeHeight,0,0};D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=region;activeViewportBuffer.Reset();hr(device->CreateBuffer(&desc,&data,&activeViewportBuffer));
    }
    const bool probeEnabled=enbDepthBypass.load();
    bool useRegion=!probeEnabled&&regionalComposition.load()&&regional.Prepare(device.Get(),c,source.Get(),composed.texture.Get());
    State state(c,context1.Get());busy=true;try{
        if(useRegion){
            ID3D11ShaderResourceView* inputs[]{backgroundView.Get(),light.srv.Get(),transmission.srv.Get(),distortion.srv.Get(),mainDepth.Get(),nearestSRV.Get()};
            regional.Draw(c,inputs,linear.Get(),activeViewportBuffer.Get(),scaledViewport,width,height,timing);
            composedFrame=frame;++healthComposes;Perf().ComposePath(true);
            static bool logged=false;if(!logged){logged=true;REX::INFO("Regional composition active: current-frame GPU optical coverage, indirect compose and bounded UAV copy-back; no CPU readback.");}
        }else{
        Perf().ComposePath(false);
        if(scaledViewport){auto* cb=activeViewportBuffer.Get();c->PSSetConstantBuffers(11,1,&cb);}
        ID3D11ShaderResourceView* none[21]{};c->PSSetShaderResources(0,21,none);auto* output=composed.rtv.Get();c->OMSetRenderTargets(1,&output,nullptr);c->OMSetDepthStencilState(noDepth.Get(),0);c->OMSetBlendState(opaque.Get(),nullptr,~0u);c->RSSetState(screen.Get());
        D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&vp);c->VSSetShader(composeVS.Get(),nullptr,0);c->PSSetShader(probeEnabled?(scaledViewport?scaledProbePS.Get():probePS.Get()):(scaledViewport?scaledComposePS.Get():composePS.Get()),nullptr,0);c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* in[]{backgroundView.Get(),light.srv.Get(),transmission.srv.Get(),distortion.srv.Get(),mainDepth.Get(),nearestSRV.Get()};c->PSSetShaderResources(0,6,in);auto* s=linear.Get();c->PSSetSamplers(0,1,&s);
        timing.Point(1);auto drawStart=ProfileNow();c->Draw(3,0);
        double drawMs=std::chrono::duration<double,std::milli>(ProfileNow()-drawStart).count();timing.Point(2);
        ID3D11ShaderResourceView* nullViews[21]{};c->PSSetShaderResources(0,21,nullViews);c->OMSetRenderTargets(0,nullptr,nullptr);
        // The final target matches the native format (including RGB-only HDR).
        // RTV/UAV stores perform the native quantization; raw copy needs no conversion.
        // Resolve the completed optical result into the original native scene.
        // This is an output copy, not an additional background scene capture.
        auto copyStart=ProfileNow();c->CopyResource(source.Get(),composed.texture.Get());++healthFull;
        double copyMs=std::chrono::duration<double,std::milli>(ProfileNow()-copyStart).count();timing.Point(3);
        Perf().AddComposeCpu(drawMs,copyMs);composedFrame=frame;++healthComposes;
        static bool once=false;if(!once){once=true;REX::INFO("Native optics: L + T * displaced background composed at HDR entry; source {}x{}, registered draws={}; native source identity preserved; output resolves=1, background captures=0.",width,height,drawn);}
        }
    }catch(...){busy=false;throw;}busy=false;
    if(traceComposition&&enbLines++<600)REX::INFO("ENB composition result frame={} completed={} regional={} destination=[{}]",frame,composedFrame==frame,useRegion,enbResource(sourceIdentity.Get()));
    // Restore the preceding world-pass state; the engine sets up HDR next.
    return true;
}
}






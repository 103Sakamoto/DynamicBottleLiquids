#include "PCH.h"
#include "SceneColour.h"
#include "NativeOptics.h"
#include <RE/B/BSEffectShaderProperty.h>
#include <RE/B/BSUtilityShader.h>
#include <RE/B/BSShaderAccumulator.h>
#include <MinHook.h>
#include <d3d11_1.h>
namespace nuka {
NativeOptics& Native(){static NativeOptics s;return s;}
bool NativeOptics::busy=false;
namespace {
void hr(HRESULT h){if(FAILED(h))throw std::runtime_error(std::format("Native optics D3D {:08X}",unsigned(h)));}
bool tagged(RE::BSGeometry* g){return g&&g->name.c_str()&&std::string_view(g->name.c_str()).starts_with("DynamicBottleLiquids:");}
using Builder=RE::BSShaderProperty::RenderPassArray*(*)(RE::BSEffectShaderProperty*,RE::BSGeometry*,std::uint32_t,RE::BSShaderAccumulator*);
Builder originalBuilder{};
using HDRRender=void(*)(void*,void*,void*);
HDRRender originalHDR{},originalHDRCS{};
void HDR(void* self,void* geometry,void* parameter){try{Native().ComposeBeforeHDR();}catch(const std::exception& e){Native().Fail(e.what());}originalHDR(self,geometry,parameter);}
void HDRCS(void* self,void* geometry,void* parameter){try{Native().ComposeBeforeHDR();}catch(const std::exception& e){Native().Fail(e.what());}originalHDRCS(self,geometry,parameter);}

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
    if(pass){pass->passType=5;}
    return result;
}
// Restore the state we change; preserve unrelated constant-buffer bindings.
struct State {
    ID3D11DeviceContext* c;bool geometry,direct;ID3D11DeviceContext1* c1=nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
    ID3D11RenderTargetView* rt[8]{};Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    ID3D11ShaderResourceView* srv[21]{};ID3D11SamplerState* samplers[16]{};
    ID3D11Buffer* vb[14]{};ID3D11Buffer* pb[14]{};UINT vf[14]{},vn[14]{},pf[14]{},pn[14]{};
    float factors[4];UINT mask=0,ref=0,nvp=16,nsc=16;D3D11_VIEWPORT vp[16]{};D3D11_RECT sc[16]{};
    D3D11_PRIMITIVE_TOPOLOGY topology;
    explicit State(ID3D11DeviceContext* x,ID3D11DeviceContext1* cached,bool g=false,bool immediate=false):c(x),geometry(g),direct(immediate),c1(cached){

        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->IAGetInputLayout(&layout);if(!direct)c->IAGetPrimitiveTopology(&topology);
        c->OMGetRenderTargets(8,rt,&dsv);c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&ref);if(!direct){c->RSGetState(&raster);c->RSGetViewports(&nvp,vp);c->RSGetScissorRects(&nsc,sc);}
        c->PSGetShaderResources(0,21,srv);if(geometry){for(UINT slot:{5u,10u,13u,14u})c->PSGetSamplers(slot,1,&samplers[slot]);}else c->PSGetSamplers(0,1,&samplers[0]);
        if(!geometry){if(c1)c1->PSGetConstantBuffers1(11,1,&pb[11],&pf[11],&pn[11]);else c->PSGetConstantBuffers(11,1,&pb[11]);}
        if(geometry){
            for(UINT slot:{10u,13u}){if(c1)c1->VSGetConstantBuffers1(slot,1,&vb[slot],&vf[slot],&vn[slot]);else c->VSGetConstantBuffers(slot,1,&vb[slot]);}
            for(UINT slot:{11u,12u,13u}){if(c1)c1->PSGetConstantBuffers1(slot,1,&pb[slot],&pf[slot],&pn[slot]);else c->PSGetConstantBuffers(slot,1,&pb[slot]);}
        }
    }
    ~State(){

        ID3D11ShaderResourceView* none[21]{};c->PSSetShaderResources(0,21,none);c->OMSetRenderTargets(8,rt,dsv.Get());c->PSSetShaderResources(0,21,srv);if(geometry){for(UINT slot:{5u,10u,13u,14u})c->PSSetSamplers(slot,1,&samplers[slot]);}else c->PSSetSamplers(0,1,&samplers[0]);
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->IASetInputLayout(layout.Get());if(!direct)c->IASetPrimitiveTopology(topology);
        if(!geometry){if(c1)c1->PSSetConstantBuffers1(11,1,&pb[11],&pf[11],&pn[11]);else c->PSSetConstantBuffers(11,1,&pb[11]);}
        if(geometry){
            for(UINT slot:{10u,13u}){if(c1)c1->VSSetConstantBuffers1(slot,1,&vb[slot],&vf[slot],&vn[slot]);else c->VSSetConstantBuffers(slot,1,&vb[slot]);}
            for(UINT slot:{11u,12u,13u}){if(c1)c1->PSSetConstantBuffers1(slot,1,&pb[slot],&pf[slot],&pn[slot]);else c->PSSetConstantBuffers(slot,1,&pb[slot]);}
        }
        c->OMSetBlendState(blend.Get(),factors,mask);c->OMSetDepthStencilState(depth.Get(),ref);if(!direct){c->RSSetState(raster.Get());c->RSSetViewports(nvp,vp);c->RSSetScissorRects(nsc,sc);}
        for(auto* p:rt)if(p)p->Release();for(auto* p:srv)if(p)p->Release();for(auto* p:samplers)if(p)p->Release();for(auto* p:vb)if(p)p->Release();for(auto* p:pb)if(p)p->Release();
    }
};
void snapshot(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Buffer* source,Microsoft::WRL::ComPtr<ID3D11Buffer>& dest){
    D3D11_BUFFER_DESC sd{},dd{};source->GetDesc(&sd);if(dest)dest->GetDesc(&dd);
    if(!dest||sd.ByteWidth!=dd.ByteWidth){dest.Reset();sd.Usage=D3D11_USAGE_DEFAULT;sd.CPUAccessFlags=0;sd.MiscFlags=0;hr(d->CreateBuffer(&sd,nullptr,&dest));}
    c->CopyResource(dest.Get(),source);
}
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

}
bool NativeOptics::IsOwnedPass(const RE::BSRenderPass* p)const{return p&&p->passType==5&&p->passEnum==0x61A&&p->shader==RE::BSUtilityShader::GetSingleton()&&tagged(p->geometry);}
bool NativeOptics::Reject(unsigned reason,const char* description){
    ++healthRejected;
    const auto bit=1u<<reason;
    if(!(reportedRejections&bit)){reportedRejections|=bit;REX::WARN("Bottle capture rejected [{}]: {}; retaining vanilla rendering",reason,description);}
    return false;
}
void NativeOptics::Fail(const char* why){failed=true;REX::ERROR("Native optics disabled: {}",why);}
void NativeOptics::Initialize(ID3D11Device* d,const std::filesystem::path& path){
    backgroundView.Reset();backgroundIdentity.Reset();
    regional.Initialize(d,path);
    device=d;auto code=[&](const char* name){Microsoft::WRL::ComPtr<ID3DBlob> b;hr(D3DReadFileToBlob((path/name).c_str(),&b));return b;};
    auto scaled=code("OpticalCompose.scaled.ps.cso");hr(d->CreatePixelShader(scaled->GetBufferPointer(),scaled->GetBufferSize(),nullptr,&scaledComposePS));
    auto v=code("OpticalParts.vs.cso"),p=code("OpticalParts.ps.cso"),sv=code("OpticalCompose.vs.cso"),sp=code("OpticalCompose.ps.cso");
    hr(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&producerVS));hr(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&producerPS));
    hr(d->CreateVertexShader(sv->GetBufferPointer(),sv->GetBufferSize(),nullptr,&composeVS));hr(d->CreatePixelShader(sp->GetBufferPointer(),sp->GetBufferSize(),nullptr,&composePS));
    D3D11_INPUT_ELEMENT_DESC e[]={{"POSITION",0,DXGI_FORMAT_R16G16B16A16_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},{"BINORMAL",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    hr(d->CreateInputLayout(e,4,v->GetBufferPointer(),v->GetBufferSize(),&layout));hr(constants.Initialize(d,sizeof(Constants)));
    D3D11_BLEND_DESC bd{};for(auto& b:bd.RenderTarget)b.RenderTargetWriteMask=15;hr(d->CreateBlendState(&bd,&opaque));
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;hr(d->CreateDepthStencilState(&dd,&nearest));dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;hr(d->CreateDepthStencilState(&dd,&noDepth));
    auto directCode=code("OpticalParts.direct.ps.cso");hr(d->CreatePixelShader(directCode->GetBufferPointer(),directCode->GetBufferSize(),nullptr,&directPS));
    D3D11_BLEND_DESC directBD{};auto& db=directBD.RenderTarget[0];db.BlendEnable=TRUE;db.SrcBlend=D3D11_BLEND_ONE;db.DestBlend=D3D11_BLEND_SRC1_COLOR;db.BlendOp=D3D11_BLEND_OP_ADD;db.SrcBlendAlpha=D3D11_BLEND_ZERO;db.DestBlendAlpha=D3D11_BLEND_ONE;db.BlendOpAlpha=D3D11_BLEND_OP_ADD;db.RenderTargetWriteMask=15;hr(d->CreateBlendState(&directBD,&directBlend));
    D3D11_DEPTH_STENCIL_DESC directDD{};directDD.DepthEnable=TRUE;directDD.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;directDD.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;hr(d->CreateDepthStencilState(&directDD,&directDepth));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;hr(d->CreateRasterizerState(&rd,&screen));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;hr(d->CreateSamplerState(&sd,&linear));
}
void NativeOptics::BeginFrame(unsigned f){if(frame==f)return;
    if(++healthFrames>=1800){
        REX::INFO("Health (1800 observed bottle frames): captures={} opticalDraws={} distantDraws={} compositions={} fullScreen={} rejected={} records={} disabled={}",healthCaptures,healthDraws,healthDistant,healthComposes,healthFull,healthRejected,records.size(),failed);
        healthFrames=0;healthCaptures=healthDraws=healthDistant=healthComposes=healthFull=healthRejected=0;
    }
    if(captured&&(drawn==0||composedFrame!=frame)){if(++missed>=3)Fail("registration or HDR composition did not complete for three active frames; restored baseline rendering");}else missed=0;
    frame=f;drawn=0;captured=0;sourceIdentity.Reset();mainDepth.Reset();for(auto i=records.begin();i!=records.end();)if(f-i->second.frame>120)i=records.erase(i);else++i;}
void NativeOptics::Prepare(ID3D11DeviceContext* c,UINT w,UINT h,DXGI_FORMAT format){

    if(width!=w||height!=h||sceneFormat!=format){
        REX::INFO("Native optical target: {}x{}, DXGI format {}",w,h,unsigned(format));

        // Keep signed distortion and light/transmission precision unchanged. Only
        // the final output adopts the native format for both UAV and raw copy-back.
        width=w;height=h;sceneFormat=format;preparedFrame=~0u;auto make=[&](Target& t,UINT tw,UINT th,bool writable=false){t={};D3D11_TEXTURE2D_DESC td{};td.Width=tw;td.Height=th;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=writable?sceneFormat:DXGI_FORMAT_R16G16B16A16_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(writable){UINT support=0;if(SUCCEEDED(device->CheckFormatSupport(td.Format,&support))&&(support&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;}
        auto result=device->CreateTexture2D(&td,nullptr,&t.texture);if(FAILED(result)&&writable){td.BindFlags&=~D3D11_BIND_UNORDERED_ACCESS;result=device->CreateTexture2D(&td,nullptr,&t.texture);}hr(result);hr(device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.srv));hr(device->CreateRenderTargetView(t.texture.Get(),nullptr,&t.rtv));};
        // Immutable size data is shared by all bottles until the target changes.
        float size[4]{float(w),float(h),0,0};D3D11_BUFFER_DESC sizeDesc{};sizeDesc.ByteWidth=sizeof(size);sizeDesc.Usage=D3D11_USAGE_IMMUTABLE;sizeDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sizeData{};sizeData.pSysMem=size;viewportConstants.Reset();hr(device->CreateBuffer(&sizeDesc,&sizeData,&viewportConstants));

        make(light,w,h);make(transmission,w,h);make(distortion,w,h);make(composed,w,h,true);nearestTexture.Reset();nearestDSV.Reset();nearestSRV.Reset();D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_TYPELESS;td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;hr(device->CreateTexture2D(&td,nullptr,&nearestTexture));
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;hr(device->CreateDepthStencilView(nearestTexture.Get(),&dv,&nearestDSV));
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;hr(device->CreateShaderResourceView(nearestTexture.Get(),&sv,&nearestSRV));
    }
    if(preparedFrame!=frame){
        const float z[4]{};
        {c->ClearRenderTargetView(light.rtv.Get(),z);}
        {c->ClearRenderTargetView(transmission.rtv.Get(),z);}
        {c->ClearRenderTargetView(distortion.rtv.Get(),z);}
        {c->ClearDepthStencilView(nearestDSV.Get(),D3D11_CLEAR_DEPTH,1,0);}
        preparedFrame=frame;
    }
}
bool NativeOptics::Capture(ID3D11DeviceContext* c,const Constants& k,ID3D11ShaderResourceView* cubeView,ID3D11ShaderResourceView* sunView,ID3D11ShaderResourceView* noiseView,ID3D11SamplerState* shadowSampler,ID3D11SamplerState* noiseSamplerView){
    if(failed||!device||!activeGeometry||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return Reject(0,"inactive renderer, geometry or unsupported context");
    if(records.size()>=256&&!records.contains(activeGeometry))return Reject(1,"geometry record capacity exceeded");
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&rt,&depth);if(!rt||!depth)return Reject(2,"missing colour or depth target");
    if(context.Get()!=c){context=c;context1.Reset();c->QueryInterface(IID_PPV_ARGS(&context1));}
    auto& r=records[activeGeometry];rt->GetResource(r.colour.ReleaseAndGetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> colour;if(FAILED(r.colour.As(&colour)))return Reject(3,"colour target is not Texture2D");D3D11_TEXTURE2D_DESC td{};colour->GetDesc(&td);
    if(td.SampleDesc.Count!=1||td.ArraySize!=1||!SupportedSceneColour(td.Format))return Reject(4,"unsupported colour format, multisampling or array");
    UINT n=1;c->RSGetViewports(&n,&r.viewport);if(n!=1||r.viewport.TopLeftX!=0||r.viewport.TopLeftY!=0||!std::isfinite(r.viewport.Width)||!std::isfinite(r.viewport.Height)||r.viewport.Width<=0||r.viewport.Height<=0||r.viewport.Width>td.Width||r.viewport.Height>td.Height)return Reject(5,"unsupported viewport");
    Microsoft::WRL::ComPtr<ID3D11Buffer> g,l;auto* c1=context1.Get();
    if(c1){c1->VSGetConstantBuffers1(2,1,&g,&r.gf,&r.gn);c1->PSGetConstantBuffers1(2,1,&l,&r.lf,&r.ln);}else{c->VSGetConstantBuffers(2,1,&g);c->PSGetConstantBuffers(2,1,&l);}if(!g||!l)return Reject(6,"missing native geometry or lighting constants");
    Microsoft::WRL::ComPtr<ID3D11Resource> dr;depth->GetResource(&dr);
    if(dr!=r.depthResource||!r.depth){r.depth.Reset();r.depthResource=dr;D3D11_DEPTH_STENCIL_VIEW_DESC vd{};depth->GetDesc(&vd);D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        if(vd.Format==DXGI_FORMAT_D24_UNORM_S8_UINT)sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;else if(vd.Format==DXGI_FORMAT_D32_FLOAT)sd.Format=DXGI_FORMAT_R32_FLOAT;else return Reject(7,"unsupported depth format");
        if(FAILED(device->CreateShaderResourceView(dr.Get(),&sd,&r.depth))){r.depthResource.Reset();Fail("native scene depth is not shader-readable");return Reject(8,"native depth view creation failed");}}
    snapshot(device.Get(),c,g.Get(),r.geometry);snapshot(device.Get(),c,l.Get(),r.lighting);
    r.constants=k;r.frame=frame;r.cube=cubeView;r.sun=sunView;r.noise=noiseView;r.shadow=shadowSampler;r.noiseSampler=noiseSamplerView;
    c->PSGetShaderResources(0,1,r.base.ReleaseAndGetAddressOf());c->PSGetShaderResources(1,1,r.normal.ReleaseAndGetAddressOf());c->PSGetShaderResources(7,1,r.mask.ReleaseAndGetAddressOf());
    // Retain the original cube filtering/minimum LOD across the delayed draw.
    // The liquid continues using .24's separate s14 sampler.
    c->PSGetSamplers(5,1,r.cubeSampler.ReleaseAndGetAddressOf());
    ++captured;++healthCaptures;return true;
}
bool NativeOptics::DrawDistant(ID3D11DeviceContext* c,const Constants& k,ID3D11ShaderResourceView* cube,ID3D11ShaderResourceView* sun,UINT count,UINT start,INT base,void(STDMETHODCALLTYPE* original)(ID3D11DeviceContext*,UINT,UINT,INT)){
    if(failed||!device||!directPS||!activeGeometry||k.optics.w<2)return false;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt;Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&rt,&depth);if(!rt||!depth)return false;
    if(context.Get()!=c){context=c;context1.Reset();c->QueryInterface(IID_PPV_ARGS(&context1));}
    Microsoft::WRL::ComPtr<ID3D11Buffer> g,l;UINT gf=0,gn=0,lf=0,ln=0;
    if(context1){context1->VSGetConstantBuffers1(2,1,&g,&gf,&gn);context1->PSGetConstantBuffers1(2,1,&l,&lf,&ln);}else{c->VSGetConstantBuffers(2,1,&g);c->PSGetConstantBuffers(2,1,&l);}if(!g||!l)return false;
    // Use the current colour draw's buffers directly, without delayed snapshots.

    State state(c,context1.Get(),true,true);busy=true;
    try{
        c->VSSetShader(producerVS.Get(),nullptr,0);c->PSSetShader(directPS.Get(),nullptr,0);c->IASetInputLayout(layout.Get());
        auto* gb=g.Get();auto* lb=l.Get();if(context1){context1->VSSetConstantBuffers1(10,1,&gb,&gf,&gn);context1->PSSetConstantBuffers1(12,1,&lb,&lf,&ln);}else{c->VSSetConstantBuffers(10,1,&gb);c->PSSetConstantBuffers(12,1,&lb);}
        ID3D11Buffer* cb=nullptr;hr(constants.Upload(c,&k,cb));c->VSSetConstantBuffers(13,1,&cb);c->PSSetConstantBuffers(13,1,&cb);
        c->OMSetBlendState(directBlend.Get(),nullptr,~0u);c->OMSetDepthStencilState(directDepth.Get(),0);
        c->PSSetShaderResources(14,1,&cube);c->PSSetShaderResources(20,1,&sun);auto* samp=linear.Get();c->PSSetSamplers(14,1,&samp);
        // Shadow sampler comes from the current renderer binding below.
        original(c,count,start,base);records.erase(activeGeometry);++healthDistant;
    }catch(...){busy=false;throw;}busy=false;return true;
}
void NativeOptics::DrawRegistered(ID3D11DeviceContext* c,const void* geometry,UINT count,UINT start,INT base,void(STDMETHODCALLTYPE* original)(ID3D11DeviceContext*,UINT,UINT,INT)){
    auto i=records.find(geometry);if(failed||i==records.end()||i->second.frame!=frame)return;auto& r=i->second;

    D3D11_TEXTURE2D_DESC td{};Microsoft::WRL::ComPtr<ID3D11Texture2D> t;hr(r.colour.As(&t));t->GetDesc(&td);Prepare(c,td.Width,td.Height,td.Format);
    State state(c,context1.Get(),true,false);busy=true;try{

        ID3D11ShaderResourceView* none[21]{};c->PSSetShaderResources(0,21,none);
        ID3D11RenderTargetView* outputs[]{light.rtv.Get(),transmission.rtv.Get(),distortion.rtv.Get()};c->OMSetRenderTargets(3,outputs,nearestDSV.Get());c->OMSetBlendState(opaque.Get(),nullptr,~0u);c->OMSetDepthStencilState(nearest.Get(),0);
        c->RSSetViewports(1,&r.viewport);c->VSSetShader(producerVS.Get(),nullptr,0);c->PSSetShader(producerPS.Get(),nullptr,0);c->IASetInputLayout(layout.Get());
        auto* g=r.geometry.Get();auto* l=r.lighting.Get();if(state.c1){state.c1->VSSetConstantBuffers1(10,1,&g,&r.gf,&r.gn);state.c1->PSSetConstantBuffers1(12,1,&l,&r.lf,&r.ln);}else{c->VSSetConstantBuffers(10,1,&g);c->PSSetConstantBuffers(12,1,&l);}
        ID3D11Buffer* cb=nullptr;{hr(constants.Upload(c,&r.constants,cb));}c->VSSetConstantBuffers(13,1,&cb);c->PSSetConstantBuffers(13,1,&cb);
        cb=viewportConstants.Get();c->PSSetConstantBuffers(11,1,&cb);
        ID3D11ShaderResourceView* resources[21]{};resources[0]=r.base.Get();resources[1]=r.normal.Get();resources[7]=r.mask.Get();resources[14]=r.cube.Get();resources[16]=r.noise.Get();resources[17]=r.depth.Get();resources[20]=r.sun.Get();c->PSSetShaderResources(0,21,resources);
        auto* s=linear.Get();c->PSSetSamplers(14,1,&s);s=r.shadow.Get();c->PSSetSamplers(10,1,&s);s=r.noiseSampler.Get();c->PSSetSamplers(13,1,&s);s=r.cubeSampler.Get();c->PSSetSamplers(5,1,&s);
        original(c,count,start,base);sourceIdentity=r.colour;mainDepth=r.depth;sceneViewport=r.viewport;++drawn;++healthDraws;

    }catch(...){busy=false;throw;}busy=false;
}
bool NativeOptics::ComposeBeforeHDR(){
    auto* c=context.Get();
    if(!c||busy||failed||!drawn||composedFrame==frame)return false;

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
    if(scaledViewport && (!activeViewportBuffer||activeWidth!=sceneViewport.Width/width||activeHeight!=sceneViewport.Height/height)){
        activeWidth=sceneViewport.Width/width;activeHeight=sceneViewport.Height/height;
        float region[4]{activeWidth,activeHeight,0,0};D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=region;activeViewportBuffer.Reset();hr(device->CreateBuffer(&desc,&data,&activeViewportBuffer));
    }
    bool useRegion=regional.Prepare(device.Get(),c,source.Get(),composed.texture.Get());
    State state(c,context1.Get());busy=true;try{
        if(useRegion){
            ID3D11ShaderResourceView* inputs[]{backgroundView.Get(),light.srv.Get(),transmission.srv.Get(),distortion.srv.Get(),mainDepth.Get(),nearestSRV.Get()};
            regional.Draw(c,inputs,linear.Get(),activeViewportBuffer.Get(),scaledViewport,width,height);
            composedFrame=frame;++healthComposes;

        }else{

        if(scaledViewport){auto* cb=activeViewportBuffer.Get();c->PSSetConstantBuffers(11,1,&cb);}
        ID3D11ShaderResourceView* none[21]{};c->PSSetShaderResources(0,21,none);auto* output=composed.rtv.Get();c->OMSetRenderTargets(1,&output,nullptr);c->OMSetDepthStencilState(noDepth.Get(),0);c->OMSetBlendState(opaque.Get(),nullptr,~0u);c->RSSetState(screen.Get());
        D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&vp);c->VSSetShader(composeVS.Get(),nullptr,0);c->PSSetShader(scaledViewport?scaledComposePS.Get():composePS.Get(),nullptr,0);c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* in[]{backgroundView.Get(),light.srv.Get(),transmission.srv.Get(),distortion.srv.Get(),mainDepth.Get(),nearestSRV.Get()};c->PSSetShaderResources(0,6,in);auto* s=linear.Get();c->PSSetSamplers(0,1,&s);
        c->Draw(3,0);

        ID3D11ShaderResourceView* nullViews[21]{};c->PSSetShaderResources(0,21,nullViews);c->OMSetRenderTargets(0,nullptr,nullptr);
        // The final target matches the native format (including RGB-only HDR).
        // RTV/UAV stores perform the native quantization; raw copy needs no conversion.
        // Resolve the completed optical result into the original native scene.
        // This is an output copy, not an additional background scene capture.
        c->CopyResource(source.Get(),composed.texture.Get());++healthFull;

        composedFrame=frame;++healthComposes;

        }
    }catch(...){busy=false;throw;}busy=false;
    // Restore the preceding world-pass state; the engine sets up HDR next.
    return true;
}
}

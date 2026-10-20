#include "PCH.h"
#include "SceneColour.h"
#include "RegionComposer.h"
#include <d3d11_1.h>
namespace nuka {
namespace {
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("Regional resources unavailable");}
struct ComputeState {
 ID3D11DeviceContext* c;Microsoft::WRL::ComPtr<ID3D11DeviceContext1> c1;
 Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;ID3D11ClassInstance* classes[256]{};UINT count=256;
 ID3D11ShaderResourceView* views[128]{};ID3D11UnorderedAccessView* uavs[8]{};
 Microsoft::WRL::ComPtr<ID3D11Buffer> cb;UINT first=0,num=0;Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
 explicit ComputeState(ID3D11DeviceContext* context):c(context){
  c->QueryInterface(IID_PPV_ARGS(&c1));c->CSGetShader(&shader,classes,&count);c->CSGetShaderResources(0,128,views);c->CSGetUnorderedAccessViews(0,8,uavs);c->CSGetSamplers(0,1,&sampler);
  if(c1)c1->CSGetConstantBuffers1(11,1,&cb,&first,&num);else c->CSGetConstantBuffers(11,1,&cb);
 }
 void Clear(){ID3D11ShaderResourceView* empty[128]{};ID3D11UnorderedAccessView* noUavs[8]{};c->CSSetShaderResources(0,128,empty);c->CSSetUnorderedAccessViews(0,8,noUavs,nullptr);}
 ~ComputeState(){Clear();c->CSSetUnorderedAccessViews(0,8,uavs,nullptr);c->CSSetShaderResources(0,128,views);auto* b=cb.Get();if(c1)c1->CSSetConstantBuffers1(11,1,&b,&first,&num);else c->CSSetConstantBuffers(11,1,&b);auto* s=sampler.Get();c->CSSetSamplers(0,1,&s);c->CSSetShader(shader.Get(),classes,count);for(auto* v:views)if(v)v->Release();for(auto* u:uavs)if(u)u->Release();for(UINT i=0;i<count;i++)if(classes[i])classes[i]->Release();}
};
}
void RegionComposer::Initialize(ID3D11Device* d,const std::filesystem::path& path){
 try{
  auto shader=[&](const char* name,auto& target){Microsoft::WRL::ComPtr<ID3DBlob> code;check(D3DReadFileToBlob((path/name).c_str(),&code));check(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&target));};
  shader("RegionBounds.reduce.cs.cso",reduce);shader("RegionBounds.finish.cs.cso",finish);shader("RegionBounds.copy.cs.cso",copy);shader("OpticalCompose.region.cs.cso",compose);shader("OpticalCompose.region-scaled.cs.cso",scaledCompose);
  auto buffer=[&](UINT bytes,bool indirect,auto& b,auto& view,auto& uav){D3D11_BUFFER_DESC bd{};bd.ByteWidth=bytes;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS|(indirect?D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS:0);check(d->CreateBuffer(&bd,nullptr,&b));
   D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;sd.BufferEx.NumElements=bytes/4;sd.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;check(d->CreateShaderResourceView(b.Get(),&sd,&view));
   D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_TYPELESS;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=bytes/4;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;check(d->CreateUnorderedAccessView(b.Get(),&ud,&uav));};
  buffer(16,false,bounds,boundsView,boundsUav);buffer(32,true,args,argsView,argsUav);ready=true;
 }catch(const std::exception& e){ready=false;REX::WARN("Regional composition unavailable, retaining full-screen path: {}",e.what());}
}
bool RegionComposer::Prepare(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* source,ID3D11Texture2D* output){
 if(!ready)return false;D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);if(!(desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS)||!SupportedSceneColour(desc.Format)||desc.SampleDesc.Count!=1)return false;
 // Source must still be bound as an RTV: D3D then guarantees it is not aliased
 // as a shader input in any graphics stage when we transition it to UAV output.
 Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt;c->OMGetRenderTargets(1,&rt,nullptr);Microsoft::WRL::ComPtr<ID3D11Resource> resource;if(rt)rt->GetResource(&resource);if(resource.Get()!=source)return false;
 if(sceneIdentity.Get()!=source){sceneIdentity=source;sceneUav.Reset();d->CreateUnorderedAccessView(source,nullptr,&sceneUav);}
 if(outputIdentity.Get()!=output){outputIdentity=output;outputUav.Reset();outputView.Reset();d->CreateUnorderedAccessView(output,nullptr,&outputUav);d->CreateShaderResourceView(output,nullptr,&outputView);}
 return sceneUav&&outputUav&&outputView;
}
void RegionComposer::Draw(ID3D11DeviceContext* c,ID3D11ShaderResourceView* const* inputs,ID3D11SamplerState* sampler,ID3D11Buffer* viewport,bool scaled,unsigned ow,unsigned oh){
 ComputeState saved(c);saved.Clear();c->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* noPS[21]{};c->PSSetShaderResources(0,21,noPS);
 UINT clear[4]{~0u,~0u,~0u,~0u};
 c->ClearUnorderedAccessViewUint(boundsUav.Get(),clear);auto* u=boundsUav.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);auto* field=inputs[3];c->CSSetShaderResources(0,1,&field);c->CSSetShader(reduce.Get(),nullptr,0);c->Dispatch((ow+15)/16,(oh+15)/16,1);saved.Clear();
 ID3D11ShaderResourceView* finishInputs[]{boundsView.Get(),inputs[0],inputs[3]};c->CSSetShaderResources(0,3,finishInputs);u=argsUav.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CSSetShader(finish.Get(),nullptr,0);c->Dispatch(1,1,1);saved.Clear();
 c->CSSetShaderResources(0,6,inputs);auto* a=argsView.Get();c->CSSetShaderResources(6,1,&a);u=outputUav.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CSSetSamplers(0,1,&sampler);if(scaled)c->CSSetConstantBuffers(11,1,&viewport);c->CSSetShader(scaled?scaledCompose.Get():compose.Get(),nullptr,0);c->DispatchIndirect(args.Get(),0);
 saved.Clear();
 auto* v=outputView.Get();c->CSSetShaderResources(0,1,&v);c->CSSetShaderResources(6,1,&a);u=sceneUav.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CSSetShader(copy.Get(),nullptr,0);c->DispatchIndirect(args.Get(),0);

}
}

#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <string>
namespace nuka {
// Narrow Sunset trial: freeze native draw inputs, execute the unchanged draw after optics.
// No game object pointers are retained. Unsupported pipeline state keeps the original draw.
class FrostQueue {
 template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
 struct Item {Ptr<ID3D11CommandList> commands;Ptr<ID3D11DeviceContext> context;};
 std::vector<Item> items;std::mutex mutex;unsigned frame=~0u;size_t bytes=0;
 Ptr<ID3D11Texture2D> frostDepth;Ptr<ID3D11DepthStencilView> frostDSV;
 D3D11_TEXTURE2D_DESC frostDesc{};D3D11_DEPTH_STENCIL_VIEW_DESC frostView{};
 static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("Frost snapshot failed");}
public:
 std::string lastFailure;
 unsigned captured=0,replayed=0,rejected=0,expired=0;
 bool Capture(ID3D11DeviceContext* c,unsigned f,UINT count,UINT start,INT base) {
  std::lock_guard lock(mutex);
  if(frame!=f){expired+=unsigned(items.size());items.clear();bytes=0;frame=f;}
  if(items.size()>=128||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){++rejected;return false;}
  try {
   Ptr<ID3D11Device> device;c->GetDevice(&device);
   Ptr<ID3D11DeviceContext> dc;Check(device->CreateDeferredContext(0,&dc));
   Ptr<ID3D11DeviceContext1> c1,d1;c->QueryInterface(IID_PPV_ARGS(&c1));dc.As(&d1);
   Ptr<ID3D11HullShader> hs;Ptr<ID3D11DomainShader> ds;Ptr<ID3D11GeometryShader> gs;
   c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);
   Ptr<ID3D11Predicate> predicate;BOOL predicateValue;c->GetPredication(&predicate,&predicateValue);
   if(hs||ds||gs||predicate)throw std::runtime_error("Unsupported frost pipeline");
   Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;UINT nv=0,np=0;
   c->VSGetShader(&vs,nullptr,&nv);c->PSGetShader(&ps,nullptr,&np);
   if(!vs||!ps||nv||np)throw std::runtime_error("Unsupported frost shaders");
   dc->VSSetShader(vs.Get(),nullptr,0);dc->PSSetShader(ps.Get(),nullptr,0);
   Ptr<ID3D11RenderTargetView> rt;Ptr<ID3D11DepthStencilView> depth;
   c->OMGetRenderTargets(1,&rt,&depth);if(!rt||!depth)throw std::runtime_error("No frost colour/depth");
   Ptr<ID3D11Resource> resource;rt->GetResource(&resource);Ptr<ID3D11Texture2D> texture;Check(resource.As(&texture));
   D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
   if(td.SampleDesc.Count!=1||td.ArraySize!=1||(td.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&td.Format!=DXGI_FORMAT_R11G11B10_FLOAT))throw std::runtime_error("Unsupported frost target");
   auto* target=rt.Get();dc->OMSetRenderTargets(1,&target,depth.Get());
   Ptr<ID3D11BlendState> blend;float factor[4];UINT mask;c->OMGetBlendState(&blend,factor,&mask);
   D3D11_BLEND_DESC bd{};if(blend)blend->GetDesc(&bd);
   if(!blend||!bd.RenderTarget[0].BlendEnable||bd.RenderTarget[0].BlendOp!=D3D11_BLEND_OP_ADD)throw std::runtime_error("Not frost colour blend");
   dc->OMSetBlendState(blend.Get(),factor,mask);
   Ptr<ID3D11DepthStencilState> depthState;UINT ref;c->OMGetDepthStencilState(&depthState,&ref);
   D3D11_DEPTH_STENCIL_DESC dd{};if(depthState)depthState->GetDesc(&dd);
   if(!depthState||!dd.DepthEnable||dd.DepthWriteMask!=D3D11_DEPTH_WRITE_MASK_ZERO||dd.StencilEnable)throw std::runtime_error("Frost depth must be read-only");
   dc->OMSetDepthStencilState(depthState.Get(),ref);
   Ptr<ID3D11RasterizerState> raster;c->RSGetState(&raster);dc->RSSetState(raster.Get());
   D3D11_VIEWPORT vp[16];UINT n=16;c->RSGetViewports(&n,vp);dc->RSSetViewports(n,vp);
   D3D11_RECT sc[16];n=16;c->RSGetScissorRects(&n,sc);dc->RSSetScissorRects(n,sc);
   Ptr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);dc->IASetInputLayout(layout.Get());
   D3D11_PRIMITIVE_TOPOLOGY topology;c->IAGetPrimitiveTopology(&topology);dc->IASetPrimitiveTopology(topology);
   Ptr<ID3D11Buffer> index;DXGI_FORMAT format;UINT offset;c->IAGetIndexBuffer(&index,&format,&offset);dc->IASetIndexBuffer(index.Get(),format,offset);
   ID3D11Buffer* vb[32]{};UINT stride[32]{},offsets[32]{};c->IAGetVertexBuffers(0,32,vb,stride,offsets);dc->IASetVertexBuffers(0,32,vb,stride,offsets);for(auto* p:vb)if(p)p->Release();
   size_t snapshotBytes=0;
   // Freeze contents now, not when the command list executes: engine CBs are reused.
   for(unsigned stage=0;stage<2;++stage)for(UINT slot=0;slot<14;++slot){
    Ptr<ID3D11Buffer> original,copy;UINT first=0,num=0;
    if(c1){if(stage)c1->PSGetConstantBuffers1(slot,1,&original,&first,&num);else c1->VSGetConstantBuffers1(slot,1,&original,&first,&num);}
    else {if(stage)c->PSGetConstantBuffers(slot,1,&original);else c->VSGetConstantBuffers(slot,1,&original);}
    if(!original)continue;
    D3D11_BUFFER_DESC d{};original->GetDesc(&d);snapshotBytes+=d.ByteWidth;
    if(bytes+snapshotBytes>16*1024*1024)throw std::runtime_error("Frost snapshot budget");
    d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;Check(device->CreateBuffer(&d,nullptr,&copy));
    c->CopyResource(copy.Get(),original.Get());auto* b=copy.Get();
    if(c1&&d1){if(stage)d1->PSSetConstantBuffers1(slot,1,&b,&first,&num);else d1->VSSetConstantBuffers1(slot,1,&b,&first,&num);}
    else {if(stage)dc->PSSetConstantBuffers(slot,1,&b);else dc->VSSetConstantBuffers(slot,1,&b);}
   }
   for(unsigned stage=0;stage<2;++stage){
    ID3D11ShaderResourceView* views[128]{};ID3D11SamplerState* samplers[16]{};
    if(stage){c->PSGetShaderResources(0,128,views);c->PSGetSamplers(0,16,samplers);dc->PSSetShaderResources(0,128,views);dc->PSSetSamplers(0,16,samplers);}
    else {c->VSGetShaderResources(0,128,views);c->VSGetSamplers(0,16,samplers);dc->VSSetShaderResources(0,128,views);dc->VSSetSamplers(0,16,samplers);}
    for(auto* p:views)if(p)p->Release();for(auto* p:samplers)if(p)p->Release();
   }
   // A native frost shell is double-sided and additive. Replaying all its surfaces
   // over the completed liquid adds the rear wall and inner bottom unattenuated.
   // Select the nearest surviving native fragment before shading, in private depth.
   // Copy at REPLAY time so foreground depth remains current. Never write game depth.
   Ptr<ID3D11Resource> depthResource;depth->GetResource(&depthResource);
   Ptr<ID3D11Texture2D> depthTexture;Check(depthResource.As(&depthTexture));
   D3D11_TEXTURE2D_DESC z{};depthTexture->GetDesc(&z);
   D3D11_DEPTH_STENCIL_VIEW_DESC zv{};depth->GetDesc(&zv);zv.Flags=0;
   if(z.ArraySize!=1||z.MipLevels!=1||z.SampleDesc.Count!=1||zv.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D||dd.DepthFunc!=D3D11_COMPARISON_LESS_EQUAL)
    throw std::runtime_error("Unsupported nearest frost depth");
   z.Usage=D3D11_USAGE_DEFAULT;z.BindFlags=D3D11_BIND_DEPTH_STENCIL;z.CPUAccessFlags=0;z.MiscFlags=0;
   if(!frostDepth||frostDesc.Width!=z.Width||frostDesc.Height!=z.Height||frostDesc.Format!=z.Format||frostView.Format!=zv.Format){
    Ptr<ID3D11Texture2D> fresh;Ptr<ID3D11DepthStencilView> freshView;
    Check(device->CreateTexture2D(&z,nullptr,&fresh));Check(device->CreateDepthStencilView(fresh.Get(),&zv,&freshView));
    frostDepth=fresh;frostDSV=freshView;frostDesc=z;frostView=zv;
   }
   Ptr<ID3D11BlendState> noColour;D3D11_BLEND_DESC zero{};Check(device->CreateBlendState(&zero,&noColour));
   Ptr<ID3D11DepthStencilState> writeNearest,readNearest;
   dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;Check(device->CreateDepthStencilState(&dd,&writeNearest));
   dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_EQUAL;Check(device->CreateDepthStencilState(&dd,&readNearest));
   dc->OMSetRenderTargets(0,nullptr,nullptr);
   dc->CopyResource(frostDepth.Get(),depthTexture.Get());
   dc->OMSetRenderTargets(1,&target,frostDSV.Get());
   dc->OMSetBlendState(noColour.Get(),factor,mask);dc->OMSetDepthStencilState(writeNearest.Get(),ref);
   dc->DrawIndexed(count,start,base); // original PS keeps alpha/discard coverage exact
   dc->OMSetBlendState(blend.Get(),factor,mask);dc->OMSetDepthStencilState(readNearest.Get(),ref);
   dc->DrawIndexed(count,start,base);Ptr<ID3D11CommandList> commands;Check(dc->FinishCommandList(FALSE,&commands));
   items.push_back({commands,c});bytes+=snapshotBytes;++captured;return true;
  }catch(const std::exception& e){lastFailure=e.what();++rejected;return false;}
 }
 void Replay(unsigned f){std::lock_guard lock(mutex);if(frame==f){for(auto& item:items){item.context->ExecuteCommandList(item.commands.Get(),TRUE);++replayed;}}else expired+=unsigned(items.size());items.clear();bytes=0;}
 size_t Pending(){std::lock_guard lock(mutex);return items.size();}
};
inline FrostQueue& Frost(){static FrostQueue queue;return queue;}
}

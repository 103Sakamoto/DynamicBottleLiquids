#include "PCH.h"
#include "GodrayShield.h"
#include <d3d11_1.h>
namespace nuka {
GodrayShield& Shield(){static GodrayShield s;return s;}
static void HR(HRESULT h){if(FAILED(h))throw std::runtime_error("God-ray shield D3D failure");}
namespace {
using Microsoft::WRL::ComPtr;
struct State {
 bool restoreOnly;ID3D11DeviceContext* c;ComPtr<ID3D11DeviceContext1> c1;
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11InputLayout> layout;
 ComPtr<ID3D11Buffer> vb,ib,geo,vc,pc;UINT stride,offset,io,ref,mask,gf=0,gn=0,vf=0,vn=0,pf=0,pn=0;DXGI_FORMAT format;D3D11_PRIMITIVE_TOPOLOGY topology;
 ComPtr<ID3D11BlendState> blend;float factors[4];ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11RasterizerState> raster;
 ComPtr<ID3D11ShaderResourceView> baseTexture;ComPtr<ID3D11SamplerState> baseSampler;
 ID3D11RenderTargetView* rt[8]{};ComPtr<ID3D11DepthStencilView> dsv;ID3D11ShaderResourceView* srv[2]{};
 D3D11_RECT scissors[16]{};UINT nsc=16;
 D3D11_VIEWPORT viewport[16]{};UINT nvp=16;ID3D11ClassInstance* vcs[256]{},*pcs[256]{};UINT nv=256,np=256;
 explicit State(ID3D11DeviceContext* x,bool restore=false):restoreOnly(restore),c(x){
 c->RSGetScissorRects(&nsc,scissors);
 if(!restoreOnly){c->PSGetShaderResources(0,1,&baseTexture);c->PSGetSamplers(14,1,&baseSampler);}
 if(!restoreOnly)c->QueryInterface(IID_PPV_ARGS(&c1));c->VSGetShader(&vs,vcs,&nv);c->PSGetShader(&ps,pcs,&np);c->IAGetInputLayout(&layout);if(!restoreOnly){c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetIndexBuffer(&ib,&format,&io);}c->IAGetPrimitiveTopology(&topology);
 if(!restoreOnly){c->VSGetConstantBuffers(2,1,&geo);c->VSGetConstantBuffers(13,1,&vc);c->PSGetConstantBuffers(13,1,&pc);}
 if(c1){c1->VSGetConstantBuffers1(2,1,geo.ReleaseAndGetAddressOf(),&gf,&gn);c1->VSGetConstantBuffers1(13,1,vc.ReleaseAndGetAddressOf(),&vf,&vn);c1->PSGetConstantBuffers1(13,1,pc.ReleaseAndGetAddressOf(),&pf,&pn);}
 c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&ref);c->RSGetState(&raster);c->RSGetViewports(&nvp,viewport);c->OMGetRenderTargets(8,rt,&dsv);c->PSGetShaderResources(14,2,srv);
 }
 ~State(){
 if(!restoreOnly){auto* savedBase=baseTexture.Get();auto* savedSampler=baseSampler.Get();c->PSSetShaderResources(0,1,&savedBase);c->PSSetSamplers(14,1,&savedSampler);}
 ID3D11ShaderResourceView* nulls[2]{};c->PSSetShaderResources(14,2,nulls);c->OMSetRenderTargets(8,rt,dsv.Get());c->PSSetShaderResources(14,2,srv);
 c->VSSetShader(vs.Get(),vcs,nv);c->PSSetShader(ps.Get(),pcs,np);c->IASetInputLayout(layout.Get());if(!restoreOnly){auto* b=vb.Get();c->IASetVertexBuffers(0,1,&b,&stride,&offset);c->IASetIndexBuffer(ib.Get(),format,io);}c->IASetPrimitiveTopology(topology);
 if(!restoreOnly){auto* g=geo.Get();auto* v=vc.Get();auto* p=pc.Get();c->VSSetConstantBuffers(2,1,&g);c->VSSetConstantBuffers(13,1,&v);c->PSSetConstantBuffers(13,1,&p);
 if(c1){c1->VSSetConstantBuffers1(2,1,&g,&gf,&gn);c1->VSSetConstantBuffers1(13,1,&v,&vf,&vn);c1->PSSetConstantBuffers1(13,1,&p,&pf,&pn);}}
 c->OMSetBlendState(blend.Get(),factors,mask);c->OMSetDepthStencilState(depth.Get(),ref);c->RSSetState(raster.Get());c->RSSetViewports(nvp,viewport);c->RSSetScissorRects(nsc,scissors);
 for(auto* t:rt)if(t)t->Release();for(auto* s:srv)if(s)s->Release();for(UINT i=0;i<nv;i++)vcs[i]->Release();for(UINT i=0;i<np;i++)pcs[i]->Release();
 }
};
}
void GodrayShield::Initialize(ID3D11Device* d,const std::filesystem::path& path){
 device=d;auto code=[&](const wchar_t* name){ComPtr<ID3DBlob> b;HR(D3DReadFileToBlob((path/name).c_str(),&b));return b;};
 auto v=code(L"FO4Bottle.vs.cso"),m=code(L"FO4Bottle.mask.cso"),sv=code(L"GodrayRestore.vs.cso"),sp=code(L"GodrayRestore.ps.cso");
 HR(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&meshVS));HR(d->CreateVertexShader(sv->GetBufferPointer(),sv->GetBufferSize(),nullptr,&screenVS));HR(d->CreatePixelShader(m->GetBufferPointer(),m->GetBufferSize(),nullptr,&maskPS));HR(d->CreatePixelShader(sp->GetBufferPointer(),sp->GetBufferSize(),nullptr,&restorePS));
 D3D11_INPUT_ELEMENT_DESC e[]={{"POSITION",0,DXGI_FORMAT_R16G16B16A16_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R16G16_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},{"BINORMAL",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};HR(d->CreateInputLayout(e,4,v->GetBufferPointer(),v->GetBufferSize(),&layout));
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Constants);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;HR(d->CreateBuffer(&bd,nullptr,&constants));
 D3D11_BLEND_DESC b{};auto& r=b.RenderTarget[0];r.RenderTargetWriteMask=15;r.BlendEnable=TRUE;r.SrcBlend=r.DestBlend=r.SrcBlendAlpha=r.DestBlendAlpha=D3D11_BLEND_ONE;r.BlendOp=r.BlendOpAlpha=D3D11_BLEND_OP_MAX;HR(d->CreateBlendState(&b,&maximum));
 r.SrcBlend=D3D11_BLEND_SRC_ALPHA;r.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;r.BlendOp=D3D11_BLEND_OP_ADD;r.BlendEnable=TRUE;r.RenderTargetWriteMask=7;HR(d->CreateBlendState(&b,&restoreBlend));
 D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthFunc=D3D11_COMPARISON_ALWAYS;HR(d->CreateDepthStencilState(&dd,&noDepth));D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;HR(d->CreateRasterizerState(&rd,&screenRaster));
}
void GodrayShield::BeginFrame(unsigned f){if(f!=frame){frame=f;used=0;pending=false;}}
void GodrayShield::Capture(ID3D11DeviceContext* c,const Constants& k,UINT count,UINT start,INT base){
 if(!enabled||!drawEnabled||!device||used>=256||k.optics.w>=2)return;
 ComPtr<ID3D11Buffer> native;ComPtr<ID3D11DeviceContext1> c1;c->QueryInterface(IID_PPV_ARGS(&c1));UINT first=0,range=0;
 c->VSGetConstantBuffers(2,1,&native);if(c1)c1->VSGetConstantBuffers1(2,1,native.ReleaseAndGetAddressOf(),&first,&range);if(!native)return;
 ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&rt,&depth);if(!rt||!depth)return;
 ComPtr<ID3D11RasterizerState> raster;c->RSGetState(&raster);D3D11_RASTERIZER_DESC rd{};if(raster){raster->GetDesc(&rd);if(rd.ScissorEnable)return;}
 if(used==records.size())records.emplace_back();auto& r=records[used];r.constants=k;r.count=count;r.start=start;r.base=base;r.first=first;r.range=range;if(r.sourceDepth!=depth){
 r.sourceDepth=depth;D3D11_DEPTH_STENCIL_VIEW_DESC view{};depth->GetDesc(&view);view.Flags|=D3D11_DSV_READ_ONLY_DEPTH;
 if(view.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||view.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)view.Flags|=D3D11_DSV_READ_ONLY_STENCIL;
 ComPtr<ID3D11Resource> depthResource;depth->GetResource(&depthResource);r.depth.Reset();HR(device->CreateDepthStencilView(depthResource.Get(),&view,&r.depth));
 }r.bounds=drawBounds;r.bounded=hasDrawBounds;r.raster=raster;r.target.Reset();rt->GetResource(&r.target);
 c->IAGetVertexBuffers(0,1,r.vb.ReleaseAndGetAddressOf(),&r.stride,&r.offset);c->IAGetIndexBuffer(r.ib.ReleaseAndGetAddressOf(),&r.format,&r.indexOffset);
 UINT n=1;c->RSGetViewports(&n,&r.viewport);if(!n||!r.vb||!r.ib)return;
 ComPtr<ID3D11DepthStencilState> ds;c->OMGetDepthStencilState(&ds,&r.stencil);D3D11_DEPTH_STENCIL_DESC dd{};if(ds)ds->GetDesc(&dd);else{dd.DepthEnable=TRUE;dd.DepthFunc=D3D11_COMPARISON_LESS;}
 dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.StencilWriteMask=0;
 if(dd.DepthFunc==D3D11_COMPARISON_LESS)dd.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 if(dd.DepthFunc==D3D11_COMPARISON_GREATER)dd.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
 D3D11_DEPTH_STENCIL_DESC previous{};if(r.depthState)r.depthState->GetDesc(&previous);if(!r.depthState||memcmp(&dd,&previous,sizeof(dd))){r.depthState.Reset();HR(device->CreateDepthStencilState(&dd,&r.depthState));}
 D3D11_BUFFER_DESC bd{},old{};native->GetDesc(&bd);if(r.geometry)r.geometry->GetDesc(&old);if(!r.geometry||old.ByteWidth!=bd.ByteWidth){r.geometry.Reset();bd.Usage=D3D11_USAGE_DEFAULT;bd.CPUAccessFlags=0;bd.MiscFlags=0;HR(device->CreateBuffer(&bd,nullptr,&r.geometry));}
 c->PSGetShaderResources(0,1,r.baseTexture.ReleaseAndGetAddressOf());c->PSGetSamplers(14,1,r.baseSampler.ReleaseAndGetAddressOf());
 c->CopyResource(r.geometry.Get(),native.Get());used++;
}
bool GodrayShield::Before(ID3D11DeviceContext* c,ID3D11RenderTargetView* rt,bool copyScene){
 pending=false;if(!enabled||!used||!rt)return false;
 ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);if(gs||hs||ds)return false;
 ComPtr<ID3D11Resource> resource;rt->GetResource(&resource);bool match=false;for(unsigned i=0;i<used;i++)if(records[i].target==resource)match=true;
 if(!match){static bool logged=false;if(!logged){logged=true;REX::WARN("God-ray shield: ApplyLighting target differs from captured bottle target; skipped.");}return false;}
 ComPtr<ID3D11Texture2D> source;resource.As(&source);if(!source)return false;D3D11_TEXTURE2D_DESC td{},old{};source->GetDesc(&td);if(td.SampleDesc.Count!=1||td.ArraySize!=1||td.MipLevels!=1)return false;
 if(before)before->GetDesc(&old);
 if(!before||old.Width!=td.Width||old.Height!=td.Height||old.Format!=td.Format){before.Reset();mask.Reset();beforeSRV.Reset();maskSRV.Reset();maskRTV.Reset();td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;td.MiscFlags=0;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;HR(device->CreateTexture2D(&td,nullptr,&before));HR(device->CreateShaderResourceView(before.Get(),nullptr,&beforeSRV));td.Format=DXGI_FORMAT_R8_UNORM;td.BindFlags|=D3D11_BIND_RENDER_TARGET;HR(device->CreateTexture2D(&td,nullptr,&mask));HR(device->CreateShaderResourceView(mask.Get(),nullptr,&maskSRV));HR(device->CreateRenderTargetView(mask.Get(),nullptr,&maskRTV));}
 restoreBounds={LONG(td.Width),LONG(td.Height),0,0};
 for(unsigned i=0;i<used;i++){auto& r=records[i];if(r.target!=resource)continue;
  if(!r.bounded){restoreBounds={0,0,LONG(td.Width),LONG(td.Height)};break;}
  restoreBounds.left=std::min(restoreBounds.left,r.bounds.left);restoreBounds.top=std::min(restoreBounds.top,r.bounds.top);
  restoreBounds.right=std::max(restoreBounds.right,r.bounds.right);restoreBounds.bottom=std::max(restoreBounds.bottom,r.bounds.bottom);
 }
 restoreBounds.left=std::clamp(restoreBounds.left,0L,LONG(td.Width));restoreBounds.right=std::clamp(restoreBounds.right,0L,LONG(td.Width));
 restoreBounds.top=std::clamp(restoreBounds.top,0L,LONG(td.Height));restoreBounds.bottom=std::clamp(restoreBounds.bottom,0L,LONG(td.Height));
 if(restoreBounds.left>=restoreBounds.right||restoreBounds.top>=restoreBounds.bottom)return false;
 State state(c);ID3D11ShaderResourceView* nulls[2]{};c->PSSetShaderResources(14,2,nulls);D3D11_BOX box{UINT(restoreBounds.left),UINT(restoreBounds.top),0,UINT(restoreBounds.right),UINT(restoreBounds.bottom),1};if(copyScene)c->CopySubresourceRegion(before.Get(),0,box.left,box.top,0,source.Get(),0,&box);float clear[4]{};
 if(state.c1)state.c1->ClearView(maskRTV.Get(),clear,&restoreBounds,1);else c->ClearRenderTargetView(maskRTV.Get(),clear);
 
 c->VSSetShader(meshVS.Get(),nullptr,0);c->PSSetShader(maskPS.Get(),nullptr,0);c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->OMSetBlendState(maximum.Get(),nullptr,~0u);
 auto* cb=constants.Get();c->VSSetConstantBuffers(13,1,&cb);c->PSSetConstantBuffers(13,1,&cb);
 
 for(unsigned i=0;i<used;i++){auto& r=records[i];if(r.target!=resource)continue;auto* m=maskRTV.Get();c->OMSetRenderTargets(1,&m,r.depth.Get());c->OMSetDepthStencilState(r.depthState.Get(),r.stencil);c->RSSetState(r.raster.Get());c->RSSetViewports(1,&r.viewport);auto* v=r.vb.Get();c->IASetVertexBuffers(0,1,&v,&r.stride,&r.offset);c->IASetIndexBuffer(r.ib.Get(),r.format,r.indexOffset);auto* g=r.geometry.Get();c->VSSetConstantBuffers(2,1,&g);if(state.c1)state.c1->VSSetConstantBuffers1(2,1,&g,&r.first,&r.range);c->UpdateSubresource(constants.Get(),0,nullptr,&r.constants,0,0);auto* baseTex=r.baseTexture.Get();auto* baseSamp=r.baseSampler.Get();c->PSSetShaderResources(0,1,&baseTex);c->PSSetSamplers(14,1,&baseSamp);c->DrawIndexed(r.count,r.start,r.base);}
 
 pending=true;return true;
}
void GodrayShield::After(ID3D11DeviceContext* c,ID3D11RenderTargetView* rt){
 if(!pending)return;pending=false;State state(c,true);c->OMSetRenderTargets(1,&rt,nullptr);c->OMSetDepthStencilState(noDepth.Get(),0);c->OMSetBlendState(restoreBlend.Get(),nullptr,~0u);c->RSSetState(screenRaster.Get());c->RSSetScissorRects(1,&restoreBounds);D3D11_TEXTURE2D_DESC td{};before->GetDesc(&td);D3D11_VIEWPORT vp{0,0,float(td.Width),float(td.Height),0,1};c->RSSetViewports(1,&vp);c->VSSetShader(screenVS.Get(),nullptr,0);c->PSSetShader(restorePS.Get(),nullptr,0);c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11ShaderResourceView* srv[]{beforeSRV.Get(),maskSRV.Get()};c->PSSetShaderResources(14,2,srv);c->Draw(3,0);
 static bool logged=false;if(!logged){logged=true;REX::INFO("God-ray shield: same-frame liquid mask applied after volumetric composition ({} draws).",used);}
}
}

#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <stdexcept>


namespace nuka {
// Raw D32 preserves adjacent surface depths. Clear private selection per bottle;
// never copy or write the native scene depth, or rotate encoded depth ranges.
class PrivateShellDepth {
 template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
 Ptr<ID3D11DepthStencilView> view;
 Ptr<ID3D11Texture2D> texture;
 Ptr<ID3D11ShaderResourceView> resource;
 Ptr<ID3D11Buffer> constants;
 UINT width=0,height=0;
 unsigned clears=0;
 static void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("Private shell depth creation failed");}
public:
 bool Prepare(ID3D11Device* device,ID3D11DepthStencilView* source){
  Ptr<ID3D11Resource> nativeResource;source->GetResource(&nativeResource);
  Ptr<ID3D11Texture2D> native;Check(nativeResource.As(&native));
  D3D11_TEXTURE2D_DESC desc{};native->GetDesc(&desc);
  D3D11_DEPTH_STENCIL_VIEW_DESC sourceView{};source->GetDesc(&sourceView);
  if(desc.SampleDesc.Count!=1||sourceView.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D)return false;
  UINT w=desc.Width>>sourceView.Texture2D.MipSlice,h=desc.Height>>sourceView.Texture2D.MipSlice;
  if(view&&width==w&&height==h)return true;
  desc={};desc.Width=w;desc.Height=h;desc.ArraySize=desc.MipLevels=1;
  desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;
  desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
  Ptr<ID3D11Texture2D> next;Check(device->CreateTexture2D(&desc,nullptr,&next));
  D3D11_DEPTH_STENCIL_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_D32_FLOAT;vd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
  Ptr<ID3D11DepthStencilView> nextView;Check(device->CreateDepthStencilView(next.Get(),&vd,&nextView));
  D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_FLOAT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
  Ptr<ID3D11ShaderResourceView> nextResource;Check(device->CreateShaderResourceView(next.Get(),&sd,&nextResource));
  if(!constants){D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_IMMUTABLE;float enabled[4]{1,0,0,0};D3D11_SUBRESOURCE_DATA data{enabled,0,0};Check(device->CreateBuffer(&bd,&data,&constants));}
  texture=next;view=nextView;resource=nextResource;width=w;height=h;
  return true;
 }
 void Begin(ID3D11DeviceContext* context){
  ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(19,1,&none);
  context->ClearDepthStencilView(view.Get(),D3D11_CLEAR_DEPTH,1,0);++clears;
  auto* buffer=constants.Get();context->PSSetConstantBuffers(9,1,&buffer);
 }
 void BindSelection(ID3D11DeviceContext* context){auto* srv=resource.Get();context->PSSetShaderResources(19,1,&srv);}
 ID3D11DepthStencilView* View()const{return view.Get();}
 unsigned ClearCount()const{return clears;}
};
class RestoreShellTargets {
 ID3D11DeviceContext* context;ID3D11RenderTargetView* targets[8]{};
 Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
public:
 explicit RestoreShellTargets(ID3D11DeviceContext* c):context(c){context->OMGetRenderTargets(8,targets,&depth);}
 ~RestoreShellTargets(){context->OMSetRenderTargets(8,targets,depth.Get());for(auto* target:targets)if(target)target->Release();}
 RestoreShellTargets(const RestoreShellTargets&)=delete;
 RestoreShellTargets& operator=(const RestoreShellTargets&)=delete;
};
}

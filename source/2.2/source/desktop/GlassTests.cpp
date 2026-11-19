#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D failure");}
int main(){try{
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;
 check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));
 float white[4]{1,1,1,1};D3D11_SUBRESOURCE_DATA initial[6]{};for(auto& x:initial){x.pSysMem=white;x.SysMemPitch=16;}
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=1;td.ArraySize=6;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
 ComPtr<ID3D11Texture2D>tex;check(d->CreateTexture2D(&td,initial,&tex));ComPtr<ID3D11ShaderResourceView>srv;check(d->CreateShaderResourceView(tex.Get(),nullptr,&srv));
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
 ComPtr<ID3D11SamplerState>sampler;check(d->CreateSamplerState(&sd,&sampler));
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=128;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
 ComPtr<ID3D11Buffer>output,staging;check(d->CreateBuffer(&bd,nullptr,&output));ComPtr<ID3D11UnorderedAccessView>uav;check(d->CreateUnorderedAccessView(output.Get(),nullptr,&uav));
 bd.BindFlags=0;bd.MiscFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(d->CreateBuffer(&bd,nullptr,&staging));
 for(bool preview:{false,true}){
  D3D_SHADER_MACRO macros[]{{"INVENTORY_PREVIEW", "1"},{nullptr,nullptr}};ComPtr<ID3DBlob>code,error;
  auto result=D3DCompileFromFile(L"shaders/GlassTests.hlsl",preview?macros:nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"Main","cs_5_0",D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
  if(FAILED(result)&&error)std::cerr<<static_cast<char*>(error->GetBufferPointer());check(result);
  ComPtr<ID3D11ComputeShader>shader;check(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
  auto* view=srv.Get();auto* samp=sampler.Get();auto* target=uav.Get();c->CSSetShader(shader.Get(),nullptr,0);c->CSSetShaderResources(0,1,&view);c->CSSetSamplers(5,1,&samp);c->CSSetUnorderedAccessViews(0,1,&target,nullptr);c->Dispatch(1,1,1);
  target=nullptr;c->CSSetUnorderedAccessViews(0,1,&target,nullptr);c->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE map{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
  const float expected[]{.04f,.04f,.04f,.02f,.01f,0,1,.08f};auto* values=static_cast<float*>(map.pData);bool valid=true;
  for(int i=0;i<8;++i)for(int channel=0;channel<3;++channel)valid&=std::abs(values[i*4+channel]-expected[i]*(preview?1.75f:1.f))<.00001f;
  c->Unmap(staging.Get(),0);if(!valid)throw std::runtime_error("Shared reflection response mismatch");
 }
 std::cout<<"PASS: production shared glass response: neutral BGSM/Effect equality, legacy scalar independence, mask/normal alpha, coverage, grazing, slider and exactly 1.75x preview-only cubemap response\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

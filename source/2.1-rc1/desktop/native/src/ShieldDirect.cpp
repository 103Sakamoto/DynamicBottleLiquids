#include <algorithm>
#include <mutex>
#include <MinHook.h>
#include <vector>
#include <cstdint>
#include <d3dcompiler.h>
#include "ShieldDirect.h"
#include <d3d11_1.h>

namespace nuka {
using Microsoft::WRL::ComPtr;
namespace {
std::mutex shaderMutex;
struct AuditedShader {ComPtr<ID3D11PixelShader> shader;ID3D11Device* device;};
std::vector<AuditedShader> auditedShaders;
using CreatePS=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
CreatePS originalCreatePS=nullptr;
void* hookedCreatePS=nullptr;
constexpr unsigned char signature[]={0x44,0x58,0x42,0x43,0xc2,0xae,0x0f,0x33,0xfe,0x1b,0xbf,0x4f,0xf7,0xaa,0x30,0x11,0xfa,0x35,0x16,0x73};
HRESULT STDMETHODCALLTYPE ObservePS(ID3D11Device* device,const void* bytes,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11PixelShader** shader){
 auto result=originalCreatePS(device,bytes,size,linkage,shader);
 if(SUCCEEDED(result)&&shader&&*shader&&!linkage&&size==2800&&bytes&&memcmp(bytes,signature,sizeof(signature))==0){
  try {std::lock_guard lock(shaderMutex);if(auditedShaders.size()<32)auditedShaders.push_back({*shader,device});}catch(...){/* fail closed */}
 }
 return result;
}
void ObserveDevice(ID3D11Device* device){
 if(!device)return;
 std::lock_guard lock(shaderMutex);
 auto* target=(*reinterpret_cast<void***>(device))[15];
 if(hookedCreatePS)return;
 if(MH_CreateHook(target,reinterpret_cast<void*>(&ObservePS),reinterpret_cast<void**>(&originalCreatePS))!=MH_OK)return;
 if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalCreatePS=nullptr;return;}
 hookedCreatePS=target;
}
using CreateDevice=decltype(&D3D11CreateDevice);
using CreateSwapChain=decltype(&D3D11CreateDeviceAndSwapChain);
CreateDevice originalDevice=nullptr;
CreateSwapChain originalSwapChain=nullptr;
HRESULT WINAPI Device(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT n,UINT sdk,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
 auto hr=originalDevice(adapter,driver,software,flags,levels,n,sdk,device,level,context);
 if(SUCCEEDED(hr)&&device)ObserveDevice(*device);return hr;
}
HRESULT WINAPI SwapChain(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT n,UINT sdk,const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** swap,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
 auto hr=originalSwapChain(adapter,driver,software,flags,levels,n,sdk,desc,swap,device,level,context);
 if(SUCCEEDED(hr)&&device)ObserveDevice(*device);return hr;
}
}
bool InstallShieldShaderObserver(){
 auto module=GetModuleHandleW(L"d3d11.dll");if(!module)module=LoadLibraryW(L"d3d11.dll");if(!module)return false;
 auto install=[&](const char* name,void* callback,void** original){auto* target=GetProcAddress(module,name);if(!target||MH_CreateHook(reinterpret_cast<void*>(target),callback,original)!=MH_OK)return false;
  if(MH_EnableHook(reinterpret_cast<void*>(target))==MH_OK)return true;MH_RemoveHook(reinterpret_cast<void*>(target));*original=nullptr;return false;};
 bool device=install("D3D11CreateDevice",reinterpret_cast<void*>(&Device),reinterpret_cast<void**>(&originalDevice));
 bool swap=install("D3D11CreateDeviceAndSwapChain",reinterpret_cast<void*>(&SwapChain),reinterpret_cast<void**>(&originalSwapChain));
 return device||swap;
}
bool ShieldDirect::Initialize(ID3D11Device* device,const std::filesystem::path& path) {
 owner=device;
 ComPtr<ID3DBlob> code;
 if(FAILED(D3DReadFileToBlob((path/L"ShieldDirect.ps.cso").c_str(),&code))||
    FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&masked)))return false;
 D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 return SUCCEEDED(device->CreateBuffer(&desc,nullptr,&bounds));
}
bool ShieldDirect::Observed() const {
 std::lock_guard lock(shaderMutex);for(auto& candidate:auditedShaders)if(candidate.device==owner.Get())return true;
 return false;
}
bool ShieldDirect::Matches(ID3D11DeviceContext* ctx) const {
 ComPtr<ID3D11PixelShader> ps;UINT classes=0;ctx->PSGetShader(&ps,nullptr,&classes);
 bool recognized=false;{std::lock_guard lock(shaderMutex);for(auto& candidate:auditedShaders)if(ps==candidate.shader){recognized=true;break;}}
 if(!recognized||!masked||!bounds||classes)return false;
 ComPtr<ID3D11BlendState> blend;FLOAT factors[4];UINT sampleMask;
 ctx->OMGetBlendState(&blend,factors,&sampleMask);if(!blend)return false;
 D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);auto& b=desc.RenderTarget[0];
 return !desc.AlphaToCoverageEnable&&b.BlendEnable&&b.BlendOp==D3D11_BLEND_OP_ADD&&
        b.DestBlend==D3D11_BLEND_ONE&&(b.SrcBlend==D3D11_BLEND_BLEND_FACTOR||b.SrcBlend==D3D11_BLEND_ONE)&&
        (b.RenderTargetWriteMask==7||b.RenderTargetWriteMask==15);
}
void ShieldDirect::Draw(ID3D11DeviceContext* ctx,ID3D11ShaderResourceView* mask,const D3D11_RECT& region,
                       void (STDMETHODCALLTYPE* draw)(ID3D11DeviceContext*,UINT,UINT)) {
 ComPtr<ID3D11PixelShader> savedShader;ctx->PSGetShader(&savedShader,nullptr,nullptr);
 ComPtr<ID3D11ShaderResourceView> savedMask;ComPtr<ID3D11Buffer> savedBounds;
 ctx->PSGetShaderResources(15,1,&savedMask);ctx->PSGetConstantBuffers(13,1,&savedBounds);
 // Preserve constant-buffer subranges too (the SDK normally does not use b13).
 ComPtr<ID3D11DeviceContext1> ctx1;ctx->QueryInterface(IID_PPV_ARGS(&ctx1));UINT first=0,count=0;
 if(ctx1)ctx1->PSGetConstantBuffers1(13,1,savedBounds.ReleaseAndGetAddressOf(),&first,&count);
 ctx->UpdateSubresource(bounds.Get(),0,nullptr,&region,0,0);auto* cb=bounds.Get();
 ctx->PSSetConstantBuffers(13,1,&cb);ctx->PSSetShaderResources(15,1,&mask);ctx->PSSetShader(masked.Get(),nullptr,0);
 draw(ctx,3,0);
 ctx->PSSetShader(savedShader.Get(),nullptr,0);auto* oldMask=savedMask.Get();auto* oldBounds=savedBounds.Get();
 ctx->PSSetShaderResources(15,1,&oldMask);ctx->PSSetConstantBuffers(13,1,&oldBounds);
 if(ctx1)ctx1->PSSetConstantBuffers1(13,1,&oldBounds,&first,&count);
}
}

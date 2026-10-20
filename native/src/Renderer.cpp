#include "PCH.h"
#include "Renderer.h"
#include "NativeOptics.h"
#include "SurfaceNoise.h"
#include <d3d11_1.h>
namespace nuka {
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error(std::format("D3D failure: {:08X}",unsigned(hr)));}
void Renderer::Initialize(ID3D11Device* device,const std::filesystem::path& path,UINT indices){
    Native().Initialize(device,path);
    expectedIndices=indices;
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    Check(device->CreateSamplerState(&sd,&noiseSampler));
    auto noise=MakeSurfaceNoise();D3D11_SUBRESOURCE_DATA noiseData[8]{};
    for(unsigned i=0;i<8;i++){noiseData[i].pSysMem=noise[i].data();noiseData[i].SysMemPitch=(128u>>i)*2;}
    D3D11_TEXTURE2D_DESC nt{};nt.Width=nt.Height=128;nt.MipLevels=8;nt.ArraySize=1;
    nt.Format=DXGI_FORMAT_R8G8_SNORM;nt.SampleDesc.Count=1;nt.Usage=D3D11_USAGE_IMMUTABLE;nt.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> noiseTexture;Check(device->CreateTexture2D(&nt,noiseData,&noiseTexture));
    Check(device->CreateShaderResourceView(noiseTexture.Get(),nullptr,&surfaceNoise));
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;sd.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;
    Check(device->CreateSamplerState(&sd,&shadowSampler));

}
bool Renderer::Draw(ID3D11DeviceContext* ctx,const Constants& c,UINT count,UINT start,INT base,void (STDMETHODCALLTYPE* original)(ID3D11DeviceContext*,UINT,UINT,INT)){
    if(!surfaceNoise||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||count!=expectedIndices)return false;
    ComPtr<ID3D11Buffer> vb,native,nativeLighting;UINT stride{},offset{};ctx->IAGetVertexBuffers(0,1,&vb,&stride,&offset);ctx->VSGetConstantBuffers(2,1,&native);
    if(!vb||stride!=20||!native)return false;D3D11_BUFFER_DESC desc{};native->GetDesc(&desc);if(desc.ByteWidth<112)return false;
    ctx->PSGetConstantBuffers(2,1,&nativeLighting);
    if(!nativeLighting)return false;
    D3D11_BUFFER_DESC lightingDesc{};nativeLighting->GetDesc(&lightingDesc);if(lightingDesc.ByteWidth<240)return false;
    D3D11_PRIMITIVE_TOPOLOGY topology;ctx->IAGetPrimitiveTopology(&topology);if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return false;
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    ctx->GSGetShader(&gs,nullptr,nullptr);ctx->HSGetShader(&hs,nullptr,nullptr);ctx->DSGetShader(&ds,nullptr,nullptr);if(gs||hs||ds)return false;
    ID3D11RenderTargetView* targets[8]{};ctx->OMGetRenderTargets(8,targets,nullptr);
    bool singleTarget=targets[0]!=nullptr;for(int i=1;i<8;i++)if(targets[i])singleTarget=false;
    for(auto* t:targets)if(t)t->Release();if(!singleTarget)return false;
    Constants drawConstants=c;drawConstants.trace.w=0;
    drawConstants.materialFlags={0,0,0,c.materialFlags.w};
    for(unsigned i=0;i<3;i++){
        ComPtr<ID3D11ShaderResourceView> view;ctx->PSGetShaderResources(i==2?7:i,1,&view);
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};if(view)view->GetDesc(&vd);
        (&drawConstants.materialFlags.x)[i]=vd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D?1.f:0.f;
    }
    if(sunSource&&c.sunInfo.x>0){
        ComPtr<ID3D11Resource> resource;sunSource->GetResource(&resource);
        if(resource!=sunResource){
            sunArray.Reset();sunResource=resource;D3D11_SHADER_RESOURCE_VIEW_DESC vd{};sunSource->GetDesc(&vd);
            ComPtr<ID3D11Texture2D> tex;if(SUCCEEDED(resource.As(&tex))){
                D3D11_TEXTURE2D_DESC td{};tex->GetDesc(&td);
                vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;vd.Texture2DArray={0,1,0,td.ArraySize};
                ComPtr<ID3D11Device> d;ctx->GetDevice(&d);d->CreateShaderResourceView(resource.Get(),&vd,&sunArray);
            }
        }
    }else {sunArray.Reset();sunResource.Reset();}
    if(!sunArray)drawConstants.sunInfo.x=0;
    // Effect materials bind their environment cube at t5. Validate the view;
    // never interpret a stale 2D binding as a cube. Keep engine slot t5 untouched.
    ComPtr<ID3D11ShaderResourceView> environment;
    ctx->PSGetShaderResources(5,1,&environment);
    D3D11_SHADER_RESOURCE_VIEW_DESC environmentDesc{};
    if(environment)environment->GetDesc(&environmentDesc);
    if(environmentDesc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURECUBE)environment.Reset();
    drawConstants.eye.w=environment?1.f:0.f;
    if(drawConstants.optics.w>=2){
        // Preserve the engine shadow sampler around the direct draw.
        ComPtr<ID3D11SamplerState> old;ctx->PSGetSamplers(10,1,&old);auto* sampler=shadowSampler.Get();ctx->PSSetSamplers(10,1,&sampler);
        bool result=false;try{result=Native().DrawDistant(ctx,drawConstants,environment.Get(),sunArray.Get(),count,start,base,original);}catch(...){sampler=old.Get();ctx->PSSetSamplers(10,1,&sampler);throw;}
        sampler=old.Get();ctx->PSSetSamplers(10,1,&sampler);return result;
    }
    bool accepted=false;
    {accepted=Native().Capture(ctx,drawConstants,environment.Get(),sunArray.Get(),surfaceNoise.Get(),shadowSampler.Get(),noiseSampler.Get());}
    if(accepted)return true;
    return false;
}
}

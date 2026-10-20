#pragma once
#include "Simulation.h"
#include "ConstantBufferRing.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
namespace nuka {
using Microsoft::WRL::ComPtr;
class Renderer {
public:
    void SetSunShadow(ID3D11ShaderResourceView* view){sunSource=view;}
    void SetExpectedIndices(unsigned count){expectedIndices=count;}
    void Initialize(ID3D11Device* device,const std::filesystem::path& path,UINT indices=876);
    bool Draw(ID3D11DeviceContext* context,const Constants& constants,UINT count,UINT start,INT base,
              void (STDMETHODCALLTYPE* original)(ID3D11DeviceContext*,UINT,UINT,INT));
private:
    ComPtr<ID3D11ShaderResourceView> surfaceNoise,sunSource,sunArray;
    ComPtr<ID3D11Resource> sunResource;
    ComPtr<ID3D11SamplerState> shadowSampler,noiseSampler;
    UINT expectedIndices=876;
};
}

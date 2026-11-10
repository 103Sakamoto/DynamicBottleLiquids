#pragma once
#include <array>
#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>
namespace nuka {
// WRITE_DISCARD is required even after rotation: wrapping does not prove
// completion of earlier GPU work. No NO_OVERWRITE assumptions or waits.
class ConstantBufferRing {
public:
    HRESULT Initialize(ID3D11Device* device,UINT bytes){
        cursor=0;size=bytes;
        for(auto& buffer:buffers)buffer.Reset();
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;
        desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        for(auto& buffer:buffers){auto hr=device->CreateBuffer(&desc,nullptr,&buffer);if(FAILED(hr))return hr;}
        return S_OK;
    }
    HRESULT Upload(ID3D11DeviceContext* context,const void* data,ID3D11Buffer*& result){
        result=nullptr;auto* buffer=buffers[cursor].Get();
        if(!buffer)return E_FAIL;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        auto hr=context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
        if(FAILED(hr))return hr;
        std::memcpy(mapped.pData,data,size);context->Unmap(buffer,0);
        result=buffer;cursor=(cursor+1)%buffers.size();return S_OK;
    }
private:
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,32> buffers;
    size_t cursor=0;UINT size=0;
};
}

#pragma once
#include "SceneColour.h"
#include <d3d11.h>
#include <wrl/client.h>
namespace nuka {
class ForwardScene {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Resource> identity;
    Ptr<ID3D11Texture2D> copy;
    Ptr<ID3D11ShaderResourceView> view;
    unsigned frame=~0u;
    D3D11_VIEWPORT viewport{};
public:
    ID3D11ShaderResourceView* Snapshot(ID3D11DeviceContext* c,ID3D11RenderTargetView* rt,unsigned f){
        if(!rt||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return nullptr;
        Ptr<ID3D11Resource> source;rt->GetResource(&source);
        Ptr<ID3D11Texture2D> tex;if(FAILED(source.As(&tex)))return nullptr;
        D3D11_TEXTURE2D_DESC td{};tex->GetDesc(&td);
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rt->GetDesc(&rd);
        if(!SupportedSceneColour(td.Format)||td.ArraySize!=1||td.MipLevels!=1||td.SampleDesc.Count!=1||rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D)return nullptr;
        UINT n=1;D3D11_VIEWPORT vp{};c->RSGetViewports(&n,&vp);if(n!=1)return nullptr;
        if(source!=identity){
            Ptr<ID3D11Device> d;c->GetDevice(&d);
            td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.CPUAccessFlags=0;td.MiscFlags=0;
            Ptr<ID3D11Texture2D> next;Ptr<ID3D11ShaderResourceView> nextView;
            if(FAILED(d->CreateTexture2D(&td,nullptr,&next))||FAILED(d->CreateShaderResourceView(next.Get(),nullptr,&nextView)))return nullptr;
            copy=next;view=nextView;identity=source;frame=~0u;
        }
        if(frame!=f||memcmp(&viewport,&vp,sizeof(vp))!=0){
            c->CopyResource(copy.Get(),source.Get());frame=f;viewport=vp;
        }
        return view.Get();
    }
};
}

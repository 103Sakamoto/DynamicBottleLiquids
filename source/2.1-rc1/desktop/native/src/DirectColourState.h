#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <chrono>

namespace nuka {
// DrawDistant changes only these bindings. Render targets, raster state and
// topology stay bound. Renderer owns the surrounding shadow/noise bindings.
class DirectColourState {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    ID3D11DeviceContext* c;
    ID3D11DeviceContext1* c1;
    Ptr<ID3D11VertexShader> vs;
    Ptr<ID3D11PixelShader> ps;
    Ptr<ID3D11InputLayout> layout;
    Ptr<ID3D11BlendState> blend;
    Ptr<ID3D11DepthStencilState> depth;
    Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11ShaderResourceView> srv[3];
    Ptr<ID3D11Buffer> vb[2],pb[2];
    UINT vf[2]{},vn[2]{},pf[2]{},pn[2]{};
    float factors[4]{};
    UINT mask{},ref{};
    static constexpr UINT resources[]{14,18,20},vertexBuffers[]{10,13},pixelBuffers[]{12,13};
public:
    double* restoreTime=nullptr;
    DirectColourState(ID3D11DeviceContext* context,ID3D11DeviceContext1* context1):c(context),c1(context1){
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->IAGetInputLayout(&layout);
        c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&ref);
        c->PSGetSamplers(14,1,&sampler);
        for(unsigned i=0;i<3;++i)c->PSGetShaderResources(resources[i],1,&srv[i]);
        for(unsigned i=0;i<2;++i){
            if(c1){c1->VSGetConstantBuffers1(vertexBuffers[i],1,&vb[i],&vf[i],&vn[i]);c1->PSGetConstantBuffers1(pixelBuffers[i],1,&pb[i],&pf[i],&pn[i]);}
            else{c->VSGetConstantBuffers(vertexBuffers[i],1,&vb[i]);c->PSGetConstantBuffers(pixelBuffers[i],1,&pb[i]);}
        }
    }
    DirectColourState(const DirectColourState&)=delete;
    DirectColourState& operator=(const DirectColourState&)=delete;
    ~DirectColourState(){
        auto start=std::chrono::steady_clock::now();
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->IASetInputLayout(layout.Get());
        c->OMSetBlendState(blend.Get(),factors,mask);c->OMSetDepthStencilState(depth.Get(),ref);
        auto* s=sampler.Get();c->PSSetSamplers(14,1,&s);
        for(unsigned i=0;i<3;++i){auto* view=srv[i].Get();c->PSSetShaderResources(resources[i],1,&view);}
        for(unsigned i=0;i<2;++i){
            auto* v=vb[i].Get();auto* p=pb[i].Get();
            if(c1){c1->VSSetConstantBuffers1(vertexBuffers[i],1,&v,&vf[i],&vn[i]);c1->PSSetConstantBuffers1(pixelBuffers[i],1,&p,&pf[i],&pn[i]);}
            else{c->VSSetConstantBuffers(vertexBuffers[i],1,&v);c->PSSetConstantBuffers(pixelBuffers[i],1,&p);}
        }
        if(restoreTime)*restoreTime=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
};
}

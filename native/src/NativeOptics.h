#pragma once
#include "Simulation.h"
#include "RegionComposer.h"
#include "ConstantBufferRing.h"
#include <RE/B/BSRenderPass.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <filesystem>
#include <unordered_map>
namespace nuka {
class NativeOptics {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Record {
        Constants constants{};unsigned frame=~0u;Ptr<ID3D11Buffer> geometry,lighting;
        UINT gf=0,gn=0,lf=0,ln=0;Ptr<ID3D11Resource> colour,depthResource;
        Ptr<ID3D11ShaderResourceView> depth,base,normal,mask,cube,sun,noise;
        Ptr<ID3D11SamplerState> shadow,noiseSampler,cubeSampler;
        D3D11_VIEWPORT viewport{};
    };
    struct Target {Ptr<ID3D11Texture2D> texture;Ptr<ID3D11ShaderResourceView> srv;Ptr<ID3D11RenderTargetView> rtv;};
    Ptr<ID3D11DeviceContext> context;Ptr<ID3D11DeviceContext1> context1;Ptr<ID3D11Device> device;Ptr<ID3D11VertexShader> producerVS,composeVS;Ptr<ID3D11PixelShader> producerPS,composePS,scaledComposePS,directPS;
    Ptr<ID3D11InputLayout> layout;Ptr<ID3D11BlendState> opaque;Ptr<ID3D11DepthStencilState> nearest,noDepth;
    Ptr<ID3D11RasterizerState> screen;Ptr<ID3D11SamplerState> linear;ConstantBufferRing constants;Ptr<ID3D11Buffer> viewportConstants;
    Target light,transmission,distortion,composed;Ptr<ID3D11Texture2D> nearestTexture;Ptr<ID3D11DepthStencilView> nearestDSV;Ptr<ID3D11ShaderResourceView> nearestSRV;
    std::unordered_map<const void*,Record> records;
    D3D11_VIEWPORT sceneViewport{};Ptr<ID3D11Buffer> activeViewportBuffer;float activeWidth=0,activeHeight=0;
    Ptr<ID3D11Resource> backgroundIdentity;Ptr<ID3D11ShaderResourceView> backgroundView;
    Ptr<ID3D11Resource> sourceIdentity;Ptr<ID3D11ShaderResourceView> mainDepth;
    DXGI_FORMAT sceneFormat=DXGI_FORMAT_UNKNOWN;
    UINT width=0,height=0;unsigned frame=~0u,preparedFrame=~0u,drawn=0,composedFrame=~0u,captured=0,missed=0;
    Ptr<ID3D11BlendState> directBlend;Ptr<ID3D11DepthStencilState> directDepth;
    RegionComposer regional;
    bool failed=false;
    // Render-thread health counters only; no GPU queries or per-draw clocks.
    std::uint64_t healthCaptures=0,healthDraws=0,healthDistant=0,healthComposes=0,healthFull=0,healthRejected=0;
    unsigned healthFrames=0;
    std::uint32_t reportedRejections=0;
    bool Reject(unsigned reason,const char* description);
    void Prepare(ID3D11DeviceContext*,UINT,UINT,DXGI_FORMAT);
public:
    ID3D11Buffer* RegionArguments()const{return regional.Arguments();}
    static bool busy;
    const void* activeGeometry=nullptr;
    void InstallRegistration();
    bool IsOwnedPass(const RE::BSRenderPass*)const;
    void Initialize(ID3D11Device*,const std::filesystem::path&);
    void BeginFrame(unsigned);
    bool Capture(ID3D11DeviceContext*,const Constants&,ID3D11ShaderResourceView* cube,ID3D11ShaderResourceView* sun,ID3D11ShaderResourceView* noise,ID3D11SamplerState* shadow,ID3D11SamplerState* noiseSampler);
    bool DrawDistant(ID3D11DeviceContext*,const Constants&,ID3D11ShaderResourceView*,ID3D11ShaderResourceView*,UINT,UINT,INT,void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT));
    void DrawRegistered(ID3D11DeviceContext*,const void*,UINT,UINT,INT,void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT));
    bool ComposeBeforeHDR();
    void Fail(const char*);
    ID3D11ShaderResourceView* Composed()const{return composed.srv.Get();}
    ID3D11ShaderResourceView* Light()const{return light.srv.Get();}
    ID3D11ShaderResourceView* Transmission()const{return transmission.srv.Get();}
    ID3D11ShaderResourceView* Distortion()const{return distortion.srv.Get();}
};
NativeOptics& Native();
}

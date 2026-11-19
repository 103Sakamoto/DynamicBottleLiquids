#pragma once
#include "Simulation.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
namespace nuka {
class GodrayShield {
public:
 void Initialize(ID3D11Device*,const std::filesystem::path&);
 void BeginFrame(unsigned);
 void SetScreenBounds(const D3D11_RECT* bounds){hasDrawBounds=bounds!=nullptr;if(bounds)drawBounds=*bounds;}
 void SetDrawEnabled(bool value){drawEnabled=value;}
 void Capture(ID3D11DeviceContext*,const Constants&,UINT,UINT,INT);
 bool Before(ID3D11DeviceContext*,ID3D11RenderTargetView*,bool copyScene=true);
 void After(ID3D11DeviceContext*,ID3D11RenderTargetView*);
 void Cancel(){pending=false;}
 ID3D11ShaderResourceView* MaskView()const{return maskSRV.Get();}
 const D3D11_RECT& Bounds()const{return restoreBounds;}
 bool enabled=false;
 unsigned Captured()const{return used;}
private:
 template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
 struct Record {D3D11_RECT bounds{};bool bounded=false;Ptr<ID3D11ShaderResourceView> baseTexture;Ptr<ID3D11SamplerState> baseSampler;Ptr<ID3D11Buffer> vb,ib,geometry;Ptr<ID3D11Resource> target;Ptr<ID3D11DepthStencilView> depth,sourceDepth;Ptr<ID3D11DepthStencilState> depthState;Ptr<ID3D11RasterizerState> raster;D3D11_VIEWPORT viewport{};Constants constants;UINT stride=0,offset=0,indexOffset=0,count=0,start=0,stencil=0,first=0,range=0;INT base=0;DXGI_FORMAT format{};};
 D3D11_RECT drawBounds{},restoreBounds{};bool hasDrawBounds=false,drawEnabled=true;
 std::vector<Record> records;unsigned frame=~0u,used=0;bool pending=false;
 Ptr<ID3D11Device> device;Ptr<ID3D11VertexShader> meshVS,screenVS;Ptr<ID3D11PixelShader> maskPS,restorePS;
 Ptr<ID3D11InputLayout> layout;Ptr<ID3D11Buffer> constants;
 Ptr<ID3D11BlendState> maximum,restoreBlend;Ptr<ID3D11DepthStencilState> noDepth;Ptr<ID3D11RasterizerState> screenRaster;
 Ptr<ID3D11Texture2D> before,mask;Ptr<ID3D11ShaderResourceView> beforeSRV,maskSRV;Ptr<ID3D11RenderTargetView> maskRTV;
};
GodrayShield& Shield();
}

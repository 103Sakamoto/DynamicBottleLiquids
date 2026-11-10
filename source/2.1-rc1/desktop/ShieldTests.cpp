#include "native/src/PCH.h"
#include "native/src/GodrayShield.h"
#include "native/src/AdaptiveQuality.h"
#include <iostream>
using Microsoft::WRL::ComPtr;
void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D failure");}
void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;
 ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));
 nuka::GodrayShield shield;shield.Initialize(d.Get(),L"../accepted-runtime/F4SE/Plugins/DynamicBottleLiquids/shaders");shield.enabled=true;
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=32;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D>colour,z;ComPtr<ID3D11RenderTargetView>rt;ComPtr<ID3D11DepthStencilView>ds;
 ck(d->CreateTexture2D(&td,nullptr,&colour));ck(d->CreateRenderTargetView(colour.Get(),nullptr,&rt));
 td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;ck(d->CreateTexture2D(&td,nullptr,&z));ck(d->CreateDepthStencilView(z.Get(),nullptr,&ds));
 auto* output=rt.Get();c->OMSetRenderTargets(1,&output,ds.Get());c->ClearDepthStencilView(ds.Get(),D3D11_CLEAR_DEPTH,1,0);
 D3D11_VIEWPORT vp{0,0,32,32,0,1};c->RSSetViewports(1,&vp);
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=true;ComPtr<ID3D11RasterizerState>raster;ck(d->CreateRasterizerState(&rd,&raster));c->RSSetState(raster.Get());
 struct Vertex{unsigned short p[4],uv[2];unsigned normal,binormal;};static_assert(sizeof(Vertex)==20);
 Vertex vertices[]={{{0xbc00,0xbc00,0x3800,0x3c00},{0,0},0xffff8080,0xff8080ff},{{0xbc00,0x4200,0x3800,0x3c00},{0,0},0xffff8080,0xff8080ff},{{0x4200,0xbc00,0x3800,0x3c00},{0,0},0xffff8080,0xff8080ff}};
 unsigned short indices[]={0,1,2};float geometry[28]{};geometry[0]=geometry[5]=geometry[10]=geometry[15]=geometry[16]=geometry[21]=geometry[26]=1;
 auto buffer=[&](void* data,UINT size,UINT bind){D3D11_BUFFER_DESC bd{};bd.ByteWidth=size;bd.BindFlags=bind;D3D11_SUBRESOURCE_DATA init{data};ComPtr<ID3D11Buffer>b;ck(d->CreateBuffer(&bd,&init,&b));return b;};
 auto vb=buffer(vertices,sizeof(vertices),D3D11_BIND_VERTEX_BUFFER),ib=buffer(indices,sizeof(indices),D3D11_BIND_INDEX_BUFFER),cb=buffer(geometry,sizeof(geometry),D3D11_BIND_CONSTANT_BUFFER);
 UINT stride=20,offset=0;auto* v=vb.Get();auto* g=cb.Get();c->IASetVertexBuffers(0,1,&v,&stride,&offset);c->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R16_UINT,0);c->VSSetConstantBuffers(2,1,&g);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 nuka::Constants k{};k.debug=10;k.optics.w=0;
 shield.BeginFrame(1);shield.SetDrawEnabled(false);shield.Capture(c.Get(),k,3,0,0);need(shield.Captured()==0,"inactive ground bottle captured");
 shield.SetDrawEnabled(true);shield.Capture(c.Get(),k,3,0,0);need(shield.Captured()==1,"active capture missing");
 need(shield.Before(c.Get(),rt.Get(),false),"early mask failed");
 ComPtr<ID3D11RenderTargetView>restored;ComPtr<ID3D11DepthStencilView>restoredZ;c->OMGetRenderTargets(1,&restored,&restoredZ);need(restored==rt&&restoredZ==ds,"output/depth state not restored");
 ComPtr<ID3D11Resource>mask;shield.MaskView()->GetResource(&mask);ComPtr<ID3D11Texture2D>maskTexture;ck(mask.As(&maskTexture));maskTexture->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D>read;ck(d->CreateTexture2D(&td,nullptr,&read));c->CopyResource(read.Get(),mask.Get());D3D11_MAPPED_SUBRESOURCE map{};ck(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map));need(static_cast<unsigned char*>(map.pData)[16*map.RowPitch+16]==255,"production vertex layout/mask coverage failed");c->Unmap(read.Get(),0);
 shield.Cancel();shield.BeginFrame(2);need(shield.Captured()==0,"stale capture retained");
 nuka::ShieldMotion motion; nuka::Vec axes[]={{1,0,0},{0,1,0},{0,0,1}};
 need(!motion.Update({},axes,1,false),"settled activation changed");need(motion.Update({},axes,2,true),"held activation missing");need(motion.Update({},axes,2.5,false),"release grace changed");need(!motion.Update({},axes,3,false),"settled shield stayed active");
 std::cout<<"PASS production shield initialization, current vertex layout, early mask coverage, output/depth restoration, inactive capture exclusion, frame reset and Test3 activation rules\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

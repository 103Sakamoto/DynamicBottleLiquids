#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
namespace nuka {
class RegionComposer {
 template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
 Ptr<ID3D11ComputeShader> reduce,finish,compose,scaledCompose,copy;
 Ptr<ID3D11Buffer> bounds,args;Ptr<ID3D11ShaderResourceView> boundsView,argsView,outputView;
 Ptr<ID3D11UnorderedAccessView> boundsUav,argsUav,sceneUav,outputUav;
 Ptr<ID3D11Texture2D> sceneIdentity,outputIdentity;
 bool ready=false;
public:
 ID3D11Buffer* Arguments()const{return args.Get();}
 void Initialize(ID3D11Device*,const std::filesystem::path&);
 bool Prepare(ID3D11Device*,ID3D11DeviceContext*,ID3D11Texture2D*,ID3D11Texture2D*);
 void Draw(ID3D11DeviceContext*,ID3D11ShaderResourceView* const*,ID3D11SamplerState*,ID3D11Buffer*,bool,unsigned,unsigned);
};
}

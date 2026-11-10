#pragma once
#include <d3d11.h>
#include <filesystem>
#include <wrl/client.h>

namespace nuka {
bool InstallShieldShaderObserver();
// Matches only the audited SDK shader. Failure leaves the original shield intact.
class ShieldDirect {
public:
 bool Initialize(ID3D11Device*, const std::filesystem::path&);
 bool Observed() const;
 bool Matches(ID3D11DeviceContext*) const;
 void Draw(ID3D11DeviceContext*, ID3D11ShaderResourceView*, const D3D11_RECT&,
           void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT));
private:
 Microsoft::WRL::ComPtr<ID3D11PixelShader> masked;
 Microsoft::WRL::ComPtr<ID3D11Device> owner;
 Microsoft::WRL::ComPtr<ID3D11Buffer> bounds;
};
}

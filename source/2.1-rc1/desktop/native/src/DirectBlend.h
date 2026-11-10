#pragma once
#include <d3d11.h>
namespace nuka {
inline D3D11_BLEND_DESC DirectBlend(bool preview){
    D3D11_BLEND_DESC d{};auto& b=d.RenderTarget[0];b.BlendEnable=TRUE;
    b.SrcBlend=D3D11_BLEND_ONE;b.DestBlend=D3D11_BLEND_SRC1_COLOR;b.BlendOp=D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha=preview?D3D11_BLEND_ONE:D3D11_BLEND_ZERO;
    b.DestBlendAlpha=preview?D3D11_BLEND_INV_SRC_ALPHA:D3D11_BLEND_ONE;
    b.BlendOpAlpha=D3D11_BLEND_OP_ADD;b.RenderTargetWriteMask=15;return d;
}
}

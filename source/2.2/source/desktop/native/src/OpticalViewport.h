#pragma once
#include <d3d11.h>
#include <cmath>
namespace nuka {
// Native opaque/depth and Utility passes use pixel-rounded extents. Effect
// colour passes can retain fractional upscaler extents. Keep depth rasterization
// aligned, but never substitute an unrelated viewport or alter the depth range.
inline D3D11_VIEWPORT OpticalDepthViewport(const D3D11_VIEWPORT& captured,
                                         const D3D11_VIEWPORT* native, UINT count) {
    if(count!=1 || !native)return captured;
    const auto& v=*native;
    if(v.TopLeftX!=captured.TopLeftX || v.TopLeftY!=captured.TopLeftY ||
       v.MinDepth!=captured.MinDepth || v.MaxDepth!=captured.MaxDepth ||
       !std::isfinite(v.Width) || !std::isfinite(v.Height) ||
       v.Width<=0 || v.Height<=0 ||
       v.Width!=std::floor(captured.Width) ||
       v.Height!=std::floor(captured.Height))return captured;
    return v;
}
}

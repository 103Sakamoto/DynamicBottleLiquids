#pragma once
#include <dxgiformat.h>
namespace nuka {
inline bool SupportedSceneColour(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R11G11B10_FLOAT;
}
}

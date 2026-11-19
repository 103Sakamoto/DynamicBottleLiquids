#pragma once
#include "Simulation.h"
#include <RE/N/NiMatrix3.h>
namespace nuka {
// FO4 runtime stores each local basis vector in one SIMD entry (columns).
// Verified in 1.11.240 NiAVObject::UpdateWorldData -> RVA 0x344820:
// world translation = parent.entry[0]*x + entry[1]*y + entry[2]*z.
// Inverting an orthonormal basis therefore means dotting with each entry.
inline Vec WorldToMeshDirection(const RE::NiMatrix3& basis, Vec v) {
    return {basis[0].x*v.x+basis[0].y*v.y+basis[0].z*v.z,
            basis[1].x*v.x+basis[1].y*v.y+basis[1].z*v.z,
            basis[2].x*v.x+basis[2].y*v.y+basis[2].z*v.z};
}
}

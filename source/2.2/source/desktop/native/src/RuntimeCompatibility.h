#pragma once

#include <REL/Version.h>
#include <array>
#include <cstdint>

namespace nuka {
// Offline-audited executable RVAs. Address Library still resolves the hooks;
// these values only reject an unexpected mapping before any hook is installed.
struct RuntimeHooks {
    REL::Version version;
    std::array<std::uintptr_t, 6> rvas;
};
inline constexpr std::array<RuntimeHooks, 3> auditedRuntimes{{
    {{1,10,163,0}, {0x287ede0,0x27cae10,0xae8110,0x27bbb70,0x3098588,0x309e048}},
    {{1,10,984,0}, {0x20c1ee0,0x201a600,0x9e9140,0x200ba00,0x26fe958,0x27038b8}},
    {{1,11,240,0}, {0x221f080,0x21777a0,0xa3ce30,0x2168ba0,0x29158a8,0x291a808}},
}};
constexpr const RuntimeHooks* AuditedRuntime(REL::Version version) {
    for (const auto& runtime : auditedRuntimes)
        if (runtime.version == version) return &runtime;
    return nullptr;
}
inline void VerifyRuntimeHooks(REL::Version version) {
    const auto* runtime = AuditedRuntime(version);
    if (!runtime) throw std::runtime_error("Unaudited desktop runtime");
    // AE retains the accepted initialization path; the new check gates OG/NG.
    if (version == REL::Version{1,11,240,0}) return;
    const std::array<std::uintptr_t,6> addresses{
        RE::ID::BSBatchRenderer::Draw.address(),
        RE::ID::BSEffectShaderProperty::GetRenderPasses.address(),
        RE::ID::Interface3D::RenderPrepassesAndMenus.address(),
        RE::ID::BSShaderProperty::RenderPassArray::Add.address(),
        RE::VTABLE::ImageSpaceEffectHDR[0].address(),
        RE::VTABLE::ImageSpaceEffectHDRCS[0].address(),
    };
    constexpr std::array<const char*,6> names{"BatchDraw","EffectPasses","InterfaceRender","PassAdd","HDR table","HDRCS table"};
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    for (std::size_t i=0;i<addresses.size();++i) {
        if (addresses[i] != base + runtime->rvas[i])
            throw std::runtime_error(std::format("Unaudited {} address for runtime {}", names[i], version.string()));
    }
    REX::INFO("OG/NG port: six native hook mappings match audited runtime {}; appearance/settings path unchanged",version.string());
}
}

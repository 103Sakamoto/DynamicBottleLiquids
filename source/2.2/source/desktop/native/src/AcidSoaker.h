#pragma once
#include "AcidSoakerLevel.h"
#include <RE/N/NiControllerManager.h>
namespace nuka {
inline bool IsAcidSoakerTank(const RE::BSGeometry* geometry){
    return geometry&&geometry->name.c_str()&&std::string_view(geometry->name.c_str())=="DynamicBottleLiquids:acidsoaker-tank";
}
inline std::optional<float> AcidSoakerFill(RE::BSGeometry* geometry,float maximum,bool* sampleValid=nullptr){
    if(sampleValid)*sampleValid=false;
    if(!IsAcidSoakerTank(geometry))return {};
    RE::NiAVObject* owner=nullptr;
    auto* sequence=FindAcidSoakerSequence(static_cast<RE::NiAVObject*>(geometry),[&](RE::NiAVObject* node){
        auto* manager=RE::NiControllerManager::GetNiControllerManager(node);
        auto* candidate=manager?manager->GetSequenceByName(RE::BSFixedString("lowering")):nullptr;
        if(candidate)owner=node;
        return candidate;
    });
    auto fraction=sequence?AcidSoakerClipFraction(sequence->lastScaledTime,sequence->beginKeyTime,sequence->endKeyTime):std::nullopt;
    const bool clip=bool(fraction);
    RE::NiAVObject* lower=nullptr;
    if(!fraction){
        // Closest containing subtree first; do not take a pose from an unrelated
        // distant object or use a global equipped-weapon counter.
        auto* node=geometry->parent;
        for(unsigned depth=0;node&&depth<64;++depth,node=node->parent){
            lower=node->GetObjectByName(RE::BSFixedString("LowerBone"));
            if(lower){fraction=AcidSoakerEndpointFraction(lower->local.translate.z);break;}
        }
    }
    const unsigned frame=RE::BSGraphics::State::GetSingleton()->frameCount;
    static unsigned reportFrame=~0u;
    if(reportFrame==~0u||frame-reportFrame>=120){
        reportFrame=frame;
        std::string ancestry;
        for(auto* n=geometry->parent;n&&ancestry.size()<768;n=n->parent){
            if(!ancestry.empty())ancestry+=" > ";ancestry+=n->name.c_str()?n->name.c_str():"<unnamed>";
        }
        REX::INFO("Acid Soaker: source={} owner={} clipTime={} begin={} end={} lowerZ={} fraction={} fill={} ancestry={}",
            clip?"native lowering clip":(fraction?"native endpoint pose":"unavailable (empty fallback)"),
            owner&&owner->name.c_str()?owner->name.c_str():"<none>",sequence?sequence->lastScaledTime:-1.f,
            sequence?sequence->beginKeyTime:-1.f,sequence?sequence->endKeyTime:-1.f,
            lower?lower->local.translate.z:-1.f,fraction.value_or(0.f),fraction.value_or(0.f)*maximum,ancestry);
    }
    if(sampleValid)*sampleValid=fraction.has_value();
    return fraction.value_or(0.f)*maximum;
}
}

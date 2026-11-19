#pragma once
#include "BottleShapeName.h"
#include <RE/A/AlchemyItem.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESObjectREFR.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_set>

namespace nuka {
inline std::atomic<std::shared_ptr<const std::unordered_set<const RE::TESForm*>>> coldDrinkForms;

inline void InitializeColdDrinks() {
    auto* data=RE::TESDataHandler::GetSingleton();
    if(!data)return;
    auto forms=std::make_shared<std::unordered_set<const RE::TESForm*>>();
    const auto add=[&](std::string_view plugin,std::initializer_list<RE::TESFormID> ids){
        for(auto id:ids)if(auto* form=data->LookupForm(id,plugin);form&&form->GetFormType()==RE::ENUM_FORM_ID::kALCH)forms->insert(form);
    };
    // Local form IDs audited from the shipping ALCH records, independent of load order.
    add("Fallout4.esm",{0x178B17,0x178B18,0x178B19,0x178B1A,0x178B1B,0x178B1C,0x178B1D,0x178B23,0x178B24,0x178B25});
    add("DLCCoast.esm",{0x54281,0x54282,0x54284,0x54286});
    add("DLCNukaWorld.esm",{0x2EDA9,0x2EDAB,0x2EDAD,0x2EDAF,0x2EDB0,0x2EDB1,
        0x502A9,0x502AC,0x502AF,0x502B2,0x502B4,0x502B7,0x502BA,0x502BD,
        0x502C0,0x502C3,0x502C6,0x502C9,0x502CC,0x502CF,0x502D2,0x502D5,
        0x502D8,0x502DB,0x502DE,0x502E1});
    add("MojaveImports.esp",{0x823,0xC8DC,0xC8DF,0xC8E2,0x3CCE3});
    REX::INFO("Condensation: {} cold drink records registered; stationary droplets",forms->size());
    coldDrinkForms.store(std::move(forms));
}

inline bool IsColdBottle(RE::BSGeometry* geometry) {
    if(!geometry)return false;
    const auto name=BottleShapeName(geometry->name.c_str()?geometry->name.c_str():"");
    if(name.starts_with("DynamicBottleLiquids:")&&(name.ends_with("icecold")||name.ends_with("_cold")))return true;
    auto forms=coldDrinkForms.load();
    if(!forms)return false;
    auto* reference=RE::TESObjectREFR::FindReferenceFor3D(geometry);
    auto* form=reference?reference->GetObjectReference():nullptr;
    const bool cold=form&&forms->contains(form);
    static std::mutex logMutex;
    static std::unordered_set<const RE::TESForm*> reported;
    if(form){
        std::lock_guard lock(logMutex);
        if(reported.size()<64&&reported.insert(form).second)
            REX::INFO("Condensation identity: form={:08X} shape={} cold={}",form->GetFormID(),name,cold);
    }
    return cold;
}
}

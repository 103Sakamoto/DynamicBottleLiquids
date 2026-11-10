#pragma once
#include <string_view>
#include <cstdint>
namespace nuka {
inline const char* MixedDrinkFullModel(std::uint32_t id, std::string_view path) {
    if(id!=0x58AB8 && id!=0x101295)return nullptr;
    const auto same=[&](std::string_view expected){
        if(path.size()!=expected.size())return false;
        for(size_t i=0;i<path.size();++i){
            auto c=path[i];if(c=='/')c='\\';
            if(c>='A'&&c<='Z')c+=('a'-'A');
            if(c!=expected[i])return false;
        }
        return true;
    };
    if(same("setdressing\\kitchen\\beerbottle01.nif"))
        return "SetDressing\\Kitchen\\BeerBottleFull01.nif";
    if(same("setdressing\\dreadeye2020\\kitchen\\beerbottle01.nif"))
        return "SetDressing\\DreadEye2020\\Kitchen\\BeerBottleFull01.nif";
    return nullptr; // Preserve full models and unrelated replacers.
}
}

#include "PCH.h"
#include "Profiles.h"
#include <bit>
namespace nuka {
namespace {
void SetBase(Settings& s, std::initializer_list<std::pair<unsigned,unsigned>> values) {
    std::array<unsigned,sizeof(Constants)/4> words{};
    for(auto [index,value]:values) words.at(index)=value;
    s.base=std::bit_cast<Constants>(words);
}
void Add(DrinkMap& drinks,const char* shape,DrinkProfile d) {
    auto it=drinks.find(shape);
    if(it==drinks.end())drinks.emplace(shape,std::move(d));
    else it->second.variants.push_back(std::move(d));
}
#include "FrozenProfiles.inc"
}
const DrinkProfile* FindProfile(const DrinkMap& drinks,std::string_view shape,unsigned vertices,unsigned indices) {
    auto it=drinks.find(shape);if(it==drinks.end())return nullptr;
    auto match=[&](const DrinkProfile& p){return p.vertices==vertices&&p.indices==indices;};
    if(match(it->second))return &it->second;
    for(const auto& p:it->second.variants)if(match(p))return &p;
    return nullptr;
}
DrinkMap LoadDrinks() {
    DrinkMap drinks;
    // Fixed, approved 1.0 profiles. No runtime configuration or file overrides.
    PopulateProfiles(drinks);
    return drinks;
}
}

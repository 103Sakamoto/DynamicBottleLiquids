#pragma once
#include "Simulation.h"
#include <filesystem>
#include <unordered_map>
#include <string>
#include <string_view>
namespace nuka {
struct DrinkProfile {std::string meshName;Settings settings;Constants distant;unsigned vertices=0,indices=0;bool empty=false,emissive=false;std::vector<DrinkProfile> variants;};
struct ProfileNameHash {
    using is_transparent=void;
    size_t operator()(std::string_view name)const noexcept{return std::hash<std::string_view>{}(name);}
};
using DrinkMap=std::unordered_map<std::string,DrinkProfile,ProfileNameHash,std::equal_to<>>;
const DrinkProfile* FindProfile(const DrinkMap&,std::string_view,unsigned vertices,unsigned indices);
DrinkMap LoadDrinks();
}

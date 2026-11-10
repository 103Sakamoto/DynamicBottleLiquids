#pragma once
#include "Simulation.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>
namespace nuka {
struct DrinkProfile {std::string meshName; std::string materialSourceShape; unsigned materialSourceVertices=0,materialSourceIndices=0;Settings settings;Constants distant;unsigned vertices=0,indices=0;bool empty=false,emissive=false;std::vector<DrinkProfile> variants; nlohmann::json resolvedControls; std::vector<std::pair<std::string,std::shared_ptr<DrinkProfile>>> materialVariants;};
struct ProfileNameHash {
    using is_transparent=void;
    size_t operator()(std::string_view name)const noexcept{return std::hash<std::string_view>{}(name);}
};
using DrinkMap=std::unordered_map<std::string,DrinkProfile,ProfileNameHash,std::equal_to<>>;
const DrinkProfile* FindProfile(const DrinkMap&,std::string_view,unsigned vertices,unsigned indices);
const DrinkProfile* FindMaterialProfile(const DrinkProfile&,std::string_view sourceTexture);
DrinkMap LoadDrinks(const std::filesystem::path& root,const Settings& defaults,const nlohmann::json& tuning=nullptr);
std::string DrinkGroup(std::string mesh);
std::string TuningGroup(const nlohmann::json& tuning,std::string mesh);
void SetTuningMembership(nlohmann::json& tuning,std::string mesh,const std::string& group);
void AddTuningGroup(nlohmann::json& tuning,const std::string& group,const nlohmann::json& controls);
void RemoveTuningGroup(nlohmann::json& tuning,const std::string& group);
nlohmann::json AppearanceControls(const std::filesystem::path& root);
nlohmann::json LoadTuning(const std::filesystem::path& root,bool factory=false);
void ValidateTuning(const nlohmann::json& tuning);
bool DiagnosticsEnabled(const std::filesystem::path& root);
Settings LoadSettings(const std::filesystem::path& root);
}

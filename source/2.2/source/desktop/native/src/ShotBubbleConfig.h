#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <stdexcept>
namespace nuka {
struct ShotBubbleConfig {
    unsigned bigCount=3,smallCount=3;
    float bigRadiusMin=.24f,bigRadiusMax=.30f,smallRadiusMin=.10f,smallRadiusMax=.14f;
    float bigSpeedMin=5.2f,bigSpeedMax=6.f,smallSpeedMin=4.f,smallSpeedMax=4.8f;
};
inline ShotBubbleConfig ParseShotBubbleConfig(const nlohmann::json& j){
    ShotBubbleConfig c;
    auto group=[&](const char* name,unsigned& count,float& lo,float& hi,float& slow,float& fast){
        const auto& g=j.at(name);
        auto n=g.at("count");if(!n.is_number_integer()||n.get<int>()<0||n.get<int>()>12)throw std::runtime_error("count must be an integer from 0 to 12");count=n.get<unsigned>();
        lo=g.at("radius_min").get<float>();hi=g.at("radius_max").get<float>();
        slow=g.at("speed_min").get<float>();fast=g.at("speed_max").get<float>();
        if(!std::isfinite(lo+hi+slow+fast)||lo<.01f||hi> .5f||lo>hi||slow<.1f||fast>20.f||slow>fast)throw std::runtime_error("invalid bubble radius/speed range");
    };
    group("big",c.bigCount,c.bigRadiusMin,c.bigRadiusMax,c.bigSpeedMin,c.bigSpeedMax);
    group("small",c.smallCount,c.smallRadiusMin,c.smallRadiusMax,c.smallSpeedMin,c.smallSpeedMax);
    return c;
}
inline ShotBubbleConfig LoadShotBubbleConfig(const std::filesystem::path& path){
    if(!std::filesystem::exists(path))return {};
    std::ifstream file(path);return ParseShotBubbleConfig(nlohmann::json::parse(file));
}
inline ShotBubbleConfig acidShotConfig;
}

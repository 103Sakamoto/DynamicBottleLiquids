#include "PCH.h"
#include "Profiles.h"
#include <iostream>
int main(int argc,char** argv){try{
    if(argc!=2)return 2;
    auto settings=nuka::LoadSettings(argv[1]);
    auto profiles=nuka::LoadDrinks(argv[1],settings,nuka::LoadTuning(argv[1]));
    if(profiles.empty())throw std::runtime_error("No profiles loaded");
    const auto& tank=profiles.at("DynamicBottleLiquids:acidsoaker-tank");
    if(tank.vertices!=172||tank.indices!=642||std::abs(tank.settings.fill-.94f)>.00001f||
       std::abs(tank.settings.base.absorption.w-.45f)>.00001f||tank.empty)
        throw std::runtime_error("Acid tank profile was changed by drink group tuning");
    const auto& feed=profiles.at("DynamicBottleLiquids:acidsoaker-feed");
    if(feed.vertices!=644||feed.indices!=3240||feed.settings.fill!=1||feed.settings.intensity!=0||feed.settings.foamStrength!=0||feed.settings.shakingBubbles!=0)
        throw std::runtime_error("Static feed tube profile failed");
    std::cout<<"PASS startup: "<<profiles.size()<<" profiles loaded\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL startup: "<<e.what()<<'\n';return 1;}}

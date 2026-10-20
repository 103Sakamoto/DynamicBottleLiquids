#include "PCH.h"
#include "Profiles.h"
#include <iostream>
using namespace nuka;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
 auto profiles=LoadDrinks();require(profiles.size()==111,"Unexpected shape count");unsigned total=0;
 auto test=[&](const DrinkProfile& p,const std::string& name){
  ++total;auto& s=p.settings;require(p.vertices>0&&p.indices>0,"Invalid geometry identity");require(s.base.ringCount>=2&&s.base.ringCount<=16,"Invalid ring count");
  require(FindProfile(profiles,name,p.vertices,p.indices)!=nullptr,"Profile lookup failed");
  Motion motion;for(int frame=0;frame<80;frame++){
   auto c=Simulate(s,motion,{float(frame%7)*.1f,.1f,-.2f},Normalize({.2f,.1f,1}),1.f/60,true,false);
   require(std::isfinite(c.plane.w)&&std::isfinite(c.wave.x),"Non-finite simulation");require(c.bubbleCount>=0&&c.bubbleCount<=192,"Bubble capacity exceeded");
  }
 };
 for(auto& [name,p]:profiles){test(p,name);for(auto& v:p.variants)test(v,name);}
 require(total==142,"Unexpected profile count");std::cout<<"PASS: 142 profiles, default-stack startup, geometry lookup and 11,360 simulation frames.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

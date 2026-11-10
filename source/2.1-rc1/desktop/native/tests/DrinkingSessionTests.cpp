#include "DrinkingSession.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace nuka;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
float run(int fps){DrinkingSession s(.95f);for(int i=0;i<fps;i++)s.Update(-.5f,1.f/fps);return s.remaining;}
int main(){try{
    struct Node {Node* parent=nullptr;};
    Node scene,hand{&scene},prop{&hand},glass{&prop};
    std::vector<const Node*> attachment{&glass,&prop,&hand,&scene};
    require(SameAttachment(&glass,attachment),"Attached session survives visibility/cache changes");
    prop.parent=nullptr;
    require(!SameAttachment(&glass,attachment),"Detect detached ancestor with glass still parented");
    prop.parent=&hand;glass.parent=nullptr;
    require(!SameAttachment(&glass,attachment),"Detect detached glass");
    glass.parent=&prop;Node otherHand{&scene};prop.parent=&otherHand;
    require(!SameAttachment(&glass,attachment),"Detect reattachment under same scene root");
    require(IsDrinkingProp("DynamicBottleLiquids:iafanimobjectnukareal"),"IAF recognition");
    require(!IsDrinkingProp("DynamicBottleLiquids:nukacolabottlefull"),"World exclusion");
    DrinkingSession s(.95f);
    for(int i=0;i<600;i++)s.Update(1,1.f/60);
    require(s.remaining==.95f,"Upright must not drain");
    for(int i=0;i<5;i++)s.Update(-.5f,1.f/60);
    require(s.remaining==.95f,"Brief tilt rejected");
    s.Update(1,1.f/60);
    for(int i=0;i<60;i++)s.Update(-.5f,1.f/60);
    require(s.remaining<.314f&&s.remaining>.311f,"Timed depletion");
    float saved=s.remaining;
    for(int i=0;i<600;i++)s.Update(1,1.f/60);
    require(s.remaining==saved,"Upright never refills");
    s.Update(-1,0);s.Update(-1,5);s.Update(-1,-1);s.Update(std::numeric_limits<float>::quiet_NaN(),.1f);
    require(s.remaining==saved,"Pause/stall/invalid input");
    require(std::abs(run(30)-run(144))<.0001f,"Frame-rate independence");
    for(int i=0;i<600;i++)s.Update(-1,1.f/60);
    require(s.remaining==0,"Clamp empty");
    DrinkingSession next(.95f);require(next.remaining==.95f,"Fresh prop starts full");
    std::cout<<"PASS: IAF eligibility, sustained tilt, pause, monotonic volume, frame rate and fresh session\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}


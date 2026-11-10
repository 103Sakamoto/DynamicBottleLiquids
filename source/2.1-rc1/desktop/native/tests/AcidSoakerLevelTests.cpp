#include "AcidSoakerLevel.h"
#include <iostream>
#include <limits>
#include <stdexcept>
void require(bool ok){if(!ok)throw std::runtime_error("Native clip mapping failed");}
int main(){try{
    struct Node {Node* parent=nullptr;int* sequence=nullptr;};
    int native=1,other=2;
    Node actor{nullptr,&other},renamedWeapon{&actor,&native},ordered{&renamedWeapon},glass{&ordered};
    auto probe=[](Node* n){return n->sequence;};
    require(nuka::FindAcidSoakerSequence(&glass,probe)==&native);
    require(nuka::FindAcidSoakerSequence(&renamedWeapon,probe)==&native);
    Node missing{};require(!nuka::FindAcidSoakerSequence(&missing,probe));
    require(!nuka::FindAcidSoakerSequence(static_cast<Node*>(nullptr),probe));
    nuka::AcidSoakerShotTracker shots;
    require(shots.Sample(1.f)==0);
    unsigned total=0;
    for(int frame=1;frame<=2000;++frame)total+=shots.Sample(1.f-frame/2000.f);
    require(total==20);
    require(shots.Sample(0.f)==0);
    require(shots.Sample(.5f)==0);require(shots.Sample(1.f)==0);
    require(shots.Sample(.95f)==1);require(shots.Sample(.95001f)==0);
    require(shots.Sample({})==0);require(shots.Sample(.7f)==0);
    constexpr float end=20.f/30.f;
    for(int ammo=0;ammo<=20;++ammo){
        const auto f=nuka::AcidSoakerClipFraction((20-ammo)/30.f,0,end);
        require(f&&std::abs(*f-ammo/20.f)<.00001f);
    }
    float previous=0;
    for(int step=0;step<=100;++step){
        auto f=nuka::AcidSoakerClipFraction(end*(1-step/100.f),0,end);
        require(f&&*f>=previous);previous=*f;
    }
    require(!nuka::AcidSoakerClipFraction(-1,0,end));
    require(!nuka::AcidSoakerClipFraction(end+1,0,end));
    require(!nuka::AcidSoakerClipFraction(0,1,1));
    require(!nuka::AcidSoakerClipFraction(std::numeric_limits<float>::quiet_NaN(),0,end));
    require(nuka::AcidSoakerEndpointFraction(7.8400206566f)==1.f);
    require(nuka::AcidSoakerEndpointFraction(2.6392197609f)==0.f);
    require(!nuka::AcidSoakerEndpointFraction(5));
    std::cout<<"PASS: shot deduplication/refill/reset, renamed ancestor/nearest-controller lookup, 21 ammo steps, reverse reload mapping, invalid samples, endpoint fallback\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

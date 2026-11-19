#include "PCH.h"
#include "Profiles.h"
#include <iostream>
int main(int argc,char**argv){try{if(argc!=2)return 2;auto root=std::filesystem::path(argv[1]);auto d=nuka::LoadDrinks(root,nuka::LoadSettings(root),nuka::LoadTuning(root));if(d.empty())return 3;std::cout<<"PASS "<<d.size()<<" profiles\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}

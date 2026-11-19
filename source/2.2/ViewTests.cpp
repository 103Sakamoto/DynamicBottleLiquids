#include "PCH.h"
#include "NativeOptics.h"
#include <iostream>
#include <stdexcept>
namespace nuka {
struct NativeOpticsViewTests {
 static void Require(bool b){if(!b)throw std::runtime_error("view isolation regression");}
 static void Run(){
  NativeOptics::secondaryView=false;auto* main=&Native();
  main->frame=1;main->captured=1;main->drawn=1;main->composedFrame=1;
  main->missed=0;main->failed=false;
  NativeOptics::secondaryView=true;auto* mirror=&Native();Require(main!=mirror);
  for(unsigned f=2;f<12;++f){mirror->BeginFrame(f);Require(!mirror->failed);}
  Require(main->frame==1&&main->captured==1&&main->drawn==1&&main->composedFrame==1&&main->missed==0);
  NativeOptics::secondaryView=false;Require(&Native()==main);main->BeginFrame(12);Require(!main->failed&&main->missed==0);
  // Same real production watchdog must still fail on three unfinished main captures.
  for(unsigned f=13;f<16;++f){main->captured=1;main->drawn=0;main->BeginFrame(f);}
  Require(main->failed&&main->missed==3&&!mirror->failed);
  std::cout<<"PASS: distinct renderers; secondary frames leave main capture untouched; main safety latch still trips at three missing compositions\n";
 }
};
}
int main(){try{nuka::NativeOpticsViewTests::Run();return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}


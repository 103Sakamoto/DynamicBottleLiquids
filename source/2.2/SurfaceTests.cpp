#include "PCH.h"
#include "Profiles.h"
#include "SurfaceNoise.h"
#include "WaveSizing.h"
#include <iostream>
#include <stdexcept>
using namespace nuka;
void Check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
int main(int argc,char**argv){try{
 Check(argc==2,"settings path required");
 auto noise=MakeSurfaceNoise(),again=MakeSurfaceNoise();Check(noise==again,"noise deterministic");size_t bytes=0;
 for(int mip=0,size=128;mip<8;++mip,size/=2){Check(noise[mip].size()==size*size*4,"mip dimensions");bytes+=noise[mip].size()*2;
 if(mip)for(int y=0;y<size;++y)for(int x=0;x<size;++x)for(int ch=0;ch<4;++ch){int sum=0;for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=noise[mip-1][((y*2+dy)*size*2+x*2+dx)*4+ch];Check(noise[mip][(y*size+x)*4+ch]==std::lround(sum/4.f),"mip filtering");}}
 Check(bytes==174760,"texture memory");
 auto settings=LoadSettings(argv[1]);auto profiles=LoadDrinks(argv[1],settings,LoadTuning(argv[1]));int cases=0;
 for(auto&[name,profile]:profiles){auto cached=profile.settings;cached.fill=.53f;cached.intensity=0;cached.shakingBubbles=0;cached.foamStrength=0; auto doubled=cached;for(int i=0;i<doubled.base.ringCount;++i){doubled.base.rings[i].x*=2;doubled.base.rings[i].y*=2;doubled.base.rings[i].z*=2;}MakeSamples(doubled);
 Check(std::abs(WaveReferenceRadius(doubled)-2*WaveReferenceRadius(cached))<.00001f,"radius scaling");
 Motion smallMotion{},largeMotion{};smallMotion.waveEnergy=largeMotion.waveEnergy=.8f;smallMotion.rippleEnergy=largeMotion.rippleEnergy=.8f;
 auto smallConstants=Simulate(cached,smallMotion,{}, {0,0,1},0);auto largeConstants=Simulate(doubled,largeMotion,{}, {0,0,1},0);
 Check(std::abs(largeConstants.wave.x-smallConstants.wave.x*2)<.00001f&&std::abs(largeConstants.waveDetail.x-smallConstants.waveDetail.x*2)<.00001f,"actual wave amplitudes must scale with bottle");
 auto empty=cached;empty.fill=0;Check(WaveReferenceRadius(empty)==0,"empty suppression");empty.fill=1;Check(WaveReferenceRadius(empty)==0,"full suppression");empty.fill=.001f;Check(WaveReferenceRadius(empty)<WaveReferenceRadius(cached),"shallow suppression");
 auto direct=cached;direct.sampleWaveBasis.clear();direct.wetPhases.clear();
 Motion a{},b{};for(int frame=0;frame<40;++frame){float t=frame*.1f;Vec up=Normalize({std::sin(t)*.6f,std::cos(t)*.4f,1});Vec force{std::sin(t)*250,std::cos(t)*150,0};auto x=Simulate(cached,a,force,up,1.f/60);auto y=Simulate(direct,b,force,up,1.f/60);Check(std::isfinite(x.plane.w),"finite plane");Check(std::abs(x.plane.w-y.plane.w)<.002f,"cached and direct volume disagree");for(int i=0;i<64;++i){const float* p=&x.wetFilm[i].x;const float*q=&y.wetFilm[i].x;for(int j=0;j<4;++j)Check(std::abs(p[j]-q[j])<.0001f,"cached and direct wet contact disagree");}++cases;}
 float time=a.time;auto x=Simulate(cached,a,{},a.lastUp,0);auto y=Simulate(cached,a,{},a.lastUp,0);if(!(a.time==time&&x.wave.z==y.wave.z&&x.wave.x==y.wave.x&&std::abs(x.plane.w-y.plane.w)<.0001f)){std::cerr<<name<<" pause "<<time<<" "<<a.time<<" wave "<<x.wave.x<<" "<<y.wave.x<<" height "<<x.plane.w<<" "<<y.plane.w<<"\n";throw std::runtime_error("pause changed surface");}
 }
 std::cout<<"PASS "<<cases<<" cached/direct frames, 113 bottle-scaling/empty/full/shallow checks, paused surfaces, 8 mip levels, 174760-byte deterministic ripple texture\n";
 return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}




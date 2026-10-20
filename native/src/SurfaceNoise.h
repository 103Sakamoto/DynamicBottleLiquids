#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace nuka {
// Seamless, deterministic smooth noise, built once; signed mip averages approach zero.
inline auto MakeSurfaceNoise(){
    std::array<std::vector<std::int8_t>,8> levels;
    auto lattice=[](int x,int y,int period,unsigned channel){
        std::uint32_t h=std::uint32_t(x%period)*374761393u+std::uint32_t(y%period)*668265263u+channel*2246822519u;
        h=(h^(h>>13))*1274126177u;h^=h>>16;
        return float(h&65535u)/32767.5f-1.f;
    };
    levels[0].resize(128*128*2);
    for(int y=0;y<128;y++)for(int x=0;x<128;x++)for(unsigned ch=0;ch<2;ch++){
        float value=0;
        for(int octave=0;octave<3;octave++){
            int period=8<<octave;float px=float(x)*period/128,py=float(y)*period/128;
            int ix=int(px),iy=int(py);float u=px-ix,v=py-iy;u=u*u*(3-2*u);v=v*v*(3-2*v);
            float a=std::lerp(lattice(ix,iy,period,ch+1),lattice(ix+1,iy,period,ch+1),u);
            float b=std::lerp(lattice(ix,iy+1,period,ch+1),lattice(ix+1,iy+1,period,ch+1),u);
            value+=std::lerp(a,b,v)*(octave==0?.6f:(octave==1?.3f:.1f));
        }
        levels[0][(y*128+x)*2+ch]=std::int8_t(std::lround(value*127));
    }
    for(int mip=1,size=64;mip<8;mip++,size/=2){
        levels[mip].resize(size*size*2);
        for(int y=0;y<size;y++)for(int x=0;x<size;x++)for(int ch=0;ch<2;ch++){
            int sum=0;for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)sum+=levels[mip-1][((y*2+dy)*size*2+x*2+dx)*2+ch];
            levels[mip][(y*size+x)*2+ch]=std::int8_t(std::lround(sum/4.f));
        }
    }
    return levels;
}
}

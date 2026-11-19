#pragma once
#include <array>
#include <chrono>
namespace nuka {
using SimulationTimings=std::array<double,7>;
inline constexpr const char* SimulationStageNames[]{"Sim motion","Sim volume solve","Sim wet film","Sim shaking bubbles","Sim surface foam","Sim carbonation","Sim output packing"};
class SimulationStageTimer {
    SimulationTimings* result;
    std::chrono::steady_clock::time_point last;
public:
    explicit SimulationStageTimer(SimulationTimings* target):result(target){if(result)last=std::chrono::steady_clock::now();}
    void End(unsigned stage){if(!result)return;auto now=std::chrono::steady_clock::now();(*result)[stage]+=std::chrono::duration<double,std::milli>(now-last).count();last=now;}
};
}

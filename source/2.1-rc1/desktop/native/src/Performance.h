#pragma once
#include <d3d11.h>
#include "Logging.h"
#include "SimulationTimings.h"
#include <wrl/client.h>
#include <array>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <chrono>
namespace nuka {

struct TimingMetric {std::string name;double average=0,p95=0;bool available=false;bool sampled=false;};
struct PerformanceReport {
    bool enabled=false,stale=true;
    unsigned frames=0,detailedFrames=0,skipped=0,invalid=0,dropped=0;
    TimingMetric cpuTotal,gpuTotal;
    std::vector<TimingMetric> cpu,gpu;
    std::vector<std::pair<std::string,unsigned>> activity;
};

class Performance {
public:
    static constexpr unsigned DetailedInterval=16;
    void Initialize(ID3D11Device* device);
    void SetDrawInfo(unsigned frame,double cpuMs);
    void AddCpu(double ms);
    void AddBubbles(unsigned simulated,unsigned rendered,bool held);
    void AddCpuStages(double simulation,double lighting);
    void AddSimulationStages(const SimulationTimings& timings);
    int Begin(ID3D11DeviceContext* context);
    void Mark(ID3D11DeviceContext* context,int token,unsigned point);
    void End(ID3D11DeviceContext* context,int token,bool copied,bool depth);
    void Poll(ID3D11DeviceContext* context);
    std::string Summary();
    PerformanceReport Inspect();
    void Reset();
    int BeginNative(ID3D11DeviceContext*,unsigned phase);
    void CaptureResult(bool accepted);
    void DirectResult();
    void CloseFrame();
    void AddNativeCpu(unsigned phase,double ms,double queries=0,double reporting=0,bool direct=false);
    void AddProducerCpu(double setup,double draw,double restore,bool direct=false);
    void ComposePath(bool regional);
    void AddComposeCpu(double draw,double copy);
    void StartWindow();
private:
    using Query=Microsoft::WRL::ComPtr<ID3D11Query>;
    struct Slot {std::array<Query,5> timestamps;bool inUse=false,pending=false,copied=false,depth=false;unsigned frame=0,epoch=0,phase=0,issued=0,group=0;};
    struct FrameQuery {Query disjoint;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;bool open=false,pending=false,ready=false,failed=false;UINT64 frequency=0;unsigned frame=0,epoch=0,remaining=0;};
    struct Sample {SimulationTimings simulationStages{};double setup=0,other=0,copy=0,color=0,depth=0,total=0,cpu=0,update=0,simulation=0,lighting=0;double nativeGpu[4]{},nativeCpu[4]{},producerCpu[3]{},producerQueries=0,producerReporting=0,directCpu=0,directStages[3]{};double earlyNestedCpu=0;double producerTargets=0,producerDraw=0,producerOther=0;double composeDraw=0,composeCopy=0,composeOther=0,composeCpuDraw=0,composeCpuCopy=0;bool bottleGpuUnavailable=false,detailedGpuFrame=false;unsigned bottleQueryScopes=0,composeQueryScopes=0,suppressedBottleScopes=0,frameDisjoints=0,timestampWrites=0;unsigned regional=0,fullscreen=0;unsigned direct=0,accepted=0,rejected=0,producers=0,composites=0;unsigned draws=0,bubblesSimulated=0,bubblesRendered=0,heldDraws=0;};
    struct Frame {unsigned id=0,pending=0;bool closed=false,bad=false;Sample sum;bool detailSelected=false;};
    std::deque<Frame> frames;
    void Publish(); // mutex held; publish only complete frames in submission order
    void CloseQueryFrame(); // mutex held, render thread only; Reset never touches D3D
    std::array<Slot,2048> slots;
    std::array<FrameQuery,32> queryFrames;
    std::mutex mutex;
    std::vector<Sample> samples;
    unsigned frame=0,skipped=0,invalid=0,epoch=0,dropped=0,sampleSequence=0,slotCursor=0,groupCursor=0;
    int activeGroup=-1;
    double lastActivity=0;
    bool ready=false,earlyScope=false;
};
Performance& Perf();
inline std::atomic<bool> perBottleGpuTiming{false};
inline std::atomic<bool> enbDepthBypass{false}; // Test only: skip native scene-depth rejection.
inline std::atomic<bool> regionalComposition{true};

inline std::atomic<int> lightingProbe{0};
inline std::atomic<int> refractionProbe{0}; // 0 native optical displacement, 1 identity displacement

}

namespace nuka {
class NativeTiming {
    ID3D11DeviceContext* context;unsigned phase;int token;bool direct;
    std::chrono::steady_clock::time_point start;
public:
    void Point(unsigned p){Perf().Mark(context,token,p);}
    bool Measured() const{return token>=0;}
    double reportingMs=0;
    NativeTiming(ID3D11DeviceContext* c,unsigned p,bool immediate=false):context(c),phase(p),token(Perf().BeginNative(c,p)),direct(immediate),start(extensiveLogging?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{}){}
    ~NativeTiming(){if(!extensiveLogging)return;auto q=std::chrono::steady_clock::now();Perf().Mark(context,token,4);Perf().End(context,token,false,false);auto end=std::chrono::steady_clock::now();Perf().AddNativeCpu(phase,std::chrono::duration<double,std::milli>(end-start).count(),std::chrono::duration<double,std::milli>(end-q).count(),reportingMs,direct);}
};
}

#include "PCH.h"
#include "Performance.h"
#include <thread>
namespace nuka {
static double Seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Performance& Perf(){static auto* instance=new Performance;return *instance;}
void Performance::Initialize(ID3D11Device* device){if(!extensiveLogging)return;
    if(ready)return;
    D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
    for(auto& group:queryFrames)if(FAILED(device->CreateQuery(&d,&group.disjoint)))throw std::runtime_error("Performance frame disjoint query unavailable");
    d.Query=D3D11_QUERY_TIMESTAMP;
    for(auto& slot:slots)for(auto& q:slot.timestamps)if(FAILED(device->CreateQuery(&d,&q)))throw std::runtime_error("Performance timestamp query unavailable");
    ready=true;
}
void Performance::Publish(){
    while(!frames.empty()&&frames.front().closed&&!frames.front().pending){
        auto f=frames.front();frames.pop_front();
        if(f.bad){dropped++;continue;}
        if(!f.sum.accepted&&!f.sum.rejected&&!f.sum.producers&&!f.sum.draws)continue;
        samples.push_back(f.sum);if(samples.size()>240)samples.erase(samples.begin());
    }
}
void Performance::SetDrawInfo(unsigned f,double ms){if(!extensiveLogging)return;
    std::lock_guard lock(mutex);
    if(frames.empty()||f!=frame){
        CloseQueryFrame();
        if(!frames.empty())frames.back().closed=true;
        frame=f;frames.push_back(Frame{f});
        frames.back().detailSelected=perBottleGpuTiming.load() && (sampleSequence++%std::max(1u,gpuTimingInterval.load())==0);
        Publish();
        while(frames.size()>512){frames.pop_front();dropped++;}
    }
    frames.back().sum.update+=ms;
}
void Performance::AddBubbles(unsigned simulated,unsigned rendered,bool held){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()){auto& s=frames.back().sum;s.bubblesSimulated+=simulated;s.bubblesRendered+=rendered;s.heldDraws+=held?1:0;}}
void Performance::AddCpuStages(double simulation,double lighting){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()){frames.back().sum.simulation+=simulation;frames.back().sum.lighting+=lighting;}}
void Performance::AddSimulationStages(const SimulationTimings& timings){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()&&!frames.back().closed)for(unsigned i=0;i<timings.size();++i)frames.back().sum.simulationStages[i]+=timings[i];}
void Performance::AddCpu(double ms){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()){frames.back().sum.cpu+=ms;if(earlyScope)frames.back().sum.earlyNestedCpu+=ms;}}
void Performance::CloseQueryFrame(){
    if(activeGroup<0)return;
    auto& group=queryFrames[activeGroup];
    group.context->End(group.disjoint.Get());group.open=false;group.pending=true;activeGroup=-1;
}
void Performance::Poll(ID3D11DeviceContext* ctx){if(!extensiveLogging)return;
    ServiceCpuScope service(2);
    if(!ready)return;
    // Only called at HDR entry. Never wait or flush the GPU for profiling.
    std::lock_guard lock(mutex);
    for(auto& group:queryFrames)if(group.pending){
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
        auto result=ctx->GetData(group.disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(result==S_FALSE)continue;
        group.pending=false;group.ready=true;group.frequency=d.Frequency;
        group.failed=FAILED(result)||d.Disjoint||!d.Frequency;
        if(group.epoch==epoch){
            auto f=std::find_if(frames.begin(),frames.end(),[&](auto& f){return f.id==group.frame;});
            if(f!=frames.end()){f->pending--;if(group.failed){invalid++;f->bad=true;}}
        }
    }
    for(auto& slot:slots)if(slot.pending){
        auto& group=queryFrames[slot.group];if(!group.ready)continue;
        UINT64 t[5]{};bool complete=true,failed=group.failed;
        if(!failed)for(unsigned k=0;k<5;k++){
            if(!(slot.issued&(1u<<k)))continue;
            auto result=ctx->GetData(slot.timestamps[k].Get(),&t[k],sizeof(t[k]),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(result==S_FALSE)complete=false;if(FAILED(result))failed=true;
        }
        if(!complete&&!failed)continue;
        slot.pending=false;slot.inUse=false;group.remaining--;
        if(slot.epoch!=epoch)continue;
        auto f=std::find_if(frames.begin(),frames.end(),[&](auto& f){return f.id==slot.frame;});
        if(f==frames.end())continue;
        f->pending--;
        if(failed||t[4]<t[0]){if(!group.failed)invalid++;f->bad=true;continue;}
        double factor=1000.0/group.frequency;
        auto& sum=f->sum;
        for(unsigned k=0;k<5;k++)sum.timestampWrites+=(slot.issued>>k)&1u;
        double duration=(t[4]-t[0])*factor;sum.nativeGpu[slot.phase-1]+=duration;
        if(slot.phase==2){
            if(slot.issued==31 && std::is_sorted(std::begin(t),std::end(t))){
                sum.producerTargets+=(t[1]-t[0])*factor;
                sum.producerDraw+=(t[3]-t[2])*factor;
                sum.producerOther+=((t[2]-t[1])+(t[4]-t[3]))*factor;
            }else sum.producerOther+=duration;
        }
        if(slot.phase==3){
            if(slot.issued==31 && std::is_sorted(std::begin(t),std::end(t))){
                sum.composeDraw+=(t[2]-t[1])*factor;sum.composeCopy+=(t[3]-t[2])*factor;
                sum.composeOther+=((t[1]-t[0])+(t[4]-t[3]))*factor;
            }else sum.composeOther+=duration;
        }
        sum.total+=duration;
    }
    for(auto& group:queryFrames)if(group.ready&&!group.remaining){group.ready=false;group.context.Reset();}
    Publish();
}
int Performance::Begin(ID3D11DeviceContext* ctx){return BeginNative(ctx,2);}
int Performance::BeginNative(ID3D11DeviceContext* ctx,unsigned phase){
    if(!extensiveLogging||!ready||phase<1||phase>4)return -1;
    std::lock_guard lock(mutex);if(frames.empty()||frames.back().closed)return -1;
    lastActivity=Seconds();auto& f=frames.back();if(phase==4)earlyScope=true;
    if(phase==2){f.sum.producers++;f.sum.draws++;}if(phase==3)f.sum.composites++;
    // Selection is fixed for the entire frame, including all bottles and stages.
    if(phase!=3 && (!f.detailSelected||f.bad)){
        f.sum.bottleGpuUnavailable=true;f.sum.suppressedBottleScopes++;return -1;
    }
    if(f.bad)return -1;
    if(activeGroup<0){
        for(unsigned attempt=0;attempt<queryFrames.size();attempt++){
            unsigned i=groupCursor++%queryFrames.size();auto& group=queryFrames[i];
            if(group.open||group.pending||group.ready)continue;
            group.frame=frame;group.epoch=epoch;group.remaining=0;group.failed=false;
            group.context=ctx;group.open=true;activeGroup=int(i);f.pending++;f.sum.frameDisjoints++;
            ctx->Begin(group.disjoint.Get());break;
        }
        if(activeGroup<0){skipped++;f.bad=true;return -1;}
    }
    for(unsigned attempt=0;attempt<slots.size();attempt++){
        unsigned i=slotCursor++%slots.size();auto& slot=slots[i];if(slot.inUse)continue;
        slot.frame=frame;slot.epoch=epoch;slot.phase=phase;slot.issued=1;slot.group=unsigned(activeGroup);slot.inUse=true;slot.pending=false;
        f.pending++;queryFrames[activeGroup].remaining++;
        if(phase!=3){f.sum.bottleQueryScopes++;f.sum.detailedGpuFrame=true;}else f.sum.composeQueryScopes++;
        ctx->End(slot.timestamps[0].Get());return int(i);
    }
    skipped++;f.bad=true;return -1;
}
void Performance::DirectResult(){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()&&!frames.back().closed)frames.back().sum.direct++;}
void Performance::CaptureResult(bool accepted){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(frames.empty()||frames.back().closed)return;lastActivity=Seconds();if(accepted)frames.back().sum.accepted++;else frames.back().sum.rejected++;}
void Performance::AddNativeCpu(unsigned phase,double ms,double queries,double reporting,bool direct){if(!extensiveLogging)return;auto before=std::chrono::steady_clock::now();std::lock_guard lock(mutex);if(!frames.empty()&&!frames.back().closed){auto& s=frames.back().sum;if(phase==4)earlyScope=false;s.nativeCpu[phase-1]+=ms;if(direct)s.directCpu+=ms;if(phase==2){s.producerQueries+=queries;s.producerReporting+=reporting+std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();}}}
void Performance::AddProducerCpu(double setup,double draw,double restore,bool direct){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()&&!frames.back().closed){auto& s=frames.back().sum;auto* stages=direct?s.directStages:s.producerCpu;stages[0]+=setup;stages[1]+=draw;stages[2]+=restore;}}
void Performance::ComposePath(bool regional){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()&&!frames.back().closed){if(regional)frames.back().sum.regional++;else frames.back().sum.fullscreen++;}}
void Performance::AddComposeCpu(double draw,double copy){if(!extensiveLogging)return;std::lock_guard lock(mutex);if(!frames.empty()&&!frames.back().closed){frames.back().sum.composeCpuDraw+=draw;frames.back().sum.composeCpuCopy+=copy;}}
void Performance::CloseFrame(){if(!extensiveLogging)return;std::lock_guard lock(mutex);CloseQueryFrame();if(!frames.empty())frames.back().closed=true;Publish();}
void Performance::Mark(ID3D11DeviceContext* ctx,int token,unsigned point){if(!extensiveLogging)return;
    if(token<0||point>=5)return;auto& slot=slots[token];
    if(slot.issued&(1u<<point))return;
    slot.issued|=1u<<point;ctx->End(slot.timestamps[point].Get());
}
void Performance::End(ID3D11DeviceContext*,int token,bool copied,bool depth){if(!extensiveLogging)return;
    if(token<0)return;auto& slot=slots[token];slot.copied=copied;slot.depth=depth;slot.pending=true;
}
void Performance::Reset(){if(!extensiveLogging)return;
    // UI thread: retain open/pending D3D query objects until the render thread
    // closes and drains them. Discard the partial frame; never re-begin it.
    std::lock_guard lock(mutex);samples.clear();frames.clear();frames.push_back(Frame{frame,0,false,true});skipped=invalid=dropped=0;sampleSequence=0;epoch++;
}
PerformanceReport Performance::Inspect(){
    PerformanceReport report;report.enabled=extensiveLogging;
    // Basic mode must not copy/sort samples or start measuring just because the UI is open.
    if(!report.enabled)return report;
    std::vector<Sample> all;double activity=0;
    {std::lock_guard lock(mutex);all=samples;report.skipped=skipped;report.invalid=invalid;report.dropped=dropped;activity=lastActivity;}
    report.stale=Seconds()-activity>1;report.frames=unsigned(all.size());
    std::vector<Sample> detailed;
    if(perBottleGpuTiming.load())for(auto& sample:all)if(sample.detailedGpuFrame&&!sample.bottleGpuUnavailable)detailed.push_back(sample);
    report.detailedFrames=unsigned(detailed.size());
    auto metric=[&](const char* name,auto value,bool sampled=false){
        TimingMetric result;result.name=name;result.sampled=sampled;
        const auto& source=sampled?detailed:all;if(source.empty())return result;
        std::vector<double> values;values.reserve(source.size());double sum=0;
        for(auto& sample:source){double v=value(sample);values.push_back(v);sum+=v;}
        std::sort(values.begin(),values.end());result.average=sum/values.size();
        result.p95=values[size_t(std::ceil(.95*values.size()))-1];result.available=true;return result;
    };
    auto cpuTotal=[](const Sample& s){return s.cpu+std::max(0.0,s.nativeCpu[1]-s.directCpu)+s.nativeCpu[2]+std::max(0.0,s.nativeCpu[3]-s.earlyNestedCpu);};
    report.cpuTotal=metric("DBL CPU measured total",cpuTotal);
    report.gpuTotal=metric("DBL GPU measured total",[](const Sample& s){return s.total;},true);
    report.cpu={
        metric("Bottle hook",[](auto& s){return s.cpu;}),
        metric("Capture (included in hook)",[](auto& s){return s.nativeCpu[0];}),
        metric("Native producer",[](auto& s){return s.nativeCpu[1];}),
        metric("Producer setup",[](auto& s){return s.producerCpu[0];}),
        metric("Producer submit",[](auto& s){return s.producerCpu[1];}),
        metric("Producer restore",[](auto& s){return s.producerCpu[2];}),
        metric("Query end",[](auto& s){return s.producerQueries;}),
        metric("Reporting (overlaps producer)",[](auto& s){return s.producerReporting;}),
        metric("Direct distant draw (overlap correction)",[](auto& s){return s.directCpu;}),
        metric("Direct setup",[](auto& s){return s.directStages[0];}),
        metric("Direct submit",[](auto& s){return s.directStages[1];}),
        metric("Direct restore",[](auto& s){return s.directStages[2];}),
        metric("Native compose",[](auto& s){return s.nativeCpu[2];}),
        metric("God-ray shield CPU",[](auto& s){return s.nativeCpu[3];}),
        metric("Compose submit",[](auto& s){return s.composeCpuDraw;}),
        metric("Copy submit",[](auto& s){return s.composeCpuCopy;}),
        metric("Simulation",[](auto& s){return s.simulation;}),
        metric("Lighting preparation",[](auto& s){return s.lighting;}),
        metric("Preparation / update",[](auto& s){return s.update;}),
        metric("Sampled CPU total",cpuTotal,true)};
    for(unsigned i=0;i<std::size(SimulationStageNames);++i)report.cpu.push_back(metric(SimulationStageNames[i],[i](const auto& s){return s.simulationStages[i];}));
    report.gpu={
        metric("Capture / snapshot",[](auto& s){return s.nativeGpu[0];},true),
        metric("Native producer",[](auto& s){return s.nativeGpu[1];},true),
        metric("God-ray shield including direct application",[](auto& s){return s.nativeGpu[3];},true),
        metric("Targets prepare",[](auto& s){return s.producerTargets;},true),
        metric("Optical draw",[](auto& s){return s.producerDraw;},true),
        metric("Producer other",[](auto& s){return s.producerOther;},true),
        metric("Compose + resolve",[](auto& s){return s.nativeGpu[2];}),
        metric("Compose shader",[](auto& s){return s.composeDraw;}),
        metric("Output copy",[](auto& s){return s.composeCopy;}),
        metric("Compose other",[](auto& s){return s.composeOther;}),
        metric("Producer accounting error",[](auto& s){return std::abs(s.nativeGpu[1]-s.producerTargets-s.producerDraw-s.producerOther);},true),
        metric("Compose accounting error",[](auto& s){return std::abs(s.nativeGpu[2]-s.composeDraw-s.composeCopy-s.composeOther);}),
        metric("Total accounting error",[](auto& s){return std::abs(s.total-s.nativeGpu[0]-s.nativeGpu[1]-s.nativeGpu[2]-s.nativeGpu[3]);},true)};
    if(!all.empty()){
        const auto& s=all.back();report.activity={{"Accepted bottles",s.accepted},{"Rejected bottles",s.rejected},{"Producer draws",s.producers},{"Direct distant draws",s.direct},{"Composites",s.composites},{"Regional composition",s.regional},{"Full-screen composition",s.fullscreen},{"Bubbles simulated",s.bubblesSimulated},{"Bubbles submitted",s.bubblesRendered},{"Held draws",s.heldDraws},{"Bottle query scopes",s.bottleQueryScopes},{"Composition query scopes",s.composeQueryScopes},{"Suppressed bottle scopes",s.suppressedBottleScopes}};
        unsigned bottles=0,composes=0,disjoints=0,timestamps=0;
        for(auto& sample:all){bottles+=sample.bottleQueryScopes;composes+=sample.composeQueryScopes;disjoints+=sample.frameDisjoints;timestamps+=sample.timestampWrites;}
        report.activity.insert(report.activity.end(),{{"Window bottle queries",bottles},{"Window composition queries",composes},{"Window disjoint queries",disjoints},{"Window timestamp writes",timestamps}});
    }
    return report;
}
std::string Performance::Summary(){
    // Allocate before locking; percentile sorting and text formatting use only this snapshot.
    std::vector<Sample> sampleSnapshot;sampleSnapshot.reserve(240);
    unsigned skippedSnapshot,invalidSnapshot,droppedSnapshot;double lastActivitySnapshot;
    {std::lock_guard lock(mutex);sampleSnapshot.assign(this->samples.begin(),this->samples.end());skippedSnapshot=this->skipped;invalidSnapshot=this->invalid;droppedSnapshot=this->dropped;lastActivitySnapshot=this->lastActivity;}
    std::string text="DYNAMIC BOTTLE LIQUIDS - PERFORMANCE TEST\n\n";
    if(Seconds()-lastActivitySnapshot>1)text+="No recent bottle draw. Showing last measurements.\n";
    if(!sampleSnapshot.empty()){auto& s=sampleSnapshot.back();text+=std::format("Native latest: {} accepted / {} rejected / {} producer draws / {} composites\n",s.accepted,s.rejected,s.producers,s.composites);text+=std::format("Direct distant draws: {} (included in producer total)\n",s.direct);text+=std::format("Composition path: {} regional / {} full-screen\n",s.regional,s.fullscreen);text+=std::format("GPU query scopes: {} bottle / {} composition / {} bottle scopes suppressed\n",s.bottleQueryScopes,s.composeQueryScopes,s.suppressedBottleScopes);text+=std::format("Bubbles: {} simulated / {} submitted; held draws: {}\n",s.bubblesSimulated,s.bubblesRendered,s.heldDraws);}
    text+=std::format("Complete frames: {} / 240   skipped queries: {}   invalid: {}\n\n",sampleSnapshot.size(),skippedSnapshot,invalidSnapshot);
    if(sampleSnapshot.empty())text+="Waiting for GPU results. Look at a supported bottle.\n";
    else{
        std::vector<Sample> detailed;
        for(auto& sample:sampleSnapshot)if(sample.detailedGpuFrame&&!sample.bottleGpuUnavailable)detailed.push_back(sample);
        text+=std::format("GPU sample coverage: {} / {} completed frames; interval {} bottle-active frames\n",detailed.size(),sampleSnapshot.size(),gpuTimingInterval.load());
        unsigned bottles=0,composes=0,disjoints=0,timestamps=0;
        for(auto& sample:sampleSnapshot){bottles+=sample.bottleQueryScopes;composes+=sample.composeQueryScopes;disjoints+=sample.frameDisjoints;timestamps+=sample.timestampWrites;}
        text+=std::format("Window query totals: {} bottle / {} composition / {} shared disjoint / {} timestamps\n",bottles,composes,disjoints,timestamps);
        if(!detailed.empty()){
            const auto& last=detailed.back();
            text+=std::format("Latest detailed frame: {} bottle / {} composition / {} shared disjoint / {} timestamps\n",last.bottleQueryScopes,last.composeQueryScopes,last.frameDisjoints,last.timestampWrites);
        }
        auto metric=[&](const char* label,auto value,bool sampledOnly=false){const auto& source=sampledOnly?detailed:sampleSnapshot;std::vector<double> v;double sum=0;for(auto& s:source){double x=value(s);v.push_back(x);sum+=x;}std::sort(v.begin(),v.end());return std::format("{:<21} {:8.3f} ms avg   {:8.3f} ms p95\n",label,sum/v.size(),v[size_t(std::ceil(.95*v.size()))-1]);};
        text+="MEASURED NATIVE WORK PER BOTTLE-ACTIVE FRAME\n";
        bool completeGpu=perBottleGpuTiming.load()&&!detailed.empty();
        if(completeGpu){
        text+=metric("GPU capture/snapshot",[](auto s){return s.nativeGpu[0];},true);
        text+=metric("GPU native producer",[](auto s){return s.nativeGpu[1];},true);
        text+=metric("GPU god-ray shield",[](auto s){return s.nativeGpu[3];},true);
        text+=metric("GPU targets prepare",[](auto s){return s.producerTargets;},true);
        text+=metric("GPU optical draw",[](auto s){return s.producerDraw;},true);
        text+=metric("GPU producer other",[](auto s){return s.producerOther;},true);
        text+=metric("GPU producer error",[](auto s){return std::abs(s.nativeGpu[1]-s.producerTargets-s.producerDraw-s.producerOther);},true);
        }else{
            text+="GPU capture/snapshot      N/A (profiling off or no sampled GPU frame)\nGPU native producer       N/A (profiling off or no sampled GPU frame)\nGPU producer stages       N/A (profiling off or no sampled GPU frame)\n";
        }
        text+=metric("GPU compose + resolve",[](auto s){return s.nativeGpu[2];});
        text+=metric("GPU compose shader",[](auto s){return s.composeDraw;});
        text+=metric("GPU output copy",[](auto s){return s.composeCopy;});
        text+=metric("GPU compose other",[](auto s){return s.composeOther;});
        text+=metric("GPU compose error",[](auto s){return std::abs(s.nativeGpu[2]-s.composeDraw-s.composeCopy-s.composeOther);});
        if(completeGpu){
        text+=metric("GPU measured total",[](auto s){return s.total;},true);
        text+=metric("GPU accounting error",[](auto s){return std::abs(s.total-s.nativeGpu[0]-s.nativeGpu[1]-s.nativeGpu[2]-s.nativeGpu[3]);},true);
        }else text+="GPU measured total        N/A (bottle GPU work unmeasured)\nGPU accounting error      N/A (incomplete GPU coverage)\n";
        text+=metric("CPU bottle hook",[](auto s){return s.cpu;});
        text+=metric("CPU capture (subset)",[](auto s){return s.nativeCpu[0];});
        text+=metric("CPU native producer",[](auto s){return s.nativeCpu[1];});
        text+=metric("CPU producer setup",[](auto s){return s.producerCpu[0];});
        text+=metric("CPU producer submit",[](auto s){return s.producerCpu[1];});
        text+=metric("CPU producer restore",[](auto s){return s.producerCpu[2];});
        text+=metric("CPU query end",[](auto s){return s.producerQueries;});
        text+=metric("CPU perf reporting",[](auto s){return s.producerReporting;});
        text+=metric("CPU direct (subset)",[](auto s){return s.directCpu;});
        text+=metric("CPU direct setup",[](auto s){return s.directStages[0];});
        text+=metric("CPU direct submit",[](auto s){return s.directStages[1];});
        text+=metric("CPU direct restore",[](auto s){return s.directStages[2];});
        text+=metric("CPU measured total",[](auto s){return s.cpu+std::max(0.0,s.nativeCpu[1]-s.directCpu)+s.nativeCpu[2]+std::max(0.0,s.nativeCpu[3]-s.earlyNestedCpu);});
        if(completeGpu)text+=metric("CPU sampled total",[](auto s){return s.cpu+std::max(0.0,s.nativeCpu[1]-s.directCpu)+s.nativeCpu[2]+std::max(0.0,s.nativeCpu[3]-s.earlyNestedCpu);},true);
        text+=metric("CPU native compose",[](auto s){return s.nativeCpu[2];});
        text+=metric("CPU god-ray shield",[](auto s){return s.nativeCpu[3];});
        text+=metric("CPU compose submit",[](auto s){return s.composeCpuDraw;});
        text+=metric("CPU copy submit",[](auto s){return s.composeCpuCopy;});
        text+=metric("CPU simulation",[](auto s){return s.simulation;});
        for(unsigned i=0;i<std::size(SimulationStageNames);++i)text+=metric(SimulationStageNames[i],[i](const auto& s){return s.simulationStages[i];});
        text+=metric("CPU lighting prep",[](auto s){return s.lighting;});
        text+=metric("CPU preparation",[](auto s){return s.update;});
    }
    text+="Sim stage rows are subsets of CPU simulation, not extra costs. They exclude adapter preparation, interpolation and budget allocation. Timers run only with Extensive logging.\n";
    text+=std::format("Incomplete/reset frames excluded: {}\n",droppedSnapshot);
    text+=std::format("\nF10 lighting probe: {} (0 normal, 1 black shell, 2 liquid mask)\n",lightingProbe.load());
    text+=std::format("\nF6 refraction: {} (0 native distortion, 1 distortion off)\n",refractionProbe.load());
    text+=std::format("\nF3 ENB composition colour probe: {}\n",enbDepthBypass.load()?"ON: red=depth rejected, blue=opaque, yellow=zero shift, green=shift present":"NORMAL");
    text+=std::format("\nF11 composition: {}\n",regionalComposition.load()?"regional when supported":"full-screen reference");
    text+=std::format("\nDetailed GPU profiling: {} (reference shader)\n",perBottleGpuTiming.load()?std::format("ON - every {} active frame(s); shared frame timing",gpuTimingInterval.load()):"OFF - snapshot/producer GPU unavailable");
    text+="\nF8: hide/show panel   F9: reset measurements\n";
    text+="1. Look at one bottle; hold still for 10 seconds.\n2. Repeat with several bottles, then a close-up.\n3. Send DynamicBottleLiquids.log when finished.\n\n";
    text+="Automatic log summary every 5 seconds.\nCPU capture is included in bottle-hook CPU: do not add them.\nProducer stage rows cover optical draws; direct stage rows cover distant draws.\nDirect CPU overlaps both bottle hook and native producer.\nCPU total = hook + producer - direct + compose + shield minus nested hook overlap. GPU total includes shield preparation/fallback and shared refraction copy (inside producer).\nQuery end measures producer closing query calls; reporting includes lock waiting.\nReporting overlaps producer total; do not sum these rows.\nTargets prepare includes allocation/clears; optical draw includes shading and depth writes.\nProducer other includes setup/restoration and distant direct draws.\nProducer stages partition GPU native producer; do not add them to total.\nExtensive logging uses the interval reported in GPU sample coverage. One disjoint query is shared by the frame.\nCPU/composition rows cover all completed frames; detailed GPU/total rows cover sampled frames only.\nGPU sample timings are not multiplied by the sampling interval. Composition remains timed every frame.\nGPU total is unavailable when bottle work is unmeasured; use external frame times.\nRegional compose shader includes GPU bounds reduction; output copy may be compute.\nCompose shader/copy/other partition GPU compose + resolve; do not add to total.\nCPU compose/copy submit are subsets of CPU native compose.\nDiagnostic overhead included; not a whole-game FPS test.\nUse borderless/windowed mode to see this separate panel.";
    return text;
}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_TIMER){
        static bool f3=false,f11=false,f6=false,f8=false,f9=false,f10=false;DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        bool active=pid==GetCurrentProcessId();bool a=active&&(GetAsyncKeyState(VK_F8)&0x8000),b=active&&(GetAsyncKeyState(VK_F9)&0x8000);
        if(a&&!f8)ShowWindow(window,IsWindowVisible(window)?SW_HIDE:SW_SHOWNOACTIVATE);
        if(b&&!f9)Perf().Reset();f8=a;f9=b;
        bool c=active&&(GetAsyncKeyState(VK_F10)&0x8000);
        if(c&&!f10){int mode=(lightingProbe.load()+1)%3;lightingProbe.store(mode);REX::INFO("Lighting isolation mode {}: 0 normal, 1 black opaque shell, 2 green liquid / purple empty",mode);}
        f10=c;
        bool d=active&&(GetAsyncKeyState(VK_F6)&0x8000);
        if(d&&!f6){int mode=(refractionProbe.load()+1)%2;refractionProbe.store(mode);Perf().Reset();REX::INFO("Refraction test mode {}: 0 native distortion, 1 distortion off; tint and lighting retained.",mode);}
        f6=d;
        bool depthKey=active&&(GetAsyncKeyState(VK_F3)&0x8000);if(depthKey&&!f3){enbDepthBypass.store(!enbDepthBypass.load());REX::INFO("ENB composition probe F3: {}",enbDepthBypass.load()?"ON red/depth blue/opaque yellow/zero green/shift (full-screen diagnostic)":"OFF normal rendering");}f3=depthKey;
        bool regionKey=active&&(GetAsyncKeyState(VK_F11)&0x8000);if(regionKey&&!f11){regionalComposition.store(!regionalComposition.load());Perf().Reset();REX::INFO("Composition mode: {}",regionalComposition.load()?"regional when supported":"full-screen reference");}f11=regionKey;
        static double last=0;double now=Seconds();if(now-last>=5){last=now;REX::INFO("Performance panel:\n{}",Perf().Summary());}
        if(IsWindowVisible(window))InvalidateRect(window,nullptr,FALSE);return 0;
    }
    if(message==WM_CLOSE){ShowWindow(window,SW_HIDE);return 0;}
    if(message==WM_PAINT){PAINTSTRUCT p;auto dc=BeginPaint(window,&p);RECT r;GetClientRect(window,&r);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));SetTextColor(dc,RGB(220,235,220));SetBkMode(dc,TRANSPARENT);auto font=SelectObject(dc,GetStockObject(ANSI_FIXED_FONT));r.left+=12;r.top+=12;auto text=Perf().Summary();DrawTextA(dc,text.c_str(),int(text.size()),&r,DT_LEFT|DT_TOP|DT_NOPREFIX);SelectObject(dc,font);EndPaint(window,&p);return 0;}
    return DefWindowProcW(window,message,w,l);
}
void Performance::StartWindow(){if(!extensiveLogging)return;
    static bool started=false;if(started)return;started=true;
    std::thread([]{
        HINSTANCE module=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=module;wc.lpszClassName=L"DynamicBottleLiquidsPerformance";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        if(!RegisterClassW(&wc)){REX::WARN("Performance window registration failed: {}",GetLastError());return;}
        HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"DynamicBottleLiquids performance - F8 hide/show",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,20,40,780,710,nullptr,nullptr,module,nullptr);
        if(!window){REX::WARN("Performance window creation failed: {}",GetLastError());return;}
        ShowWindow(window,SW_SHOWNOACTIVATE);SetTimer(window,1,100,nullptr);
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }).detach();
}
}


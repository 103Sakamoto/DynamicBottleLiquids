#include "PCH.h"
#include "ShieldHooks.h"
#include "GodrayShield.h"
#include "Performance.h"
#include <RE/B/BSGraphics.h>
#include <MinHook.h>
namespace nuka {
namespace {
using ApplyFn=int(*)(void*,ID3D11DeviceContext*,ID3D11RenderTargetView*,ID3D11ShaderResourceView*,const void*,int,float,ID3D11BlendState*);
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
ApplyFn originalApply=nullptr;
DrawFn originalDraw=nullptr;
ID3D11DeviceContext* immediate=nullptr;
ShieldDirect direct;
bool directReady=false;
struct Application {ID3D11DeviceContext* context;ID3D11RenderTargetView* output;bool prepared=false,handled=false;};
thread_local Application* current=nullptr;
bool Prepare(ID3D11DeviceContext* c,ID3D11RenderTargetView* target,bool copy){
    NativeTiming timing(c,4);
    return Shield().Before(c,target,copy);
}
void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* c,UINT count,UINT start){
    auto* a=current;
    if(!a||a->handled||c!=a->context){originalDraw(c,count,start);return;}
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;c->OMGetRenderTargets(1,&target,nullptr);
    if(target.Get()!=a->output){originalDraw(c,count,start);return;}
    a->handled=true;
    bool supported=count==3&&start==0&&direct.Matches(c);
    try{
        // Unsupported SDK states need a fresh background AFTER SDK resolve,
        // as in Test 3's copy/restore fallback.
        if(!a->prepared||!supported)a->prepared=Prepare(c,a->output,!supported);
    }catch(const std::exception& e){Shield().Cancel();a->prepared=false;REX::WARN("Shield preparation failed: {}",e.what());}
    if(supported&&a->prepared){
        NativeTiming timing(c,4);
        direct.Draw(c,Shield().MaskView(),Shield().Bounds(),originalDraw);
        Shield().Cancel();a->prepared=false;
        static bool logged=false;if(!logged){logged=true;REX::INFO("Shield: fixed-early mask and audited direct SDK application active; no scene copy/restore.");}
    }else originalDraw(c,count,start);
}
int Apply(void* library,ID3D11DeviceContext* c,ID3D11RenderTargetView* target,ID3D11ShaderResourceView* depth,const void* post,int quality,float factor,ID3D11BlendState* blend){
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bound;if(!target)c->OMGetRenderTargets(1,&bound,nullptr);
    auto* output=target?target:bound.Get();bool prepared=false,route=false;
    Shield().BeginFrame(RE::BSGraphics::State::GetSingleton()->frameCount);
    if(Shield().Captured()){
        route=directReady&&c==immediate&&output&&!current&&direct.Observed();
        try{prepared=Prepare(c,output,!route);}catch(const std::exception& e){Shield().Cancel();REX::WARN("Shield early preparation failed: {}",e.what());}
    }
    Application application{c,output,prepared};auto* previous=current;
    if(route)current=&application;
    auto result=originalApply(library,c,target,depth,post,quality,factor,blend);
    current=previous;
    if(route){prepared=application.prepared;if(!application.handled){Shield().Cancel();prepared=false;directReady=false;REX::WARN("Shield final SDK draw not intercepted; using fallback on subsequent calls.");}}
    try{if(result==0&&prepared){NativeTiming timing(c,4);Shield().After(c,output);}else Shield().Cancel();}
    catch(const std::exception& e){Shield().Cancel();REX::WARN("Shield fallback restore failed: {}",e.what());}
    return result;
}
}
void InitializeShield(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& path){
    Shield().Initialize(device,path);
    auto module=GetModuleHandleW(L"GFSDK_GodraysLib.x64.dll");
    auto* apply=module?GetProcAddress(module,"?GFSDK_GodraysLib_ApplyLighting@@YA?AW4GFSDK_GodraysLib_Status@@PEAUGFSDK_GodraysLib_Ctx@@PEAUID3D11DeviceContext@@PEAUID3D11RenderTargetView@@PEAUID3D11ShaderResourceView@@PEBUGFSDK_GodraysLib_PostProcessDesc@@W4GFSDK_GodraysLib_UpsampleQuality@@MPEAUID3D11BlendState@@@Z"):nullptr;
    if(!apply)throw std::runtime_error("Shield ApplyLighting export unavailable");
    if(MH_CreateHook(reinterpret_cast<void*>(apply),reinterpret_cast<void*>(&Apply),reinterpret_cast<void**>(&originalApply))!=MH_OK||MH_EnableHook(reinterpret_cast<void*>(apply))!=MH_OK)throw std::runtime_error("Shield ApplyLighting hook failed");
    Shield().enabled=true;immediate=context;
    if(direct.Initialize(device,path)){
        auto** table=*reinterpret_cast<void***>(context);
        if(MH_CreateHook(table[13],reinterpret_cast<void*>(&Draw),reinterpret_cast<void**>(&originalDraw))==MH_OK){
            directReady=MH_EnableHook(table[13])==MH_OK;
            if(!directReady){MH_RemoveHook(table[13]);originalDraw=nullptr;}
        }
    }
    REX::INFO("Shield initialized: directReady={}, settled-ground activation unchanged",directReady);
}
}

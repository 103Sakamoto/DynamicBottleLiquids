#include "PCH.h"
#include "Tuning.h"
#include <atomic>
#if defined(DBL_UI_MF)
namespace RE { class InputEvent; }
#include "F4SEMenuFramework.h"
#elif defined(DBL_UI_DMUI)
#include <DearModdingUI/Client.h>
#endif
namespace nuka {
namespace {
std::atomic_bool ready=false;
#if defined(DBL_UI_MF)
MENU_WINDOW overlayWindow=nullptr;
void __stdcall DrawEditor(){DrawTuning(true);}
void __stdcall DrawOverlay(){
 ImGuiMCP::SetNextWindowPos({16,16},ImGuiMCP::ImGuiCond_Always,{0,0});
 ImGuiMCP::SetNextWindowBgAlpha(.8f);
 if(ImGuiMCP::Begin("DBL Performance##DBLOverlay",nullptr,ImGuiMCP::ImGuiWindowFlags_NoInputs|ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize|ImGuiMCP::ImGuiWindowFlags_NoFocusOnAppearing))DrawTuning();ImGuiMCP::End();
}
#elif defined(DBL_UI_DMUI)
dmui::Client client{"DynamicBottleLiquids","Dynamic Bottle Liquids",dmui::Version{1,3},"sliders"};
DMUI_PageHandle overlayPage{};
#endif
}
void InitializeTuningRenderer(IDXGISwapChain*){
 static bool attempted=false;if(attempted)return;attempted=true;
#if defined(DBL_UI_MF)
 auto module=F4SEMenuFramework_GetHostModule();if(!module){REX::WARN("Selected F4SE Menu Framework is not installed; bottle effects remain enabled");return;}
 // The public wrapper assumes required cimgui exports exist. Validate those used
 // by DBL before any render callback can reach them.
 for(auto name: {
#include "MfRequiredExports.inc"
 })if(!GetProcAddress(module,name)){REX::WARN("Menu Framework missing {}; DBL UI not registered",name);return;}
 auto add=reinterpret_cast<F4SEMenuFramework::Model::AddWindowFunction>(GetProcAddress(module,"AddWindow"));
 auto section=reinterpret_cast<F4SEMenuFramework::Model::AddSectionItemFunction>(GetProcAddress(module,"AddSectionItem"));
 if(!section)return;
 section("Dynamic Bottle Liquids/Settings",DrawEditor);
 overlayWindow=add(DrawOverlay);if(!overlayWindow)return;
 overlayWindow->BlockUserInput=false;ready=true;
#elif defined(DBL_UI_DMUI)
 if(!client.Connect()){REX::WARN("Selected Dear Modding UI unavailable or incompatible; bottle effects remain enabled");return;}
 auto e=client.AddPage({.id="appearance",.displayName="DBL Appearance",.kind=DMUI_PAGE_KIND_SETTINGS},[]{DrawTuning(true);});
 auto o=client.AddPage({.id="performance",.displayName="DBL Performance",.kind=DMUI_PAGE_KIND_OVERLAY},[]{DrawTuning();});
 if(!e||!o)return;overlayPage=*o;
 DMUI_ManagedOverlayOptions options{};options.anchor=DMUI_OVERLAY_ANCHOR_TOP_LEFT;options.offset={16,16};options.size={460,230};options.minimumSize={440,210};options.maximumSize={4096,4096};options.opacity=1;options.contentScale=1;options.backgroundVisible=1;options.borderVisible=1;options.allowArrangement=0;
 if(!client.ConfigureOverlay(overlayPage,options))return;ready=true;
#else
 REX::INFO("In-game settings disabled by installer choice; automatic fog and saved bottle settings remain active");
#endif
 if(ready)REX::INFO("Selected DBL UI adapter ready");
}
bool FusionPreviewSession(bool){return false;} // Settings panels are owned by the selected host.
void TickFusionUI(){
 if(!ready)return;
 DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);const bool focused=pid==GetCurrentProcessId();PollTuningHotkeys(focused);
#if defined(DBL_UI_MF)
 overlayWindow->IsOpen=focused&&TuningWantsDrawing()&&!TuningVisible();
#elif defined(DBL_UI_DMUI)
 const bool show=focused&&TuningWantsDrawing()&&!TuningVisible();static bool shown=false;
 if(show!=shown){if(show)shown=client.RequestFrame(overlayPage);else{(void)client.ReleaseFrame(overlayPage);shown=false;}}
#endif
}
}


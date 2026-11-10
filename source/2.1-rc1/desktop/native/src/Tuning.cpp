#include "PCH.h"
#include "Tuning.h"
#include "AppearanceEditorModel.h"
#include "EditorHotkeys.h"
#include "Performance.h"
#if !defined(DBL_NO_UI)
#include "TuningWidgets.h"
#endif
#include <thread>
#include <atomic>
#include <cstdio>
namespace nuka { namespace {
using Json=nlohmann::json;
std::atomic_bool requested=false,visible=false,overlay=false,ready=false;
std::mutex gate,targetGate;std::string targetMesh;
AppearanceEditorModel model;std::filesystem::path root;
std::function<bool(bool)> session;
std::string status="Pick up a supported drink to edit its effects in game.";bool error=false,dirty=false;ULONGLONG due=0;
EditorHotkey editorKey{},overlayKey{VK_F8,true,false,false};HotkeyEdge editorEdge,overlayEdge;
std::string selectedGroup,editGroup;Json groupEdits=Json::object();char newGroup[81]{};int selectedLogging=0;bool savedLogging=false;
PerformanceReport report;ULONGLONG nextReport=0;
#ifndef DBL_NO_UI
struct Field {const char* key;const char* label;int tab;float lo,hi;bool percent=false,color=false;};
constexpr Field fields[]={
 {"liquid_color","Liquid color",0,0,1,false,true},{"liquid_level","Fill level",0,0,1,true},{"opacity","Opacity",0,0,1,true},
 {"bubble_density","Bubble density",0,0,10},{"shaking_bubbles","Shaking bubbles",0,0,1,true},{"surface_foam","Surface foam",0,0,1,true},{"glow_strength","Glow strength",0,0,10},
 {"glass_color","Glass color",1,0,1,false,true},{"glass_thickness","Glass thickness",1,.01f,1},{"glass_opacity","Glass opacity",1,0,.95f,true},{"refraction_strength","Refraction strength",1,0,20},{"reflection_strength","Reflection strength",1,0,10},
 {"wet_absorption","Wet absorption",2,0,1,true},{"wet_smoothness","Wet smoothness",2,0,1,true},{"wet_refraction","Wet refraction",2,0,1,true},{"wet_drying","Wet drying (seconds)",2,.1f,20}};
void Status(std::string s,bool failed=false){status=std::move(s);error=failed;}
template<class F> void Try(F&& fn){try{fn();}catch(const std::exception& e){Status(e.what(),true);}}
void WriteHotkeys(){
 EditorHotkey::Parse(editorKey.Text());EditorHotkey::Parse(overlayKey.Text());
 if(editorKey==overlayKey)throw std::runtime_error("Editor and overlay shortcuts must differ");
 auto temp=UserSettingsRoot(root)/"ui.json.tmp";Json j={{"editor",editorKey.Text()},{"overlay",overlayKey.Text()}};
 {std::ofstream out(temp);out<<j.dump(2);out.flush();if(!out)throw std::runtime_error("Cannot save shortcuts");}
 if(!MoveFileExW(temp.c_str(),(UserSettingsRoot(root)/"ui.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace ui.json");
 Status("Shortcuts saved. Changes apply immediately.");
}
bool ValueControl(const Field& f,Json& value){
 if(f.color){
  auto text=value.is_string()?value.get<std::string>():std::string("#FFFFFF");unsigned r=255,g=255,b=255;
  std::sscanf(text.c_str(),"#%02x%02x%02x",&r,&g,&b);float rgb[]={r/255.f,g/255.f,b/255.f};
  bool changed=ImGui::ColorEdit3("##value",rgb,ImGuiColorEditFlags_DisplayHex|ImGuiColorEditFlags_PickerHueWheel|ImGuiColorEditFlags_NoAlpha);
  if(changed){char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",int(rgb[0]*255+.5f),int(rgb[1]*255+.5f),int(rgb[2]*255+.5f));value=hex;}return changed;
 }
 float v=value.is_number()?value.get<float>():f.lo;float shown=f.percent?v*100:v;
 bool changed=ImGui::SliderFloat("##value",&shown,f.percent?f.lo*100:f.lo,f.percent?f.hi*100:f.hi,f.percent?"%.0f%%":"%.2f",ImGuiSliderFlags_AlwaysClamp);
 if(changed){if(f.percent)shown=std::clamp(std::round(shown),f.lo*100,f.hi*100);value=f.percent?shown/100:shown;}return changed;
}
// External panels can be narrower and use larger theme fonts than the Fusion window.
void ActionLine(const char* next,bool startRow=false){
#ifdef DBL_UI_DMUI
 if(startRow)return;
 ImGui::SameLine();
 if(ImGui::GetContentRegionAvail().x<ImGui::CalcTextSize(next).x+ImGui::CalcTextSize("MMMM").x)ImGui::NewLine();
#else
 ImGui::SameLine();
#endif
}
void ParameterWidth(float reserve){
#ifdef DBL_UI_DMUI
 ImGui::SetNextItemWidth(std::max(1.f,ImGui::GetContentRegionAvail().x));
#else
 ImGui::SetNextItemWidth(std::max(140.f,ImGui::GetContentRegionAvail().x-reserve));
#endif
}
void Appearance(int tab){
 if(model.mesh.empty()){ImGui::TextWrapped("Pick up a supported drink, close this editor and open it again.");return;}
 auto values=model.Values();auto group=model.Group();
 for(auto& f:fields)if(f.tab==tab){ImGui::PushID(f.key);ImGui::Separator();ImGui::TextUnformatted(f.label);
  Json value=values.value(f.key,f.color?Json("#FFFFFF"):Json(f.lo));
  ParameterWidth(355);
  if(ValueControl(f,value))Try([&]{model.Edit(f.key,value);if(!dirty)due=GetTickCount64()+120;dirty=true;Status("Live change pending. Save commits only this parameter.");});
  ActionLine("Save",true);if(ImGui::Button("Save"))Try([&]{model.SaveParameter(f.key,0);Status("Parameter saved to this drink.");});
  ActionLine("Save to group");ImGui::BeginDisabled(group.empty());if(ImGui::Button("Save to group"))Try([&]{model.SaveParameter(f.key,1);Status("Parameter saved to group; member overrides for this parameter cleared.");});
  ActionLine("Apply from group");if(ImGui::Button("Apply from group"))Try([&]{model.SaveParameter(f.key,2);Status("Parameter now inherits from its group; saved.");});ImGui::EndDisabled();ImGui::PopID();
 }
}
void Groups(){
#ifdef DBL_UI_DMUI
 static std::string removing;
 if(!removing.empty()){ImGui::Text("Remove group %s? Members become ungrouped; bottle overrides remain.",removing.c_str());
  if(ImGui::Button("Confirm removal")){Try([&]{model.DeleteGroup(removing);selectedGroup.clear();Status("Group removed and saved.");});removing.clear();}
  ActionLine("Cancel removal");if(ImGui::Button("Cancel removal"))removing.clear();return;
 }
#endif
 auto groups=model.Document()["groups"];
 if(!groups.contains(selectedGroup))selectedGroup=groups.empty()?"":groups.begin().key();
 ImGui::SetNextItemWidth(220);if(ImGui::BeginCombo("Group",selectedGroup.empty()?"No groups":selectedGroup.c_str())){for(auto it=groups.begin();it!=groups.end();++it)if(ImGui::Selectable(it.key().c_str(),it.key()==selectedGroup))selectedGroup=it.key();ImGui::EndCombo();}
 ImGui::SetNextItemWidth(220);ImGui::InputText("New group name",newGroup,sizeof(newGroup));
 if(ImGui::Button("Add group"))Try([&]{model.CreateGroup(newGroup);selectedGroup=newGroup;newGroup[0]=0;Status("Group created and saved.");});
 ActionLine("Remove group");ImGui::BeginDisabled(selectedGroup.empty());if(ImGui::Button("Remove group")){
#ifdef DBL_UI_DMUI
 removing=selectedGroup;
#else
 ImGui::OpenPopup("Remove this group?");
#endif
 }ImGui::EndDisabled();

#ifndef DBL_UI_DMUI
 if(ImGui::BeginPopupModal("Remove this group?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Members become ungrouped. Saved bottle overrides remain intact.");if(ImGui::Button("Remove")){Try([&]{model.DeleteGroup(selectedGroup);selectedGroup.clear();Status("Group removed and saved.");});ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
#endif

 ImGui::Separator();ImGui::Text("Held drink: %s",model.mesh.empty()?"None":model.mesh.c_str());auto current=model.Group();ImGui::Text("Current group: %s",current.empty()?"Ungrouped":current.c_str());
 ImGui::BeginDisabled(model.mesh.empty()||selectedGroup.empty()||selectedGroup==current);if(ImGui::Button("Add held drink to selected group"))Try([&]{model.AssignGroup(selectedGroup);Status("Membership saved; bottle overrides retained.");});ImGui::EndDisabled();ActionLine("Remove held drink from group");ImGui::BeginDisabled(model.mesh.empty()||current.empty());if(ImGui::Button("Remove held drink from group"))Try([&]{model.AssignGroup("");Status("Drink is now ungrouped; saved overrides retained.");});ImGui::EndDisabled();
 if(!selectedGroup.empty()&&groups.contains(selectedGroup)){ImGui::Separator();ImGui::TextUnformatted("Shared appearance (changes save to this group)");if(editGroup!=selectedGroup){editGroup=selectedGroup;groupEdits=Json::object();}auto values=model.GroupValues(selectedGroup);values.update(groupEdits);
  for(auto& f:fields){ImGui::PushID(f.key);ImGui::TextUnformatted(f.label);auto value=values.value(f.key,f.color?Json("#FFFFFF"):Json(f.lo));ParameterWidth(100);if(ValueControl(f,value))groupEdits[f.key]=value;ActionLine("Save",true);if(ImGui::Button("Save"))Try([&]{model.SaveGroupParameter(selectedGroup,f.key,value);groupEdits.erase(f.key);Status("Group parameter saved. Bottle overrides remain in effect.");});ImGui::PopID();}
 }
}
void Metric(const TimingMetric& m,const char* label){if(m.available)ImGui::Text("%s: %.3f ms avg | %.3f ms P95",label,m.average,m.p95);else ImGui::Text("%s: N/A",label);}
void Table(const char* name,const std::vector<TimingMetric>& rows){if(ImGui::BeginTable(name,4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)){ImGui::TableSetupColumn("Stage",0,3);ImGui::TableSetupColumn("Avg ms");ImGui::TableSetupColumn("P95 ms");ImGui::TableSetupColumn("Coverage");ImGui::TableHeadersRow();for(auto& m:rows){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(m.name.c_str());ImGui::TableNextColumn();if(m.available)ImGui::Text("%.3f",m.average);else ImGui::TextUnformatted("N/A");ImGui::TableNextColumn();if(m.available)ImGui::Text("%.3f",m.p95);else ImGui::TextUnformatted("N/A");ImGui::TableNextColumn();ImGui::TextUnformatted(m.sampled?"Sampled frames":"All frames");}ImGui::EndTable();}}
void Diagnostics(){
 ImGui::Text("Active logging: %s",extensiveLogging?"Extensive":"Basic");ImGui::SameLine();ImGui::Text("Saved: %s",savedLogging?"Extensive":"Basic");
 if(savedLogging!=extensiveLogging)ImGui::TextColored({1,.7f,.25f,1},"Restart required. Active logging and profiling have NOT changed yet.");
 const char* choices[]={"Basic","Extensive"};ImGui::SetNextItemWidth(180);ImGui::Combo("Logging mode",&selectedLogging,choices,2);ActionLine("Save logging mode");if(ImGui::Button("Save logging mode"))Try([&]{SaveLoggingMode(root,selectedLogging==1);savedLogging=selectedLogging==1;Status(savedLogging!=extensiveLogging?"Saved. Restart Fallout 4 to apply this logging mode.":"Saved. This logging mode is already active.");});
 ImGui::TextWrapped("Basic disables extensive logs, timing queries and performance collection. Startup, errors and limited health messages remain.");
 if(ImGui::Button(
#if defined(DBL_UI_MF) || defined(DBL_UI_DMUI)
 "Show performance overlay"
#else
 "Resume gameplay & show overlay"
#endif
 )){overlay=true;requested=false;}
 ActionLine("Reset measurements");ImGui::BeginDisabled(!extensiveLogging);if(ImGui::Button("Reset measurements")){Perf().Reset();nextReport=0;}ImGui::EndDisabled();
 ImGui::Separator();if(!extensiveLogging){ImGui::TextUnformatted("Profiling disabled. CPU/GPU measurements are unavailable in Basic mode.");return;}
 Metric(report.cpuTotal,"DBL CPU total");Metric(report.gpuTotal,"DBL GPU total");if(report.stale)ImGui::TextUnformatted("No recent bottle draw. Showing last measurements.");
 ImGui::Text("Frames: %u / 240 | Detailed GPU: %u / %u | sample interval 1/16",report.frames,report.detailedFrames,report.frames);
 if(ImGui::CollapsingHeader("How totals are calculated"))ImGui::TextWrapped("CPU = hook + max(0, producer - direct) + compose. GPU = capture + producer + compose on complete sampled frames. Totals and P95 are calculated per frame; overlapping/child stages are not added twice. These are DBL measurements, not whole-game frame time. CPU and GPU are not summed.");
 if(ImGui::CollapsingHeader("CPU timings",ImGuiTreeNodeFlags_DefaultOpen))Table("cpu",report.cpu);
 if(ImGui::CollapsingHeader("GPU timings",ImGuiTreeNodeFlags_DefaultOpen))Table("gpu",report.gpu);
 if(ImGui::CollapsingHeader("Activity & query health")){ImGui::Text("Skipped: %u | Invalid: %u | Excluded/reset: %u",report.skipped,report.invalid,report.dropped);for(auto& [label,value]:report.activity)ImGui::Text("%s: %u",label.c_str(),value);}
 if(ImGui::CollapsingHeader("Rendering probes")){int light=lightingProbe.load();const char* modes[]={"Normal","Black shell","Liquid mask"};if(ImGui::Combo("Lighting",&light,modes,3))lightingProbe=light;bool refract=refractionProbe.load()!=0;if(ImGui::Checkbox("Disable distortion (test)",&refract))refractionProbe=refract?1:0;bool enb=enbDepthBypass.load();if(ImGui::Checkbox("ENB composition color probe",&enb))enbDepthBypass=enb;bool region=regionalComposition.load();if(ImGui::Checkbox("Regional composition",&region)){regionalComposition=region;Perf().Reset();}ImGui::TextWrapped("Detailed GPU sampling follows the active Extensive mode. To stop profiling, save Basic and restart.");}
}
void HotkeyControl(const char* label,EditorHotkey& key){ImGui::PushID(label);ImGui::TextUnformatted(label);ImGui::Checkbox("Ctrl",&key.ctrl);ImGui::SameLine();ImGui::Checkbox("Shift",&key.shift);ImGui::SameLine();ImGui::Checkbox("Alt",&key.alt);ImGui::SameLine();const char* names[]={"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};int f=key.key-VK_F1;ImGui::SetNextItemWidth(110);if(ImGui::Combo("Key",&f,names,12))key.key=VK_F1+f;ImGui::PopID();}
void Shortcuts(){
#if defined(DBL_UI_MF) || defined(DBL_UI_DMUI)
 static EditorHotkey over;static bool init=false;if(!init){over=overlayKey;init=true;}
 ImGui::TextWrapped("Open DBL settings through this framework's configuration panel. DBL's appearance-editor shortcut is disabled.");
 HotkeyControl("Performance overlay",over);
 if(ImGui::Button("Save shortcut"))Try([&]{auto old=overlayKey;overlayKey=over;try{WriteHotkeys();}catch(...){overlayKey=old;throw;}overlayEdge.previous=true;});
 ActionLine("Restore default");if(ImGui::Button("Restore default"))over={VK_F8,true,false,false};
#else
static EditorHotkey edit{},over{VK_F8,true,false,false};static bool init=false;if(!init){edit=editorKey;over=overlayKey;init=true;}HotkeyControl("Appearance editor",edit);HotkeyControl("Performance overlay",over);if(ImGui::Button("Save shortcuts"))Try([&]{if(edit==over)throw std::runtime_error("Shortcuts must differ");auto oldEdit=editorKey,oldOver=overlayKey;editorKey=edit;overlayKey=over;try{WriteHotkeys();}catch(...){editorKey=oldEdit;overlayKey=oldOver;throw;}editorEdge.previous=overlayEdge.previous=true;});ImGui::SameLine();if(ImGui::Button("Restore defaults")){edit={};over={VK_F8,true,false,false};}ImGui::TextWrapped("Function keys F1-F12 support Ctrl, Shift and Alt combinations. Changes take effect when saved. Avoid shortcuts already assigned to other mods. Escape always closes the editor.");
#endif
}
#else
void Status(std::string,bool=false){}
template<class F> void Try(F&& fn){try{fn();}catch(const std::exception& e){REX::WARN("Settings: {}",e.what());}}
#endif
} // namespace
bool TuningVisible(){return visible.load();}
bool TuningWantsDrawing(){return ready&&(visible||overlay);}
void CloseTuning(){requested=false;}
void TuningTarget(std::string_view mesh){if(visible)return;std::lock_guard lock(targetGate);targetMesh=mesh;}
void PollTuningHotkeys(bool focused){if(!ready)return;std::lock_guard lock(gate);bool ctrl=GetAsyncKeyState(VK_CONTROL)&0x8000,shift=GetAsyncKeyState(VK_SHIFT)&0x8000,alt=GetAsyncKeyState(VK_MENU)&0x8000;
#if !defined(DBL_UI_MF) && !defined(DBL_UI_DMUI)
 if(editorEdge.Update(editorKey.Matches(focused,GetAsyncKeyState(editorKey.key)&0x8000,ctrl,shift,alt)))requested=!requested.load();
#endif
 if(overlayEdge.Update(overlayKey.Matches(focused,GetAsyncKeyState(overlayKey.key)&0x8000,ctrl,shift,alt))){bool show=!overlay.load();overlay=show;if(show)requested=false;}
 if(!focused)requested=false;
}
#ifndef DBL_NO_UI
void DrawTuning(bool hostPanel){std::lock_guard lock(gate);
 if(extensiveLogging&&GetTickCount64()>=nextReport){report=Perf().Inspect();nextReport=GetTickCount64()+500;}
#if defined(DBL_UI_MF) || defined(DBL_UI_DMUI)
 if(!ready)return;
 if(hostPanel){
  std::string target;{std::lock_guard targetLock(targetGate);target=targetMesh;}
  if(target!=model.mesh){if(dirty){Try([&]{model.Publish();});dirty=false;}model.Select(target);groupEdits=Json::object();editGroup.clear();}

  ImGui::Text("Held drink: %s",model.mesh.empty()?"None detected":model.mesh.c_str());
  ImGui::Text("Group: %s",model.Group().c_str());
  static int page=0;const char* tabs[]={"Liquid","Glass & reflections","Wetness","Groups","Diagnostics","Shortcuts"};
  ImGui::Combo("DBL page",&page,tabs,6);ImGui::Separator();
  if(page<3)Appearance(page);else if(page==3)Groups();else if(page==4)Diagnostics();else Shortcuts();
  ImGui::Separator();
#ifdef DBL_UI_DMUI
 ImGui::TextWrapped("%s%s",error?"Error: ":"",status.c_str());
#else
 ImGui::TextColored(error?ImVec4{1,.35f,.35f,1}:ImVec4{.7f,.8f,.8f,1},"%s",status.c_str());
#endif
  if(ImGui::Button("Reload saved"))Try([&]{model.Reload();dirty=false;groupEdits=Json::object();editGroup.clear();Status("Saved appearance reloaded.");});
 }
 if(overlay&&!hostPanel){
#ifndef DBL_UI_DMUI
 ImGui::TextUnformatted("Dynamic Bottle Liquids");
#endif
if(extensiveLogging){Metric(report.cpuTotal,"CPU");Metric(report.gpuTotal,"GPU");}else ImGui::TextUnformatted("Basic - profiling disabled");}
#else
 if(visible){bool open=true;ImGui::SetNextWindowSize({940,700},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSizeConstraints({650,350},ImGui::GetIO().DisplaySize);if(ImGui::Begin("Dynamic Bottle Liquids - Appearance",&open)){
  ImGui::Text("Held drink: %s",model.mesh.empty()?"None detected":model.mesh.c_str());auto group=model.Group();ImGui::Text("Group: %s | %s editor | %s overlay",group.empty()?"Ungrouped":group.c_str(),editorKey.Text().c_str(),overlayKey.Text().c_str());
  if(ImGui::BeginTabBar("DBL pages")){const char* tabs[]={"Liquid","Glass & reflections","Wetness","Groups","Diagnostics","Shortcuts"};for(int i=0;i<6;i++)if(ImGui::BeginTabItem(tabs[i])){ImGui::BeginChild("content",{0,-70});if(i<3)Appearance(i);else if(i==3)Groups();else if(i==4)Diagnostics();else Shortcuts();ImGui::EndChild();ImGui::EndTabItem();}ImGui::EndTabBar();}
  ImGui::Separator();ImGui::TextColored(error?ImVec4(1,.35f,.35f,1):ImVec4(.7f,.8f,.8f,1),"%s",status.c_str());if(ImGui::Button("Close / resume"))open=false;ImGui::SameLine();if(ImGui::Button("Reload saved"))Try([&]{model.Reload();dirty=false;groupEdits=Json::object();editGroup.clear();Status("Saved appearance reloaded.");});
 }ImGui::End();if(!open||ImGui::IsKeyPressed(ImGuiKey_Escape))requested=false;}
 if(overlay&&!visible){ImGui::SetNextWindowPos({16,16},ImGuiCond_Always);ImGui::SetNextWindowBgAlpha(.8f);ImGui::Begin("DBL Performance",nullptr,ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing);ImGui::TextUnformatted("Dynamic Bottle Liquids");if(extensiveLogging){Metric(report.cpuTotal,"CPU");Metric(report.gpuTotal,"GPU");if(report.stale)ImGui::TextUnformatted("No recent bottle draw - last measurements");}else ImGui::TextUnformatted("Basic - profiling disabled");if(savedLogging!=extensiveLogging)ImGui::Text("Restart required: saved %s",savedLogging?"Extensive":"Basic");ImGui::Text("%s hide | %s editor",overlayKey.Text().c_str(),editorKey.Text().c_str());ImGui::End();}
#endif
}
#else
void DrawTuning(bool hostPanel){}
#endif
void StartTuning(const std::filesystem::path& path,std::function<void(DrinkMap)> publish,std::function<bool(bool)> previewSession){
 root=path;session=std::move(previewSession);model.Initialize(root,std::move(publish));savedLogging=SavedExtensiveLogging(root);selectedLogging=savedLogging?1:0;
 Try([&]{auto file=UserSettingsRoot(root)/"ui.json";if(std::filesystem::exists(file)){std::ifstream in(file);auto j=Json::parse(in);auto a=EditorHotkey::Parse(j.at("editor").get<std::string>()),b=EditorHotkey::Parse(j.at("overlay").get<std::string>());
#if !defined(DBL_UI_MF) && !defined(DBL_UI_DMUI)
 if(a==b)throw std::runtime_error("Duplicate saved shortcuts; using defaults");
#endif
editorKey=a;overlayKey=b;}});ready=true;
 std::thread([]{bool owned=false;for(;;){TickFusionUI();bool want=requested.load();if(want!=owned){if(want){bool ok=!session||session(true);if(ok){std::string name;{std::lock_guard lock(targetGate);name=targetMesh;}{std::lock_guard lock(gate);model.Select(name);savedLogging=SavedExtensiveLogging(root);selectedLogging=savedLogging?1:0;Status("Editing held drink. Close / resume releases player controls.");}owned=true;visible=true;}else{requested=false;std::lock_guard lock(gate);Status("Could not acquire editor input. Gameplay remains active.",true);}}else{visible=false;if(session)session(false);owned=false;Perf().Reset();std::lock_guard lock(gate);nextReport=0;}}
  {std::lock_guard lock(gate);if(dirty&&GetTickCount64()>=due){dirty=false;Try([&]{model.Publish();Status("Live effect updated. Row actions save only that parameter.");});}}
  static ULONGLONG nextLog=0;if(extensiveLogging&&GetTickCount64()>=nextLog){nextLog=GetTickCount64()+5000;REX::INFO("Performance diagnostics:\n{}",Perf().Summary());}
  Sleep(16);
 }}).detach();
}
bool TuningSmokeTest(const std::filesystem::path& path){AppearanceEditorModel m;m.Initialize(path,[](DrinkMap){});return m.Select("NukaColaBottleFull.nif")&&!m.Values().empty();}
#ifdef DBL_ENGINE_FREE
bool TuningLayoutTest(const std::filesystem::path& path){
 root=path;model.Initialize(path,[](DrinkMap){});model.Select("NukaColaBottleFull.nif");savedLogging=SavedExtensiveLogging(root);
 auto* prior=ImGui::GetCurrentContext();auto* ctx=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,900};io.DeltaTime=1.f/60;unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);bool ok=true;
 for(int page=0;page<6;page++){ImGui::NewFrame();ImGui::SetNextWindowSize({1100,800});ImGui::Begin("Layout test");if(page<3)Appearance(page);else if(page==3)Groups();else if(page==4)Diagnostics();else Shortcuts();ImGui::End();ImGui::Render();ok=ok&&ImGui::GetDrawData()->TotalVtxCount>0;}
 overlay=true;visible=false;ready=true;
 for(int frame=0;frame<3;frame++){io.AddMousePosEvent(30,30);ImGui::NewFrame();DrawTuning();ImGui::Render();if(frame==2)ok=ok&&!io.WantCaptureMouse&&!io.WantCaptureKeyboard;}
 overlay=false;ready=false;
 ImGui::DestroyContext(ctx);ImGui::SetCurrentContext(prior);return ok;
}
#endif
} // namespace nuka


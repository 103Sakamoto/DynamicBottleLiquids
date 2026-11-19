#pragma once
#include <DearModdingUI/UI.h>
#include <algorithm>
#include <charconv>
#include <cstring>
using ImVec4=dmui::ui::Vec4;
constexpr int ImGuiColorEditFlags_DisplayHex=0,ImGuiColorEditFlags_PickerHueWheel=0,ImGuiColorEditFlags_NoAlpha=0;
constexpr auto ImGuiSliderFlags_AlwaysClamp=dmui::ui::SliderFlags::kAlwaysClamp;
constexpr auto ImGuiTableFlags_RowBg=dmui::ui::TableFlags::kRowBg,ImGuiTableFlags_BordersInnerH=dmui::ui::TableFlags::kBordersInnerH,ImGuiTableFlags_SizingStretchProp=dmui::ui::TableFlags::kSizingStretchProp;
constexpr auto ImGuiTreeNodeFlags_DefaultOpen=dmui::ui::TreeNodeFlags::kDefaultOpen;
namespace ImGui {
using namespace dmui::ui;
inline bool SliderFloat(const char* label,float* value,float lo,float hi,const char* format,SliderFlags flags=SliderFlags::kNone){return SliderScalar(label,value,&lo,&hi,format,flags);}
inline bool ColorEdit3(const char* label,float* rgb,int){
 PushID(label);bool changed=false;const char* channels[]={"Red","Green","Blue"};
 const float width=std::max(1.f,GetContentRegionAvail().x);
 for(int i=0;i<3;++i){TextUnformatted(channels[i]);SetNextItemWidth(width);PushID(i);changed=SliderFloat("##channel",&rgb[i],0,1,"%.3f",SliderFlags::kAlwaysClamp)||changed;PopID();}
 TextUnformatted("Hex color (Enter to apply)");SetNextItemWidth(width);
 char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",int(std::clamp(rgb[0],0.f,1.f)*255+.5f),int(std::clamp(rgb[1],0.f,1.f)*255+.5f),int(std::clamp(rgb[2],0.f,1.f)*255+.5f));
 if(InputText("##hex",hex,sizeof(hex),InputTextFlags::kEnterReturnsTrue)){
  const char* first=hex+(hex[0]=='#');const char* last=hex+std::strlen(hex);unsigned value=0;
  auto parsed=std::from_chars(first,last,value,16);
  if(last-first==6&&parsed.ec==std::errc{}&&parsed.ptr==last){rgb[0]=((value>>16)&255)/255.f;rgb[1]=((value>>8)&255)/255.f;rgb[2]=(value&255)/255.f;changed=true;}
 }
 PopID();return changed;
}
inline bool Combo(const char* label,int* current,const char* const* labels,int count){
 bool changed=false;if(BeginCombo(label,*current>=0&&*current<count?labels[*current]:"")){
  for(int i=0;i<count;i++)if(Selectable(labels[i],i==*current)){*current=i;changed=true;}EndCombo();
 }return changed;
}
inline void TableSetupColumn(const char* label,int flags=0,float weight=0){dmui::ui::TableSetupColumn(label,static_cast<TableColumnFlags>(flags),weight);}
}

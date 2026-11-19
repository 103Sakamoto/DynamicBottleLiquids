#pragma once
#if defined(DBL_UI_MF)
namespace RE { class InputEvent; }
#include "F4SEMenuFramework.h"
using namespace ImGuiMCP;
namespace ImGui {
using namespace ImGuiMCP;
// Upstream wrapper incorrectly passes col[3] by value. The C export takes float*.
inline bool ColorEdit3(const char* label,float* rgb,ImGuiColorEditFlags flags=0){
 using Fn=bool (*)(const char*,float*,ImGuiColorEditFlags);
 auto fn=reinterpret_cast<Fn>(GetProcAddress(F4SEMenuFramework_GetHostModule(),"igColorEdit3"));
 return fn&&fn(label,rgb,flags);
}
inline ImVec2 GetContentRegionAvail(){ImVec2 v;ImGuiMCP::GetContentRegionAvail(&v);return v;}
}
#elif defined(DBL_UI_DMUI)
#include "DmuiWidgets.h"
#else
#include <imgui.h>
#endif

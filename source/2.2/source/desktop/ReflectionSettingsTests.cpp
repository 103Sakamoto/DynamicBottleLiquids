#include "PCH.h"
#include "Profiles.h"
#include "AppearanceEditorModel.h"
#include <iostream>
using Json=nlohmann::json;
void need(bool ok){if(!ok)throw std::runtime_error("Reflection settings migration mismatch");}
int main(int argc,char** argv){try{
 if(argc!=2)return 2;std::filesystem::path root=argv[1];
 auto user=root/"UserSettings";auto testFile=user/"glass-reflection-test1-tuning.json";
 std::filesystem::remove(testFile); // isolated workspace fixture only
 auto legacy=nuka::LoadTuning(root,true);
 legacy["groups"]["beer"]["reflection_strength"]=2.5;
 legacy["meshes"]["NukaColaBottleFull.nif"]={{"reflection_strength",.25},{"opacity",.37}};
 const auto before=legacy.dump(2);{std::ofstream f(user/"tuning.json");f<<before;}
 auto imported=nuka::LoadTuning(root);
 need(imported["groups"]["beer"]["reflection_strength"]==1);
 need(imported["meshes"]["NukaColaBottleFull.nif"]["reflection_strength"]==1);
 need(imported["meshes"]["NukaColaBottleFull.nif"]["opacity"]==.37);
 auto profiles=nuka::LoadDrinks(root,nuka::LoadSettings(root),imported);
 for(const auto& [name,p]:profiles){need(p.settings.base.surfaceEnvironment.w==1);for(auto& v:p.variants)need(v.settings.base.surfaceEnvironment.w==1);}
 nuka::AppearanceEditorModel editor;editor.Initialize(root,[](auto){});need(editor.Select("NukaColaBottleFull.nif"));
 editor.Edit("reflection_strength",.6);editor.SaveParameter("reflection_strength",0);
 auto reloaded=nuka::LoadTuning(root);need(reloaded["meshes"]["NukaColaBottleFull.nif"]["reflection_strength"]==.6);
 std::ifstream old(user/"tuning.json");std::string after((std::istreambuf_iterator<char>(old)),{});need(after==before);
 std::cout<<"PASS: "<<profiles.size()<<" profiles use shared default; legacy reflection reset only, unrelated settings retained, new override reload and original file unchanged\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

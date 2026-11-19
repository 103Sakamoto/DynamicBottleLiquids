#pragma once
#include "Profiles.h"
#include "UserSettings.h"
#include <functional>
#include <fstream>
#include <Windows.h>
namespace nuka {
// UI-independent model. Callers serialize access; only changed parameters become overrides.
class AppearanceEditorModel {
 using Json=nlohmann::json;
 std::filesystem::path root;
 Json base,draft,saved;
 std::function<void(DrinkMap)> publish;
 Json Defaults()const{auto v=base["defaults"];Json extra={{"shaking_bubbles",0.0},{"surface_foam",0.0},{"wet_absorption",0.0},{"wet_smoothness",.15},{"wet_refraction",.8},{"wet_drying",3.5},{"reflection_strength",1.0}};for(auto& [key,value]:extra.items())if(!v.contains(key))v[key]=value;return v;}
 static std::string Lower(std::string v){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::tolower(c));});return v;}
 void Commit(const Json& next,const Json& live){
  ValidateTuning(next);ValidateTuning(live);
  auto profiles=LoadDrinks(root,LoadSettings(root),live);
  auto temporary=UserSettingsRoot(root)/"glass-reflection-test1-tuning.json.tmp";{std::ofstream out(temporary);out<<next.dump(2);out.flush();if(!out)throw std::runtime_error("Could not write glass-reflection-test1-tuning.json");}
  if(!MoveFileExW(temporary.c_str(),(UserSettingsRoot(root)/"glass-reflection-test1-tuning.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not replace glass-reflection-test1-tuning.json");
  saved=next;draft=live;if(publish)publish(std::move(profiles));
 }
public:
 std::string mesh;
 void Initialize(const std::filesystem::path& path,std::function<void(DrinkMap)> callback){root=path;publish=std::move(callback);base=AppearanceControls(root);draft=saved=LoadTuning(root);}
 const Json& Document()const{return draft;}
 const Json& Catalog()const{return base["meshes"];}
 bool Select(std::string name){for(auto it=base["meshes"].begin();it!=base["meshes"].end();++it)if(Lower(it.key())==Lower(name)){mesh=it.key();return true;}mesh.clear();return false;}
 std::string Group()const{return mesh.empty()?std::string{}:TuningGroup(draft,mesh);}
 Json Values()const{auto v=Defaults();if(base["meshes"].contains(mesh))v.update(base["meshes"][mesh]);auto group=Group();if(!group.empty())v.update(draft["groups"][group]);if(draft["meshes"].contains(mesh))v.update(draft["meshes"][mesh]);return v;}
 void Edit(const char* key,const Json& value){if(mesh.empty())throw std::runtime_error("Pick up a supported bottle first");auto next=draft;next["meshes"][mesh][key]=value;ValidateTuning(next);draft=std::move(next);}
 void Publish(){auto profiles=LoadDrinks(root,LoadSettings(root),draft);if(publish)publish(std::move(profiles));}
 void SaveParameter(const char* key,int action){
  if(mesh.empty())throw std::runtime_error("Pick up a supported bottle first");auto next=saved,live=draft;auto value=Values().at(key);auto group=Group();
  if(action==0)next["meshes"][mesh][key]=value;
  else if(action==1){if(group.empty())throw std::runtime_error("Assign a group first");for(auto* doc:{&next,&live}){(*doc)["groups"][group][key]=value;for(auto it=(*doc)["meshes"].begin();it!=(*doc)["meshes"].end();++it)if(TuningGroup(*doc,it.key())==group)it.value().erase(key);}}
  else if(action==2){if(group.empty())throw std::runtime_error("Assign a group first");next["meshes"][mesh].erase(key);live["meshes"][mesh].erase(key);}
  else throw std::runtime_error("Unknown save action");Commit(next,live);
 }
 void CreateGroup(const std::string& name){auto next=saved,live=draft;AddTuningGroup(next,name,Json::object());AddTuningGroup(live,name,Json::object());Commit(next,live);}
 void DeleteGroup(const std::string& name){auto next=saved,live=draft;for(auto* doc:{&next,&live}){for(auto it=base["meshes"].begin();it!=base["meshes"].end();++it)if(TuningGroup(*doc,it.key())==name)SetTuningMembership(*doc,it.key(),"");RemoveTuningGroup(*doc,name);}Commit(next,live);}
 void AssignGroup(const std::string& name){if(mesh.empty())throw std::runtime_error("Pick up a supported bottle first");auto next=saved,live=draft;SetTuningMembership(next,mesh,name);SetTuningMembership(live,mesh,name);Commit(next,live);}
 void SaveGroupParameter(const std::string& name,const char* key,const Json& value){auto next=saved,live=draft;for(auto* doc:{&next,&live})(*doc)["groups"].at(name)[key]=value;Commit(next,live);}
 Json GroupValues(const std::string& name)const{auto v=Defaults();v.update(draft["groups"].at(name));return v;}
 void Reload(){auto next=LoadTuning(root);auto profiles=LoadDrinks(root,LoadSettings(root),next);draft=saved=next;if(publish)publish(std::move(profiles));}
};
}

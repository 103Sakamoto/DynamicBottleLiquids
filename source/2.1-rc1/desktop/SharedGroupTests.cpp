#include "PCH.h"
#include "Profiles.h"
#include "AppearanceEditorModel.h"
#include <iostream>
using Json=nlohmann::json;
using namespace nuka;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json read(const std::filesystem::path& path){std::ifstream input(path);return Json::parse(input);}
void write(const std::filesystem::path& path,const Json& value){std::filesystem::create_directories(path.parent_path());std::ofstream out(path);out<<value.dump(2);}
std::string bytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){try{
 check(argc==2,"Expected isolated fixture path");auto root=std::filesystem::absolute(argv[1]);auto settings=UserSettingsRoot(root)/"tuning.json";
 std::filesystem::remove(settings);
 std::filesystem::remove(UserSettingsRoot(root)/"glass-reflection-test1-tuning.json");
 std::filesystem::remove(root/"groups.d/second.groups.json");
 auto patchPath=root/"groups.d/shared-test.groups.json";auto patch=read(patchPath);
 auto group=patch["memberships"]["First.nif"].get<std::string>();
 patch["groups"][group]["opacity"]=.3;patch["groups"][group]["reflection_strength"]=2.5;write(patchPath,patch);
 auto t=LoadTuning(root);check(TuningGroup(t,"FIRST.NIF")==group,"Imported membership not resolved");
 check(t["groups"][group]["reflection_strength"]==2.5,"Imported group default absent");
 auto resolved=LoadDrinks(root,LoadSettings(root),t);check(resolved.size()==2,"Exported geometry did not load");
 for(const auto& [name,p]:resolved){check(p.resolvedControls["reflection_strength"]==2.5,"Renderer group value differs");check(p.resolvedControls["opacity"]==(p.meshName=="First.nif"?.4:.3),"Author bottle override precedence failed");}
 check(!std::filesystem::exists(settings),"Loading patch created player tuning");
 // Existing schema 1 preferences must remain byte-for-byte unchanged on load.
 Json legacy={{"schema_version",1},{"groups",{{"cola",{{"opacity",.77}}}}},{"meshes",{{"FIRST.NIF",{{"opacity",.91}}}}}};
 write(settings,legacy);auto before=bytes(settings);t=LoadTuning(root);
 check(bytes(settings)==before&&t["groups"]["cola"]["opacity"]==.77,"Legacy preferences changed");
 check(t["meshes"]["First.nif"]["opacity"]==.91,"Player bottle override lost");
 // Existing authoritative schema 2 memberships, ungrouping, empty overrides and
 // deleted built-in groups remain authoritative even when adding a new pack.
 Json custom={{"schema_version",2},{"groups",{{"My drinks",{{"opacity",.8}}}}},{"meshes",{{"First.nif",Json::object()}}},{"memberships",{{"FIRST.NIF","My drinks"},{"second.nif",""}}}};
 write(settings,custom);before=bytes(settings);t=LoadTuning(root);
 check(bytes(settings)==before&&!t["groups"].contains("cola"),"Deleted built-in returned");
 check(TuningGroup(t,"First.nif")=="My drinks"&&TuningGroup(t,"Second.nif").empty(),"Player memberships replaced");
 check(t["meshes"]["First.nif"].empty(),"Explicit inherit was replaced by authored override");
 // Exercise actual editor save-to-group/apply-from-group and restart paths.
 write(settings,legacy);AppearanceEditorModel editor;editor.Initialize(root,[](DrinkMap){});
 check(editor.Select("First.nif"),"Editor cannot find exported bottle");editor.Edit("opacity",.61);editor.SaveParameter("opacity",1);
 check(editor.Select("Second.nif")&&editor.Values()["opacity"]==.61,"Shared edit did not reach second bottle");
 settings=UserSettingsRoot(root)/"glass-reflection-test1-tuning.json";before=bytes(settings);editor.Reload();check(bytes(settings)==before&&editor.Values()["opacity"]==.61,"Shared edit did not survive restart");
 editor.Select("First.nif");editor.Edit("opacity",.12);editor.SaveParameter("opacity",0);editor.SaveParameter("opacity",2);editor.Reload();check(editor.Values()["opacity"]==.61,"Apply from group did not persist");
 editor.DeleteGroup(group);before=bytes(settings);editor.Reload();
 check(bytes(settings)==before&&!editor.Document()["groups"].contains(group)&&editor.Group().empty(),"Deleted patch group returned");
 // A genuinely new group installed later is not suppressed by old history.
 Json additional={{"schema_version",1},{"groups",{{"Another pack",{{"opacity",.2}}}}},{"memberships",{{"Third.nif","Another pack"}}},{"meshes",Json::object()}};
 write(root/"groups.d/second.groups.json",additional);t=LoadTuning(root);check(t["groups"].contains("Another pack"),"New pack hidden by old catalog");check(bytes(settings)==before,"New pack changed player file");
 // Saved settings also survive reinstalling a group file and updating defaults.
 auto saved=read(settings);saved["groups"][group]={{"opacity",.45}};write(settings,saved);
 patch["groups"][group]["opacity"]=.05;write(patchPath,patch);before=bytes(settings);t=LoadTuning(root);
 check(t["groups"][group]["opacity"]==.45&&bytes(settings)==before,"Updated patch replaced player group");
 // Current reflection settings outrank legacy tuning, without changing either file.
 saved=read(settings);saved["groups"][group]["reflection_strength"]=.73;write(settings,saved);
 before=bytes(settings);auto legacyBytes=bytes(UserSettingsRoot(root)/"tuning.json");
 t=LoadTuning(root);check(t["groups"][group]["reflection_strength"]==.73,"Current reflection override replaced");
 check(bytes(settings)==before&&bytes(UserSettingsRoot(root)/"tuning.json")==legacyBytes,"Loading rewrote settings");
 auto factory=LoadTuning(root,true);check(factory["groups"][group]["reflection_strength"]==2.5,"Factory reset lacks author group defaults");
 check(bytes(settings)==before,"Factory load rewrote settings");
 // Duplicate/invalid catalogs fail without touching saved preferences.
 additional["groups"]=patch["groups"];write(root/"groups.d/second.groups.json",additional);bool rejected=false;
 try{LoadTuning(root);}catch(...){rejected=true;}check(rejected&&bytes(settings)==before,"Invalid patch changed player data");
 std::cout<<"PASS: actual patcher export, renderer precedence, legacy settings, memberships, ungrouping, empty overrides, editor save/apply, restart, deletion, new packs, updates, invalid files; player bytes preserved\n";
 return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

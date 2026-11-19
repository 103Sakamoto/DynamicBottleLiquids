#pragma once
#include <Windows.h>
#ifndef DBL_ENGINE_FREE
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#endif
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
namespace nuka {
// Copy legacy preferences only once. Never replace existing external preferences.
inline void MigrateUserSettings(const std::filesystem::path& assets,const std::filesystem::path& destination,const std::filesystem::path& previous={}){
 static std::mutex migrationMutex;std::lock_guard lock(migrationMutex);
 std::filesystem::create_directories(destination);
 auto marker=destination/"migration-v1.complete";
 if(std::filesystem::exists(marker))return;
 for(auto name:{"tuning.json","ui.json","DynamicBottleLiquids.ini"}){
  auto source=std::string(name)=="DynamicBottleLiquids.ini"?assets.parent_path()/name:assets/name;
  if(!previous.empty()&&std::filesystem::exists(previous/name))source=previous/name;
  auto target=destination/name;
  if(!std::filesystem::exists(target)&&std::filesystem::exists(source)){
   auto temporary=target;temporary+=".migration.tmp";
   std::filesystem::copy_file(source,temporary,std::filesystem::copy_options::overwrite_existing);
   // No replacement: a concurrently saved preference always wins.
   if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)){
    if(!std::filesystem::exists(target))throw std::runtime_error("Could not migrate DBL preferences");
    std::filesystem::remove(temporary);
   }
  }
 }
 std::ofstream out(marker);out<<"Legacy preferences imported. Delete this marker to retry import of missing files.\n";out.flush();
 if(!out)throw std::runtime_error("Could not finish DBL preferences migration");
}
inline std::filesystem::path UserSettingsRoot(const std::filesystem::path& assets){
#ifdef DBL_ENGINE_FREE
 // Test fixtures are isolated; never read or write the player's settings.
 return assets;
#else
 auto destination=std::filesystem::absolute(assets/"UserSettings");
 // Read the short-lived AppData location only to preserve preferences from that build.
 std::filesystem::path previous;wchar_t folder[MAX_PATH]{};
 if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,folder))){
#ifdef DBL_FALLOUT_VR
  previous=std::filesystem::path(folder)/"DynamicBottleLiquids"/"Fallout4VR";
#else
  previous=std::filesystem::path(folder)/"DynamicBottleLiquids"/"Fallout4";
#endif
 }
 MigrateUserSettings(assets,destination,previous);return destination;
#endif
}
inline std::filesystem::path UserLoggingPath(const std::filesystem::path& assets){
#ifdef DBL_ENGINE_FREE
 return std::filesystem::absolute(assets.parent_path()/"DynamicBottleLiquids.ini");
#else
 return UserSettingsRoot(assets)/"DynamicBottleLiquids.ini";
#endif
}
inline std::filesystem::path ReadLoggingPath(const std::filesystem::path& assets){
 auto path=UserLoggingPath(assets);return std::filesystem::exists(path)?path:std::filesystem::absolute(assets.parent_path()/"DynamicBottleLiquids.ini");
}
}

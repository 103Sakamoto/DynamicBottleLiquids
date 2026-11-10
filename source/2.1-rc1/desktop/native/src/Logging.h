#pragma once
#include <Windows.h>
#include "UserSettings.h"
#include <filesystem>
#include <string>
#include <algorithm>
#include <cwctype>
#include <chrono>
#include <stdexcept>
namespace nuka {
#ifdef DBL_DEVELOPMENT_BUILD
inline constexpr bool DefaultExtensiveLogging=true;
#else
inline constexpr bool DefaultExtensiveLogging=false;
#endif
inline bool extensiveLogging=false; // Set once at startup, before render hooks.
inline auto ProfileNow(){return extensiveLogging?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};}
inline bool ConfigureLogging(const std::filesystem::path& root){
 wchar_t value[64]{};
 auto path=ReadLoggingPath(root);
 GetPrivateProfileStringW(L"Logging",L"Mode",DefaultExtensiveLogging?L"Extensive":L"Basic",value,64,path.c_str());
 std::wstring mode(value);
 auto first=mode.find_first_not_of(L" \t");
 mode=first==std::wstring::npos?L"":mode.substr(first,mode.find_last_not_of(L" \t")-first+1);
 std::transform(mode.begin(),mode.end(),mode.begin(),[](wchar_t c){return std::towlower(c);});
 extensiveLogging=mode==L"extensive"?true:mode==L"basic"?false:DefaultExtensiveLogging;
 return mode==L"extensive"||mode==L"basic";
}
// Saving a requested mode never mutates render-thread flags mid-frame. The active
// mode stays fixed until the next startup; UI must compare saved and active modes.
inline bool SavedExtensiveLogging(const std::filesystem::path& root){
    wchar_t value[64]{};auto path=ReadLoggingPath(root);
    GetPrivateProfileStringW(L"Logging",L"Mode",DefaultExtensiveLogging?L"Extensive":L"Basic",value,64,path.c_str());
    std::wstring mode(value);auto first=mode.find_first_not_of(L" \t");
    mode=first==std::wstring::npos?L"":mode.substr(first,mode.find_last_not_of(L" \t")-first+1);
    std::transform(mode.begin(),mode.end(),mode.begin(),[](wchar_t c){return std::towlower(c);});
    return mode==L"extensive"?true:mode==L"basic"?false:DefaultExtensiveLogging;
}
inline void SaveLoggingMode(const std::filesystem::path& root,bool extensive){
    auto path=UserLoggingPath(root);
    if(!WritePrivateProfileStringW(L"Logging",L"Mode",extensive?L"Extensive":L"Basic",path.c_str()))
        throw std::runtime_error("Could not save logging mode");
}
inline bool LoggingRestartRequired(const std::filesystem::path& root){return SavedExtensiveLogging(root)!=extensiveLogging;}

}

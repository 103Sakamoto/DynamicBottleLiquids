// Compiled into every plugin that links the Fusion DLLs (fusionframework_use()), never into a DLL's exports.
#include "fusionframework/Game.h"
#include "fusionframework/Require.h"

#include <delayimp.h>

#include <atomic>
#include <filesystem>
#include <format>

namespace fusionframework::require {
namespace {
std::filesystem::path OwnDirectory()
{
    HMODULE self{};
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&OwnDirectory), &self);
    wchar_t buffer[MAX_PATH]{};
    const auto length = ::GetModuleFileNameW(self, buffer, MAX_PATH);
    return std::filesystem::path(buffer, buffer + length).parent_path();
}

bool IsFusionDll(const char* name)
{
    return _stricmp(name, "FusionFramework.dll") == 0 || _stricmp(name, "DBLFusionUI.dll") == 0;
}

HMODULE Load(const char* file)
{
    if (auto* loaded = ::GetModuleHandleA(file)) return loaded;
    return ::LoadLibraryW((OwnDirectory() / file).c_str());
}

std::string Format(std::uint32_t version)
{
    return std::format("{}.{}.{}", version >> 20, (version >> 10) & 0x3FF, version & 0x3FF);
}

std::string& Notice()
{
    static std::string notice;
    return notice;
}

void ShowNotice(F4SE::MessagingInterface::Message* message)
{
    static std::atomic_bool shown;
    if (message->type != F4SE::MessagingInterface::kPostLoadGame && message->type != F4SE::MessagingInterface::kNewGame) return;
    if (shown.exchange(true)) return;
    RE::SendHUDMessage::ShowHUDMessage(Notice().c_str(), nullptr, false, true);
}

// Data/F4SE/Plugins is not on the loader's search path, so delay-loaded Fusion DLLs resolve from beside the plugin.
FARPROC WINAPI DelayLoadHook(unsigned notify, PDelayLoadInfo info)
{
    if (notify != dliNotePreLoadLibrary || !info || !IsFusionDll(info->szDll)) return nullptr;
    return reinterpret_cast<FARPROC>(Load(info->szDll));
}
}

std::string Check(std::string_view plugin, std::initializer_list<Dll> dlls)
{
    std::string notice;
    for (const auto& dll : dlls) {
        std::string problem;
        if (auto* module = Load(dll.file); !module) {
            problem = std::format("{} needs {} {} or newer, which is not installed.", plugin, dll.download, Format(dll.version));
        } else if (auto* version = reinterpret_cast<std::uint32_t (*)()>(::GetProcAddress(module, dll.versionExport)); !version) {
            problem = std::format("{} needs {} {} or newer; update {}.", plugin, dll.download, Format(dll.version), dll.download);
        } else if (const auto installed = version(); (installed >> 20) > (dll.version >> 20)) {
            problem = std::format("{} was built for {} {}, but {} is installed; update {}.", plugin, dll.download,
                                  Format(dll.version), Format(installed), plugin);
        } else if (installed < dll.version) {
            problem = std::format("{} needs {} {} or newer, but {} is installed; update {}.", plugin, dll.download,
                                  Format(dll.version), Format(installed), dll.download);
        }
        if (problem.empty()) continue;
        spdlog::error("{}", problem);
        if (!notice.empty()) notice += ' ';
        notice += problem;
    }
    if (!notice.empty()) {
        Notice() = notice;
        if (const auto* messaging = F4SE::GetMessagingInterface()) messaging->RegisterListener(ShowNotice);
    }
    return notice;
}
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = fusionframework::require::DelayLoadHook;

#include "PCH.h"
#include "Tuning.h"
#include <fusionui/FusionUI.h>
#include <fusionui/Require.h>
#include <atomic>
#include <future>

namespace nuka {
namespace {
class Editor final : public fusionui::Client {
    void Draw(const fusionui::FrameContext&) override {
        if (TuningVisible()) DrawTuning();
    }
    void OnFocusChanged(bool focused) override {
        if (!focused) CloseTuning();
    }
} editor;
class Overlay final : public fusionui::Client {
    void Draw(const fusionui::FrameContext&) override {
        if (!TuningVisible() && TuningWantsDrawing()) DrawTuning();
    }
} overlayClient;
std::atomic_bool registered=false;
}

void InitializeTuningRenderer(IDXGISwapChain*) {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto notice=fusionframework::require::Check("Dynamic Bottle Liquids",
            {fusionframework::require::kFramework, fusionui::kRequire});
        if (!notice.empty()) { REX::ERROR("DBL editor unavailable: {}",notice); return; }
        fusionui::ClientOptions options;
        options.name="DBL Appearance";
        options.modal=true;
        options.pausesGame=true;
        options.capturesCharacters=true;
        if (!fusionui::Register(editor, options)) return;
        options.name="DBL Performance";
        options.modal=false;
        options.pausesGame=false;
        options.capturesCharacters=false;
        options.padPointer=false;
        registered=fusionui::Register(overlayClient, options);
        REX::INFO("DBL Fusion UI clients registered: {}",registered.load());
    });
}

// Called only by the tuning worker. Fusion owns the cursor, controls and pause menu.
// Confirm opening on the game thread: Open() off-thread only confirms queuing.
bool FusionPreviewSession(bool open) {
    if (!registered) return false;
    auto* tasks=F4SE::GetTaskInterface();
    if (!tasks) return false;
    auto result=std::make_shared<std::promise<bool>>();
    auto cancelled=std::make_shared<std::atomic_bool>(false);
    auto future=result->get_future();
    tasks->AddTask([open,result,cancelled] {
        if (cancelled->load()) { result->set_value(false); return; }
        if (open) result->set_value(fusionui::Open(editor));
        else { fusionui::Close(editor); result->set_value(true); }
    });
    if (future.wait_for(std::chrono::seconds(2))==std::future_status::ready)
        return future.get();
    *cancelled=true;
    fusionui::Close(editor);
    REX::WARN("DBL Fusion editor request timed out; close queued");
    return false;
}

void TickFusionUI() {
    if (!registered || !fusionui::Available()) return;
    DWORD foregroundProcess=0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
    const bool focused=foregroundProcess==GetCurrentProcessId();
    PollTuningHotkeys(focused);
    const bool showOverlay=focused && TuningWantsDrawing() && !TuningVisible();
    static bool overlayRequested=false;
    if (showOverlay!=overlayRequested) {
        if (showOverlay) overlayRequested=fusionui::Open(overlayClient);
        else { fusionui::Close(overlayClient); overlayRequested=false; }
    }
}
}

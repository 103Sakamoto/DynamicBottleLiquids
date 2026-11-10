#pragma once
#include "Profiles.h"
#include <functional>
#include <cmath>
struct IDXGISwapChain;
namespace nuka {
bool TuningSmokeTest(const std::filesystem::path& root);
void TuningTarget(std::string_view mesh);
bool TuningVisible();
void StartTuning(const std::filesystem::path& root,std::function<void(DrinkMap)> publish,std::function<bool(bool)> previewSession);
void InitializeTuningRenderer(IDXGISwapChain*);
void PollTuningHotkeys(bool focused);
bool TuningWantsDrawing();
void DrawTuning(bool hostPanel=false);
void CloseTuning();
}

namespace nuka { bool FusionPreviewSession(bool open); void TickFusionUI(); }



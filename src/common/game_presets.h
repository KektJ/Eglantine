// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "common/settings.h"

namespace Settings {

inline constexpr u64 MarioKartWorldProgramId = 0x0400C3F00006E000ULL;
inline constexpr u64 FortunesWeaveProgramId = 0x04007EF01F8AE000ULL;

inline void SetMarioKartWorldRecommendedSettings(Values& settings, bool global) {
    const auto session = [global](auto& setting, auto value) {
        setting.SetGlobal(global);
        setting.SetValue(value);
    };
    session(settings.renderer_backend, RendererBackend::Vulkan);
    session(settings.resolution_setup, ResolutionSetup::Res1X);
    session(settings.gpu_accuracy, GpuAccuracy::Low);
    session(settings.use_asynchronous_shaders, true);
    session(settings.astc_recompression, AstcRecompression::Bc3);
    session(settings.vram_usage_mode, VramUsageMode::Conservative);
    session(settings.use_reactive_flushing, false);
    session(settings.vsync_mode, VSyncMode::FifoRelaxed);
    session(settings.use_docked_mode, ConsoleMode::Handheld);
    session(settings.scaling_filter, ScalingFilter::ScaleForce);
    session(settings.anti_aliasing, AntiAliasing::Fxaa);
}

inline bool ApplyMarioKartWorldPreset(Values& settings, u64 program_id, bool accepted) {
    if (!accepted || program_id != MarioKartWorldProgramId) {
        return false;
    }
    SetMarioKartWorldRecommendedSettings(settings, false);
    return true;
}

/// Fortune's Weave's recommended values for this fork's settings, applied for the session when it
/// boots (yuzu/main_window.cpp). Only settings the user hasn't chosen are changed: no per-game
/// value, and the global value still at its default. Returns the names of the settings it set.
inline std::vector<const char*> ApplyFortunesWeaveRecommendedSettings(Values& settings,
                                                                      u64 program_id) {
    std::vector<const char*> applied;
    if (program_id != FortunesWeaveProgramId ||
        !settings.fortunes_weave_recommended_settings.GetValue()) {
        return applied;
    }
    const auto recommend = [&applied](auto& setting, auto value, const char* name) {
        if (!setting.UsingGlobal() || setting.GetValue(true) != setting.GetDefault()) {
            return; // The user chose a value, per game or globally
        }
        setting.SetGlobal(false);
        setting.SetValue(value);
        applied.push_back(name);
    };
    recommend(settings.gpu_accuracy, GpuAccuracy::Low, "gpu_accuracy");
    // Native graphics pipelines are translated on worker threads only with this on.
    recommend(settings.use_asynchronous_shaders, true, "use_asynchronous_shaders");
    recommend(settings.texture_array_limit, TextureArrayLimit::Limit_64, "texture_array_limit");
    recommend(settings.frames_ahead_mode, FramesAheadMode::Relaxed, "frames_ahead_mode");
    recommend(settings.uniform_inplace_compare, true, "uniform_inplace_compare");
    recommend(settings.handle_pointer_cache, true, "handle_pointer_cache");
    return applied;
}

} // namespace Settings

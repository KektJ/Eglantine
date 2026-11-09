// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <vector>

#include "common/common_types.h"

// Optional edits of Fire Emblem: Fortune's Weave's own settings files (MessagePack), made on the
// player's RomFS when the game loads (file_sys/patch_manager.cpp). Each returns the edited file, or
// nothing when the file doesn't parse, has no such setting, or needs no change.
namespace FileSys::FortunesWeave {

/// Graphics/Settings/**/*.set: SceneShadow.MaxCascadeCount above `cascades` becomes `cascades`.
/// With `add_if_missing` (Preset/Base.set) the key is added where SceneShadow doesn't set it.
std::optional<std::vector<u8>> LimitShadowCascades(std::span<const u8> file, int cascades,
                                                   bool add_if_missing);

/// Graphics/Settings/Preset/lodPresets.lod: crowd characters (CharacterMob, CharacterLegion) switch
/// to their simpler models at twice the screen size, and hide below 5 % of the screen (was 2 %)
/// from 25 m away (was 50 m).
std::optional<std::vector<u8>> LighterCrowdLod(std::span<const u8> file);

} // namespace FileSys::FortunesWeave

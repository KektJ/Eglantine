// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Kernel {
class KProcess;
}

// EDEN_NX2_NVN_TRACE=1: counts every NVN call the game makes, without changing what it does.
//
// The game gets all of NVN through nvnBootstrapLoader (sdk export): it returns
// nvnDeviceGetProcAddress, and that returns every other entry point. A small code area after the
// game's last module holds stubs: the loader's first instruction jumps to a wrapper that runs the
// real loader and asks the host (svc 0x4E01 / 0x4E02) to replace the pointer it returns with a stub.
// The stubs live in a plain read-write page mapping (the JIT runs code from any mapped page; the
// game's dynamic linker only looks at code regions), reached through a far jump in the padding at
// the end of the sdk's text segment.
// Each NVN entry point's stub adds one to a counter next to it in game memory and jumps on to the
// real function, so a call costs four instructions more and never leaves the JIT. The host reads
// the counters every 600 presents and logs the calls per frame by name.
namespace Kernel::NvnTrace {

/// Whether the trace is on (EDEN_NX2_NVN_TRACE set).
bool Enabled();

/// EDEN_NX2_NVN_TRACE=bench: before the first NVN lookup, game code times 200,000 host round
/// trips (an svc that does nothing), 200,000 32-byte records into game memory and 200,000 empty
/// loop steps, and the log gets the cost of each.
bool Bench();

/// Size of the stub area.
constexpr u64 StubAreaSize = 0x20000;

/// Hooks nvnBootstrapLoader in the module loaded at `sdk_base`. [`pad_start`, `pad_end`) is the
/// unused, executable end of that module's text segment (its last page past the code). Does
/// nothing if the loader isn't found, doesn't look like the version this was written for, or
/// there's no room.
void Install(Core::System& system, KProcess& process, u64 sdk_base, u64 pad_start, u64 pad_end);

/// svc 0x4E01 / 0x4E02 from the stubs: x0 is the pointer the real function returned, x1 the
/// name that was looked up. Replaces x0 with the stub that stands in for it.
bool HandleSvc(Core::System& system, u32 imm, std::array<u64, 8>& args);

/// Once per presented frame (nx2:vi2): logs the counts every 600 frames.
void OnPresent(Core::System& system, u64 presents);

} // namespace Kernel::NvnTrace

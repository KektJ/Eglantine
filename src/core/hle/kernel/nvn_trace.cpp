// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "common/alignment.h"
#include "common/logging.h"
#include "core/arm/symbols.h"
#include "core/core.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/nvn_trace.h"
#include "core/memory.h"

namespace Kernel::NvnTrace {
namespace {

constexpr u32 SvcLoaderResult = 0x4E01;
constexpr u32 SvcProcResult = 0x4E02;
constexpr u32 SvcBenchNothing = 0x4E03;
constexpr u32 SvcBenchStart = 0x4E05;
constexpr u32 SvcBenchRoundTrips = 0x4E06;
constexpr u32 SvcBenchRecords = 0x4E07;
constexpr u32 SvcBenchEmpty = 0x4E08;
constexpr u64 BenchSteps = 200'000;

// Bench (llvm-mc): pre runs bench once (a flag in the stub area), then goes on to the loader
// wrapper (literal at +0x18), keeping the caller's x30 in x17. bench: svc 0x4E05; 200,000 x
// svc 0x4E03; svc 0x4E06; 200,000 x two stp of x0-x3 to a ring; svc 0x4E07; 200,000 empty loop
// steps; svc 0x4E08. It changes only x9 and x10.
constexpr std::array<u32, 6> BenchPreCode{0xAA1E03F1, 0x94000007, 0xAA1103FE,
                                          0x58000070, 0xD61F0200, 0xD503201F};
constexpr std::array<u32, 26> BenchCode{
    0x10000349, 0xF940012A, 0xB50002EA, 0xD280002A, 0xF900012A, 0xD409C0A1, 0xD281A809,
    0xF2A00069, 0xD409C061, 0xF1000529, 0x54FFFFC1, 0xD409C0C1, 0xD281A809, 0xF2A00069,
    0x100001CA, 0xA9000540, 0xA9010D42, 0xF1000529, 0x54FFFFA1, 0xD409C0E1, 0xD281A809,
    0xF2A00069, 0xF1000529, 0x54FFFFE1, 0xD409C101, 0xD65F03C0};
constexpr u64 BenchSize = 0xB0; // literal at 0x18, bench at 0x20, flag at 0x88, ring at 0x90

// The stubs (AArch64, as llvm-mc assembles them). x16 and x17 may be clobbered at any call.
//   wrapper: stp x29,x30,[sp,#-32]!; mov x29,sp; str x<name>,[sp,#16]; ldr x16,=target;
//            blr x16; ldr x1,[sp,#16]; svc #imm; ldp x29,x30,[sp],#32; ret; nop; .quad target
constexpr std::array<u32, 10> WrapperCode(u32 store_name, u32 svc) {
    return {0xA9BE7BFD, 0x910003FD, store_name, 0x580000F0, 0xD63F0200,
            0xF9400BE1, svc,        0xA8C27BFD, 0xD65F03C0, 0xD503201F};
}
constexpr u32 StoreX0 = 0xF9000BE0; // str x0, [sp, #16]
constexpr u32 StoreX1 = 0xF9000BE1; // str x1, [sp, #16]
constexpr u32 SvcInsn(u32 imm) {
    return 0xD4000001 | (imm << 5);
}
constexpr u64 WrapperSize = 48; // 10 instructions, the target at +40
//   counter: adr x16,counter; ldr x17,[x16]; add x17,x17,#1; str x17,[x16]; ldr x16,=target;
//            br x16; .quad target; .quad counter
constexpr std::array<u32, 6> CounterCode{0x10000110, 0xF9400211, 0x91000631,
                                         0xF9000211, 0x58000050, 0xD61F0200};
constexpr u64 CounterSize = 40; // target at +24, count at +32
constexpr u64 CountOffset = 32;

//   far jump: ldr x16,=target; br x16; .quad target
constexpr std::array<u32, 2> FarJumpCode{0x58000050, 0xD61F0200};
constexpr u64 FarJumpSize = 16;

constexpr u32 Pacibsp = 0xD503237F;
constexpr u32 FramePush = 0xA9BE7BFD; // stp x29, x30, [sp, #-32]!

struct Entry {
    std::string names;
    u64 count_addr{};
    u64 last{};
};

struct State {
    std::mutex mutex;
    bool installed{};
    u64 next{};
    u64 end{};
    std::unordered_map<u64, u64> stub_of; ///< real function -> its stub
    std::unordered_map<u64, size_t> entry_of; ///< real function -> entries index
    std::vector<Entry> entries;
    u64 last_presents{};
    std::chrono::steady_clock::time_point bench_mark{};
    double bench_ns[3]{};
};

State& GetState() {
    static State state;
    return state;
}

void WriteWords(Core::Memory::Memory& memory, u64 addr, std::span<const u32> words) {
    for (size_t i = 0; i < words.size(); ++i) {
        memory.Write32(addr + 4 * i, words[i]);
    }
}

/// A new stub area slot of `size` bytes, or 0 when the area is full.
u64 Allocate(State& s, u64 size) {
    if (s.next + size > s.end) {
        return 0;
    }
    const u64 addr = s.next;
    s.next += size;
    return addr;
}

u64 MakeWrapper(State& s, Core::Memory::Memory& memory, u64 target, u32 store_name, u32 svc) {
    const u64 addr = Allocate(s, WrapperSize);
    if (addr == 0) {
        return 0;
    }
    WriteWords(memory, addr, WrapperCode(store_name, svc));
    memory.Write64(addr + 40, target);
    return addr;
}

/// The stub that stands in for `real` (made the first time it's seen), named `name`.
u64 StubFor(State& s, Core::Memory::Memory& memory, u64 real, const std::string& name) {
    if (const auto it = s.stub_of.find(real); it != s.stub_of.end()) {
        auto& entry = s.entries[s.entry_of[real]];
        if (entry.names.find(name) == std::string::npos) {
            entry.names += "/" + name;
        }
        return it->second;
    }
    // GetProcAddress itself hands out the rest, so its stand-in wraps it instead of counting.
    const bool lookup = name == "nvnDeviceGetProcAddress";
    u64 stub = 0;
    if (lookup) {
        stub = MakeWrapper(s, memory, real, StoreX1, SvcInsn(SvcProcResult));
    } else if ((stub = Allocate(s, CounterSize)) != 0) {
        WriteWords(memory, stub, CounterCode);
        memory.Write64(stub + 24, real);
        memory.Write64(stub + CountOffset, 0);
    }
    if (stub == 0) {
        LOG_WARNING(Kernel, "NVN trace: stub area full, {} is not counted", name);
        return real;
    }
    s.stub_of.emplace(real, stub);
    if (!lookup) {
        s.entry_of.emplace(real, s.entries.size());
        s.entries.push_back({name, stub + CountOffset, 0});
    }
    return stub;
}

} // namespace

bool Enabled() {
    static const bool enabled = std::getenv("EDEN_NX2_NVN_TRACE") != nullptr;
    return enabled;
}

bool Bench() {
    static const bool bench = [] {
        const char* text = std::getenv("EDEN_NX2_NVN_TRACE");
        return text != nullptr && std::strcmp(text, "bench") == 0;
    }();
    return bench;
}

void Install(Core::System& system, KProcess& process, u64 sdk_base, u64 pad_start, u64 pad_end) {
    auto& memory = process.GetMemory();
    const auto symbols = Core::Symbols::GetSymbols(sdk_base, memory);
    const auto it = symbols.find("nvnBootstrapLoader");
    if (it == symbols.end()) {
        LOG_WARNING(Kernel, "NVN trace: nvnBootstrapLoader not found in the sdk module");
        return;
    }
    // The symbol values are offsets in the module.
    const u64 loader = sdk_base + it->second.first;
    const u64 jump = Common::AlignUp(pad_start, 16);
    if (jump + FarJumpSize > pad_end || memory.Read64(jump) != 0 || memory.Read64(jump + 8) != 0) {
        LOG_WARNING(Kernel, "NVN trace: no free room at the end of the sdk's text: not hooked");
        return;
    }
    KProcessAddress stubs{};
    if (R_FAILED(process.GetPageTable().MapPages(std::addressof(stubs), StubAreaSize / PageSize,
                                                 KMemoryState::Stack,
                                                 KMemoryPermission::UserReadWrite))) {
        LOG_WARNING(Kernel, "NVN trace: no memory for the stubs: not hooked");
        return;
    }
    const u64 stub_base = GetInteger(stubs);
    if (memory.Read32(loader) != Pacibsp || memory.Read32(loader + 4) != FramePush) {
        LOG_WARNING(Kernel, "NVN trace: nvnBootstrapLoader at {:#x} starts {:08x} {:08x}, not the "
                    "expected code: not hooked", loader, memory.Read32(loader),
                    memory.Read32(loader + 4));
        return;
    }
    auto& s = GetState();
    std::scoped_lock lk{s.mutex};
    s.stub_of.clear();
    s.entry_of.clear();
    s.entries.clear();
    s.last_presents = 0;
    s.next = stub_base;
    s.end = stub_base + StubAreaSize;
    // The loader's pacibsp (a hint) becomes a branch to a far jump in the text padding, on to the
    // wrapper, which runs the loader from its second instruction on (position independent).
    const u64 wrapper = MakeWrapper(s, memory, loader + 4, StoreX0, SvcInsn(SvcLoaderResult));
    u64 entry = wrapper;
    if (Bench()) {
        if (const u64 bench = Allocate(s, BenchSize); bench != 0) {
            for (u64 i = 0; i < BenchSize; i += 8) {
                memory.Write64(bench + i, 0);
            }
            WriteWords(memory, bench, BenchPreCode);
            memory.Write64(bench + 0x18, wrapper);
            WriteWords(memory, bench + 0x20, BenchCode);
            entry = bench;
        }
    }
    WriteWords(memory, jump, FarJumpCode);
    memory.Write64(jump + 8, entry);
    const s64 distance = static_cast<s64>(jump) - static_cast<s64>(loader);
    if (distance >= (s64{1} << 27) || distance < -(s64{1} << 27)) {
        LOG_WARNING(Kernel, "NVN trace: the far jump at {:#x} is out of branch range", jump);
        return;
    }
    memory.Write32(loader, 0x14000000 | ((static_cast<u64>(distance) >> 2) & 0x3FFFFFF));
    s.installed = true;
    LOG_INFO(Kernel, "NVN trace: nvnBootstrapLoader at {:#x} hooked, stubs at {:#x}", loader,
             stub_base);
}

bool HandleSvc(Core::System& system, u32 imm, std::array<u64, 8>& args) {
    if (imm == SvcBenchNothing) {
        return true; // a round trip and nothing else: what the bench times
    }
    if (imm >= SvcBenchStart && imm <= SvcBenchEmpty) {
        auto& s = GetState();
        const auto now = std::chrono::steady_clock::now();
        if (imm != SvcBenchStart) {
            s.bench_ns[imm - SvcBenchRoundTrips] =
                std::chrono::duration<double, std::nano>(now - s.bench_mark).count() / BenchSteps;
        }
        if (imm == SvcBenchEmpty) {
            LOG_INFO(Kernel,
                     "NVN trace bench ({} steps each): host round trip (svc) {:.1f} ns, 32-byte "
                     "record in game memory {:.1f} ns, empty loop step {:.1f} ns",
                     BenchSteps, s.bench_ns[0], s.bench_ns[1], s.bench_ns[2]);
        }
        s.bench_mark = std::chrono::steady_clock::now();
        return true;
    }
    if (imm != SvcLoaderResult && imm != SvcProcResult) {
        return false;
    }
    auto& s = GetState();
    std::scoped_lock lk{s.mutex};
    if (!s.installed || args[0] == 0) {
        return true;
    }
    auto& memory = system.ApplicationMemory();
    const std::string name = memory.ReadCString(args[1], 128);
    args[0] = StubFor(s, memory, args[0], name);
    return true;
}

void OnPresent(Core::System& system, u64 presents) {
    constexpr u64 Period = 600;
    if (!Enabled() || presents % Period != 0) {
        return;
    }
    auto& s = GetState();
    std::scoped_lock lk{s.mutex};
    if (!s.installed) {
        return;
    }
    auto& memory = system.ApplicationMemory();
    const u64 frames = std::max<u64>(presents - s.last_presents, 1);
    s.last_presents = presents;
    struct Row {
        u64 calls;
        const Entry* entry;
    };
    std::vector<Row> rows;
    u64 total = 0;
    size_t used = 0;
    for (auto& entry : s.entries) {
        const u64 now = memory.Read64(entry.count_addr);
        const u64 calls = now - entry.last;
        entry.last = now;
        total += calls;
        if (now != 0) {
            ++used;
        }
        if (calls != 0) {
            rows.push_back({calls, &entry});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.calls > b.calls; });
    std::string top;
    for (size_t i = 0; i < std::min<size_t>(rows.size(), 40); ++i) {
        top += fmt::format("\n    {:9.1f}  {}", static_cast<double>(rows[i].calls) / frames,
                           rows[i].entry->names);
    }
    LOG_INFO(Kernel,
             "NVN trace, presents {}: {:.0f} NVN calls per frame over {} frames; {} entry points "
             "handed out, {} ever called, {} called in this window. Calls per frame:{}",
             presents, static_cast<double>(total) / frames, frames, s.entries.size(), used,
             rows.size(), top);
}

} // namespace Kernel::NvnTrace

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "core/file_sys/fortunes_weave_data.h"

namespace FileSys::FortunesWeave {
namespace {

// A MessagePack value as the game's files use it. Encoding back gives the original bytes: floats
// keep their width, integers and strings take the smallest form, maps keep their key order.
struct Node {
    enum class Kind { Nil, Bool, Int, F32, F64, Str, Bin, Array, Map } kind{Kind::Nil};
    bool boolean{};
    s64 integer{};
    bool unsigned_big{}; ///< an integer above INT64_MAX (kept in `big`)
    u64 big{};
    double real{};
    std::string text; ///< Str and Bin bytes
    std::vector<Node> items;  ///< Array elements, or a Map's keys
    std::vector<Node> values; ///< a Map's values, in the order of its keys

    static Node Int(s64 v) {
        Node n;
        n.kind = Kind::Int;
        n.integer = v;
        return n;
    }
    static Node F64(double v) {
        Node n;
        n.kind = Kind::F64;
        n.real = v;
        return n;
    }
    static Node Str(std::string_view v) {
        Node n;
        n.kind = Kind::Str;
        n.text = v;
        return n;
    }

    bool IsNumber() const {
        return kind == Kind::Int || kind == Kind::F32 || kind == Kind::F64;
    }
    double Number() const {
        return kind == Kind::Int ? static_cast<double>(integer) : real;
    }
    Node* Find(std::string_view key) {
        if (kind != Kind::Map) {
            return nullptr;
        }
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].kind == Kind::Str && items[i].text == key) {
                return &values[i];
            }
        }
        return nullptr;
    }
    void Add(Node key, Node value) {
        items.push_back(std::move(key));
        values.push_back(std::move(value));
    }
};

class Reader {
public:
    explicit Reader(std::span<const u8> data_) : data{data_} {}

    std::optional<Node> ReadAll() {
        auto node = Read(0);
        if (!node || pos != data.size()) {
            return std::nullopt;
        }
        return node;
    }

private:
    bool Has(size_t n) const {
        return pos + n <= data.size();
    }
    u64 Be(size_t n) {
        u64 v = 0;
        for (size_t i = 0; i < n; ++i) {
            v = (v << 8) | data[pos++];
        }
        return v;
    }

    std::optional<Node> Read(int depth) {
        if (depth > 64 || !Has(1)) {
            return std::nullopt;
        }
        const u8 t = data[pos++];
        Node n;
        const auto sized = [&](size_t bytes) -> std::optional<u64> {
            if (!Has(bytes)) {
                return std::nullopt;
            }
            return Be(bytes);
        };
        if (t <= 0x7f) {
            return Node::Int(t);
        }
        if (t >= 0xe0) {
            return Node::Int(static_cast<s64>(t) - 256);
        }
        if (t >= 0x80 && t <= 0x8f) {
            return Container(Node::Kind::Map, t & 0xf, depth);
        }
        if (t >= 0x90 && t <= 0x9f) {
            return Container(Node::Kind::Array, t & 0xf, depth);
        }
        if (t >= 0xa0 && t <= 0xbf) {
            return Bytes(Node::Kind::Str, t & 0x1f);
        }
        switch (t) {
        case 0xc0:
            return n;
        case 0xc2:
        case 0xc3:
            n.kind = Node::Kind::Bool;
            n.boolean = t == 0xc3;
            return n;
        case 0xca: {
            const auto v = sized(4);
            if (!v) {
                return std::nullopt;
            }
            n.kind = Node::Kind::F32;
            n.real = std::bit_cast<float>(static_cast<u32>(*v));
            return n;
        }
        case 0xcb: {
            const auto v = sized(8);
            if (!v) {
                return std::nullopt;
            }
            n.kind = Node::Kind::F64;
            n.real = std::bit_cast<double>(*v);
            return n;
        }
        case 0xcc:
        case 0xcd:
        case 0xce:
        case 0xcf: {
            const auto v = sized(size_t{1} << (t - 0xcc));
            if (!v) {
                return std::nullopt;
            }
            n.kind = Node::Kind::Int;
            if (*v > static_cast<u64>(INT64_MAX)) {
                n.unsigned_big = true;
                n.big = *v;
            } else {
                n.integer = static_cast<s64>(*v);
            }
            return n;
        }
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3: {
            const size_t bytes = size_t{1} << (t - 0xd0);
            const auto v = sized(bytes);
            if (!v) {
                return std::nullopt;
            }
            const int shift = 64 - static_cast<int>(bytes) * 8;
            return Node::Int(static_cast<s64>(*v << shift) >> shift);
        }
        case 0xc4:
        case 0xc5:
        case 0xc6:
        case 0xd9:
        case 0xda:
        case 0xdb: {
            const bool bin = t <= 0xc6;
            const auto len = sized(size_t{1} << (t - (bin ? 0xc4 : 0xd9)));
            if (!len) {
                return std::nullopt;
            }
            return Bytes(bin ? Node::Kind::Bin : Node::Kind::Str, *len);
        }
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf: {
            const bool map = t >= 0xde;
            const auto count = sized((t == 0xdc || t == 0xde) ? 2 : 4);
            if (!count) {
                return std::nullopt;
            }
            return Container(map ? Node::Kind::Map : Node::Kind::Array, *count, depth);
        }
        default:
            return std::nullopt; // extension types: not in the game's settings files
        }
    }

    std::optional<Node> Bytes(Node::Kind kind, u64 len) {
        if (!Has(len)) {
            return std::nullopt;
        }
        Node n;
        n.kind = kind;
        n.text.assign(reinterpret_cast<const char*>(data.data() + pos), len);
        pos += len;
        return n;
    }

    std::optional<Node> Container(Node::Kind kind, u64 count, int depth) {
        if (count > data.size() - pos) {
            return std::nullopt; // every element takes at least one byte
        }
        Node n;
        n.kind = kind;
        for (u64 i = 0; i < count; ++i) {
            auto first = Read(depth + 1);
            if (!first) {
                return std::nullopt;
            }
            if (kind == Node::Kind::Array) {
                n.items.push_back(std::move(*first));
                continue;
            }
            auto second = Read(depth + 1);
            if (!second) {
                return std::nullopt;
            }
            n.Add(std::move(*first), std::move(*second));
        }
        return n;
    }

    std::span<const u8> data;
    size_t pos{};
};

void PutBe(std::vector<u8>& out, u64 v, size_t n) {
    for (size_t i = n; i-- > 0;) {
        out.push_back(static_cast<u8>(v >> (i * 8)));
    }
}

void Write(std::vector<u8>& out, const Node& n) {
    using K = Node::Kind;
    switch (n.kind) {
    case K::Nil:
        out.push_back(0xc0);
        return;
    case K::Bool:
        out.push_back(n.boolean ? 0xc3 : 0xc2);
        return;
    case K::F32:
        out.push_back(0xca);
        PutBe(out, std::bit_cast<u32>(static_cast<float>(n.real)), 4);
        return;
    case K::F64:
        out.push_back(0xcb);
        PutBe(out, std::bit_cast<u64>(n.real), 8);
        return;
    case K::Int: {
        if (n.unsigned_big) {
            out.push_back(0xcf);
            PutBe(out, n.big, 8);
            return;
        }
        const s64 v = n.integer;
        if (v >= 0) {
            if (v <= 0x7f) {
                out.push_back(static_cast<u8>(v));
            } else if (v <= 0xff) {
                out.push_back(0xcc);
                PutBe(out, static_cast<u64>(v), 1);
            } else if (v <= 0xffff) {
                out.push_back(0xcd);
                PutBe(out, static_cast<u64>(v), 2);
            } else if (v <= 0xffffffffLL) {
                out.push_back(0xce);
                PutBe(out, static_cast<u64>(v), 4);
            } else {
                out.push_back(0xcf);
                PutBe(out, static_cast<u64>(v), 8);
            }
        } else if (v >= -32) {
            out.push_back(static_cast<u8>(v + 256));
        } else if (v >= -0x80) {
            out.push_back(0xd0);
            PutBe(out, static_cast<u64>(v), 1);
        } else if (v >= -0x8000) {
            out.push_back(0xd1);
            PutBe(out, static_cast<u64>(v), 2);
        } else if (v >= -0x80000000LL) {
            out.push_back(0xd2);
            PutBe(out, static_cast<u64>(v), 4);
        } else {
            out.push_back(0xd3);
            PutBe(out, static_cast<u64>(v), 8);
        }
        return;
    }
    case K::Str:
    case K::Bin: {
        const size_t len = n.text.size();
        if (n.kind == K::Bin) {
            const size_t bytes = len <= 0xff ? 1 : len <= 0xffff ? 2 : 4;
            out.push_back(static_cast<u8>(0xc4 + (bytes == 1 ? 0 : bytes == 2 ? 1 : 2)));
            PutBe(out, len, bytes);
        } else if (len <= 31) {
            out.push_back(static_cast<u8>(0xa0 | len));
        } else if (len <= 0xff) {
            out.push_back(0xd9);
            PutBe(out, len, 1);
        } else if (len <= 0xffff) {
            out.push_back(0xda);
            PutBe(out, len, 2);
        } else {
            out.push_back(0xdb);
            PutBe(out, len, 4);
        }
        out.insert(out.end(), n.text.begin(), n.text.end());
        return;
    }
    case K::Array:
    case K::Map: {
        const bool map = n.kind == K::Map;
        const size_t count = n.items.size();
        if (count <= 15) {
            out.push_back(static_cast<u8>((map ? 0x80 : 0x90) | count));
        } else if (count <= 0xffff) {
            out.push_back(map ? 0xde : 0xdc);
            PutBe(out, count, 2);
        } else {
            out.push_back(map ? 0xdf : 0xdd);
            PutBe(out, count, 4);
        }
        for (size_t i = 0; i < count; ++i) {
            Write(out, n.items[i]);
            if (map) {
                Write(out, n.values[i]);
            }
        }
        return;
    }
    }
}

/// Parses `file` and checks that writing it back gives the same bytes, so an edit changes nothing
/// else in the file.
std::optional<Node> Parse(std::span<const u8> file) {
    auto doc = Reader{file}.ReadAll();
    if (!doc) {
        return std::nullopt;
    }
    std::vector<u8> again;
    again.reserve(file.size());
    Write(again, *doc);
    if (!std::equal(again.begin(), again.end(), file.begin(), file.end())) {
        return std::nullopt;
    }
    return doc;
}

std::vector<u8> Serialize(const Node& doc) {
    std::vector<u8> out;
    Write(out, doc);
    return out;
}

/// A number of the same kind as `like` (an integer stays one where `value` is whole).
Node SameKind(const Node& like, double value) {
    Node n = like;
    if (like.kind == Node::Kind::Int) {
        n.integer = static_cast<s64>(value);
    } else {
        n.real = value;
    }
    return n;
}

} // namespace

namespace {

/// Calls `edit` on the value of every key frame of <section>.<key> (an array of {frame, value}
/// maps; a value-less key frame keeps the previous value). True when one of them changed.
template <typename Edit>
bool EditKeyframes(Node& doc, std::string_view section, std::string_view key, Edit&& edit) {
    Node* frames = nullptr;
    if (Node* values = doc.Find(section); values != nullptr) {
        frames = values->Find(key);
    }
    if (frames == nullptr || frames->kind != Node::Kind::Array) {
        return false;
    }
    bool changed = false;
    for (auto& frame : frames->items) {
        if (Node* value = frame.Find("value"); value != nullptr) {
            changed |= edit(*value);
        }
    }
    return changed;
}

/// An integer above `limit` becomes `limit`.
auto LowerTo(s64 limit) {
    return [limit](Node& value) {
        if (value.kind != Node::Kind::Int || value.unsigned_big || value.integer <= limit) {
            return false;
        }
        value.integer = limit;
        return true;
    };
}

/// A true boolean becomes false.
bool TurnOff(Node& value) {
    if (value.kind != Node::Kind::Bool || !value.boolean) {
        return false;
    }
    value.boolean = false;
    return true;
}

} // namespace

std::optional<std::vector<u8>> EditGraphicsSettings(std::span<const u8> file,
                                                    const GraphicsSettingsEdits& edits) {
    auto doc = Parse(file);
    if (!doc) {
        return std::nullopt;
    }
    bool changed = false;
    if (edits.max_shadow_cascades > 0) {
        Node* shadow = doc->Find("SceneShadow");
        if (shadow != nullptr && shadow->Find("MaxCascadeCount") != nullptr) {
            changed |= EditKeyframes(*doc, "SceneShadow", "MaxCascadeCount",
                                     LowerTo(edits.max_shadow_cascades));
        } else if (shadow != nullptr && shadow->kind == Node::Kind::Map &&
                   edits.add_shadow_cascades) {
            // As the maps write it: [{"frame": -1, "value": n}] (-1: no key frames, one value).
            Node key;
            key.kind = Node::Kind::Map;
            key.Add(Node::Str("frame"), Node::Int(-1));
            key.Add(Node::Str("value"), Node::Int(edits.max_shadow_cascades));
            Node keys_node;
            keys_node.kind = Node::Kind::Array;
            keys_node.items.push_back(std::move(key));
            shadow->Add(Node::Str("MaxCascadeCount"), std::move(keys_node));
            changed = true;
        }
    }
    if (edits.disable_reflections) {
        changed |= EditKeyframes(*doc, "SSR", "Enabled", TurnOff);
    } else if (edits.max_reflection_rays > 0) {
        changed |= EditKeyframes(*doc, "SSR", "RaySamplingCount",
                                 LowerTo(edits.max_reflection_rays));
    }
    if (edits.disable_ambient_occlusion) {
        changed |= EditKeyframes(*doc, "AO", "SSAO_Enabled", TurnOff);
    }
    if (!changed) {
        return std::nullopt;
    }
    return Serialize(*doc);
}

std::optional<std::vector<u8>> LimitShadowCascades(std::span<const u8> file, int cascades,
                                                   bool add_if_missing) {
    GraphicsSettingsEdits edits;
    edits.max_shadow_cascades = cascades;
    edits.add_shadow_cascades = add_if_missing;
    return EditGraphicsSettings(file, edits);
}

std::optional<std::vector<u8>> LighterCrowdLod(std::span<const u8> file) {
    auto doc = Parse(file);
    if (!doc) {
        return std::nullopt;
    }
    Node* presets = doc->Find("presets");
    if (presets == nullptr) {
        return std::nullopt;
    }
    bool changed = false;
    for (const std::string_view name : {"CharacterMob", "CharacterLegion"}) {
        Node* preset = presets->Find(name);
        if (preset == nullptr) {
            continue;
        }
        Node* thresholds = preset->Find("Thresholds");
        Node* hide_level = preset->Find("HideLevel");
        Node* hide_distance = preset->Find("HideDistanceMin");
        if (thresholds == nullptr || thresholds->kind != Node::Kind::Array || hide_level == nullptr ||
            hide_level->kind != Node::Kind::Int || hide_distance == nullptr ||
            !hide_distance->IsNumber()) {
            continue;
        }
        // Thresholds: the screen-size fraction below which the next simpler model is drawn; the one
        // at HideLevel - 1 is where the character stops being drawn (beyond HideDistanceMin).
        auto& t = thresholds->items;
        const s64 hide = hide_level->integer - 1;
        if (hide < 0 || hide >= static_cast<s64>(t.size()) ||
            !std::all_of(t.begin(), t.end(), [](const Node& n) { return n.IsNumber(); })) {
            continue;
        }
        for (auto& level : t) {
            if (level.Number() > 0) {
                level = SameKind(level, std::min(level.Number() * 2, 1.6));
            }
        }
        t[hide] = t[hide].kind == Node::Kind::Int ? Node::F64(0.05) : SameKind(t[hide], 0.05);
        *hide_distance = SameKind(*hide_distance, std::min(hide_distance->Number(), 25.0));
        changed = true;
    }
    if (!changed) {
        return std::nullopt;
    }
    return Serialize(*doc);
}

} // namespace FileSys::FortunesWeave

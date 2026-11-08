#include "gpu/cpuvertex/native_programs.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace mh4n::gpu::cpuvertex {

namespace {

bool ByWords() {
    static const bool on = [] {
        const char* v = std::getenv("MH4N_PROGRAMS_BY_WORDS");
        return v == nullptr || *v != '0';
    }();
    return on;
}

}  // namespace

void NativePrograms::SetTable(const PicaNativeEntry* entries, u32 count) {
    entries_.assign(entries, entries + (entries != nullptr ? count : 0));
    known_.clear();
    groups_.clear();
    seen_.clear();
    seen_next_ = 0;
    for (const PicaNativeEntry& e : entries_) {
        if (e.code_words > pica::kMaxProgramWords || e.desc_count > pica::kMaxSwizzleWords) {
            continue;  // (never matches: Hashed leaves it out)
        }
        const bool geometry = e.geometry != 0;
        auto g = std::find_if(groups_.begin(), groups_.end(),
                              [&](const Group& x) { return x.entry_point == e.entry_point && x.geometry == geometry; });
        if (g == groups_.end()) {
            groups_.push_back(Group{e.entry_point, geometry, 0, 0});
            g = groups_.end() - 1;
        }
        g->code_words = std::max(g->code_words, e.code_words);
        g->desc_count = std::max(g->desc_count, e.desc_count);
    }
}

PicaNativeFunction NativePrograms::Hashed(const pica::ShaderSetup& unit, u32 entry_point, bool geometry) const {
    // (the hash of the first n words, once per n: the entries share a few lengths, and each hash reads the words)
    struct Hash {
        u32 n;
        u64 hash;
    };
    std::array<Hash, 8> code_hashes, desc_hashes;
    size_t code_n = 0, desc_n = 0;
    const auto hash_of = [](const u32* words, u32 n, std::array<Hash, 8>& memo, size_t& used) {
        for (size_t i = 0; i < used; ++i) {
            if (memo[i].n == n) return memo[i].hash;
        }
        const u64 h = pica_native_hash(words, n);
        memo[used < memo.size() ? used++ : memo.size() - 1] = Hash{n, h};
        return h;
    };
    for (const PicaNativeEntry& e : entries_) {
        if (e.entry_point != entry_point || (e.geometry != 0) != geometry || e.code_words > unit.program_code.size() ||
            e.desc_count > unit.swizzle_data.size()) {
            continue;
        }
        if (hash_of(unit.program_code.data(), e.code_words, code_hashes, code_n) == e.code_hash &&
            hash_of(unit.swizzle_data.data(), e.desc_count, desc_hashes, desc_n) == e.desc_hash) {
            return e.function;
        }
    }
    return nullptr;
}

PicaNativeFunction NativePrograms::Find(const pica::ShaderSetup& unit, u32 entry_point, bool geometry) {
    if (entries_.empty() || unit.program_serial == 0 || unit.swizzle_serial == 0) {
        return nullptr;
    }
    for (const Known& k : known_) {
        if (k.program_serial == unit.program_serial && k.swizzle_serial == unit.swizzle_serial &&
            k.entry_point == entry_point && k.geometry == geometry) {
            return k.function;
        }
    }
    PicaNativeFunction found = nullptr;
    const auto group = std::find_if(groups_.begin(), groups_.end(),
                                    [&](const Group& g) { return g.entry_point == entry_point && g.geometry == geometry; });
    if (group != groups_.end()) {
        // the words seen before at this entry point (the game's few programs in turn), else the hashes
        const Seen* seen = nullptr;
        if (ByWords()) {
            for (const Seen& s : seen_) {
                if (s.entry_point == entry_point && s.geometry == geometry &&
                    std::memcmp(s.code.data(), unit.program_code.data(), s.code.size() * sizeof(u32)) == 0 &&
                    std::memcmp(s.desc.data(), unit.swizzle_data.data(), s.desc.size() * sizeof(u32)) == 0) {
                    seen = &s;
                    break;
                }
            }
        }
        if (seen != nullptr) {
            found = seen->function;
        } else {
            found = Hashed(unit, entry_point, geometry);
            if (ByWords()) {
                constexpr size_t kSeen = 64;
                Seen s;
                s.entry_point = entry_point;
                s.geometry = geometry;
                s.code.assign(unit.program_code.begin(), unit.program_code.begin() + group->code_words);
                s.desc.assign(unit.swizzle_data.begin(), unit.swizzle_data.begin() + group->desc_count);
                s.function = found;
                if (seen_.size() < kSeen) {
                    seen_.push_back(std::move(s));
                } else {
                    seen_[seen_next_] = std::move(s);
                    seen_next_ = (seen_next_ + 1) % kSeen;
                }
            }
        }
    }
    constexpr size_t kKnown = 32;
    if (known_.size() >= kKnown) {
        known_.erase(known_.begin());
    }
    known_.push_back(Known{unit.program_serial, unit.swizzle_serial, entry_point, geometry, found});
    return found;
}

}  // namespace mh4n::gpu::cpuvertex

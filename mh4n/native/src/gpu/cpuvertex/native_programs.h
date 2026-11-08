// The PICA programs the player's code library carries compiled (tools/recomp/runtime/pica_native.h, made by
// tools/pica_native.py from the game's shader package): which one a unit's program at an entry point is.
//
// An entry matches when the unit's program words [0, code_words) and operand descriptors [0, desc_count) hash as the
// entry says, at the same entry point and for the same kind of unit. The answer is kept by the unit's program and
// swizzle serials (pica::ShaderSetup: a number taken at each change of them), so the words are hashed again only
// after an upload; a unit whose serials are not known (0) runs on the interpreter.
//
// The game uploads a handful of programs in turn, dozens of uploads a frame, and each upload is a new serial: the hashes
// (a byte at a time, for every length the entries of the entry point have) were a tenth of the renderer thread on the
// phones. So an answer is also kept by the words themselves: the unit's first words of the program and of the
// descriptors, as many as the longest entry of that entry point and kind hashes, give the same answer whatever serials
// they come with, since every entry's hashes read only words among those. MH4N_PROGRAMS_BY_WORDS=0: the hashes on every
// new serial, as before.
#pragma once

#include <vector>

#include "gpu/pica/state.h"
#include "mh4n/types.h"
#include "pica_native.h"

namespace mh4n::gpu::cpuvertex {

class NativePrograms {
public:
    // The table of a code library (none: no program is compiled).
    void SetTable(const PicaNativeEntry* entries, u32 count);
    bool Empty() const { return entries_.empty(); }

    // The compiled function for `unit`'s program at `entry_point`, or nullptr.
    PicaNativeFunction Find(const pica::ShaderSetup& unit, u32 entry_point, bool geometry);

private:
    struct Known {
        u64 program_serial = 0, swizzle_serial = 0;
        u32 entry_point = 0;
        bool geometry = false;
        PicaNativeFunction function = nullptr;
    };
    // The words the answers at an entry point, for a kind of unit, depend on: the longest program and descriptor prefixes
    // its entries hash.
    struct Group {
        u32 entry_point = 0;
        bool geometry = false;
        u32 code_words = 0, desc_count = 0;
    };
    // An answer kept by those words, as the unit had them.
    struct Seen {
        u32 entry_point = 0;
        bool geometry = false;
        std::vector<u32> code, desc;
        PicaNativeFunction function = nullptr;
    };
    PicaNativeFunction Hashed(const pica::ShaderSetup& unit, u32 entry_point, bool geometry) const;

    std::vector<PicaNativeEntry> entries_;
    std::vector<Known> known_;  // the answers so far (a few programs a frame)
    std::vector<Group> groups_;
    std::vector<Seen> seen_;
    size_t seen_next_ = 0;
};

}  // namespace mh4n::gpu::cpuvertex

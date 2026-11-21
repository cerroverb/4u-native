// Derived from Azahar src/video_core/shader/shader_interpreter.cpp, src/video_core/pica/shader_unit.cpp
// and src/video_core/pica_types.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later;
// every one of those files says "Licensed under GPLv2 or any later version"): the interpreter, one
// handler for each instruction, in the order and with the quirks of the original; ShaderUnit::LoadInput
// and WriteOutput; GeometryEmitter::Emit.
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (shader_interpreter.cpp)
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (shader_unit.cpp)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (pica_types.h)
//
// Each `case` below is the same case of RunInterpreter in shader_interpreter.cpp (Debug = false: the
// debug records are not kept). Where this file differs it says so in a comment starting with
// "Deviation".
#include "gpu/cpuvertex/shader_interpreter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "mh4n/log.h"

#if defined(__aarch64__)
#include <arm_neon.h>
#define MH4N_DECODED_NEON 1  // (the decoded interpreter's four lanes at once: InterpretDecoded)
#else
#define MH4N_DECODED_NEON 0
#endif

namespace mh4n::gpu::cpuvertex {

using isa::CompareOp;
using isa::FlowOp;
using isa::Instruction;
using isa::Opcode;
using isa::RegisterType;
using isa::SwizzlePattern;
using isa::Type;

// ---- ShaderUnit and the emitter (shader_unit.cpp) -------------------------------------------------

void ShaderUnit::LoadInput(const ShaderInputConfig& config, const AttributeBuffer& buffer) {
    const u32 max_attribute = config.max_input_attribute_index;
    for (u32 attr = 0; attr <= max_attribute; ++attr) {
        const u32 reg = config.GetRegisterForAttribute(attr);
        input[reg] = buffer[attr];
    }
}

void ShaderUnit::WriteOutput(u32 output_mask, AttributeBuffer& buffer) const {
    u32 output_index{};
    // Common::BitSet<u32>: the set bits, lowest first.
    for (u32 mask = output_mask; mask != 0; mask &= mask - 1) {
        const u32 reg = static_cast<u32>(__builtin_ctz(mask));
        buffer[output_index++] = output[output_bank][reg];
    }
}

void GeometryEmitter::Emit(std::span<const Vec4, 16> output_regs) {
    // The interpreter has checked emit_state.vertex_id < 3 (Azahar: ASSERT).
    u32 output_index{};
    for (u32 mask = output_mask; mask != 0; mask &= mask - 1) {
        const u32 reg = static_cast<u32>(__builtin_ctz(mask));
        buffer[emit_state.vertex_id][output_index++] = output_regs[reg];
    }

    if (emit_state.prim_emit) {
        if (emit_state.winding) {
            sink->SetWinding();
        }
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            sink->SubmitVertex(buffer[i]);
        }
    }
}

void GeometryShaderUnit::Reset() {
    VertexSink* keep = emitter.sink;
    const u32 mask = emitter.output_mask;
    address_registers[0] = address_registers[1] = address_registers[2] = 0;
    conditional_code[0] = conditional_code[1] = false;
    input = {};
    temporary = {};
    output = {};
    output_bank = false;
    emitter = GeometryEmitter{};
    emitter.sink = keep;
    emitter.output_mask = mask;
}

// ---- the interpreter (shader_interpreter.cpp) ------------------------------------------------------

namespace {

struct IfStackElement {
    u32 else_address;
    u32 end_address;
};

struct CallStackElement {
    u32 end_address;
    u32 return_address;
};

struct LoopStackElement {
    u32 entry_address;
    u32 end_address;
    u8 loop_downcounter;
    u8 address_increment;
    u8 previous_aL;
};

// boost::circular_buffer<T>(N) as the interpreter uses it: pushing onto a full buffer overwrites
// the oldest element.
template <typename T, std::size_t N>
class CircularStack {
public:
    bool empty() const {
        return size_ == 0;
    }
    std::size_t size() const {
        return size_;
    }
    T& back() {
        return data_[(first_ + size_ - 1) % N];
    }
    void push_back(const T& value) {
        if (size_ == N) {
            first_ = (first_ + 1) % N;
            --size_;
        }
        data_[(first_ + size_) % N] = value;
        ++size_;
    }
    void pop_back() {
        --size_;
    }

private:
    std::array<T, N> data_{};
    std::size_t first_ = 0;
    std::size_t size_ = 0;
};

// static_cast<s32>(float) as it is on x86-64 (Azahar's only configuration that matters here): a
// value that does not fit, and NaN, give the most negative integer. The C++ cast is undefined
// there. (Every such value is outside the 8-bit range LookupSourceRegister allows an address
// register, so the result only has to be deterministic.)
s32 FloatToS32(float value) {
    if (!(value > -2147483648.0f && value < 2147483648.0f)) {
        return std::numeric_limits<s32>::min();
    }
    return static_cast<s32>(value);
}

} // namespace

// ---- compiled programs (pica_native.h) ------------------------------------------------------------

static_assert(sizeof(Vec4) == 16 && sizeof(f24) == 4, "a register is float[4] to a compiled program");
static_assert(sizeof(bool) == 1, "the bool uniforms and the conditional code are bytes to a compiled program");
static_assert(sizeof(std::array<std::array<Vec4, 16>, 2>) == 2 * 16 * 16, "the output banks are float[2][16][4]");

namespace {

std::atomic<u64> g_check_runs{0};
std::atomic<u64> g_check_differences{0};

// EMIT and SETEMIT of a compiled program: the interpreter's checks, then the emitter.
struct NativeContext {
    ShaderUnit* unit = nullptr;
    Unhandled failure = Unhandled::None;
};

int NativeEmit(void* ctx) {
    NativeContext& c = *static_cast<NativeContext*>(ctx);
    GeometryEmitter* emitter = c.unit->emitter_ptr;
    if (emitter == nullptr) {
        c.failure = Unhandled::EmitOnVertexShader;
        return 0;
    }
    if (emitter->emit_state.vertex_id >= 3) {
        c.failure = Unhandled::EmitVertexId;
        return 0;
    }
    emitter->Emit(c.unit->output[c.unit->output_bank]);
    return 1;
}

int NativeSetEmit(void* ctx, unsigned vertex_id, unsigned prim_emit, unsigned winding) {
    NativeContext& c = *static_cast<NativeContext*>(ctx);
    GeometryEmitter* emitter = c.unit->emitter_ptr;
    if (emitter == nullptr) {
        c.failure = Unhandled::EmitOnVertexShader;
        return 0;
    }
    emitter->emit_state.vertex_id = static_cast<u8>(vertex_id);
    emitter->emit_state.prim_emit = prim_emit != 0;
    emitter->emit_state.winding = winding != 0;
    return 1;
}

float NativeExp2(float x) {
    return std::exp2(x);
}
float NativeLog2(float x) {
    return std::log2(x);
}

// A sink that keeps what it is given (and passes it on, when it has where to).
class RecordingSink final : public VertexSink {
public:
    explicit RecordingSink(VertexSink* next) : next_(next) {}
    void SubmitVertex(const AttributeBuffer& output) override {
        vertices.push_back(output);
        if (next_ != nullptr) {
            next_->SubmitVertex(output);
        }
    }
    void SetWinding() override {
        windings.push_back(vertices.size());
        if (next_ != nullptr) {
            next_->SetWinding();
        }
    }
    std::vector<AttributeBuffer> vertices;
    std::vector<size_t> windings;

private:
    VertexSink* next_;
};

// The same floats, bit for bit, except that any NaN equals any NaN: which NaN an operation on NaNs gives (the sign, the
// payload) is up to the compiler's order of commutative operands and to the CPU, and nothing that reads a register sees it.
bool SameBits(const void* a, const void* b, size_t size) {
    const auto* x = static_cast<const f24*>(a);
    const auto* y = static_cast<const f24*>(b);
    for (size_t i = 0; i < size / sizeof(f24); ++i) {
        const float p = x[i].value, q = y[i].value;
        if (!(std::isnan(p) && std::isnan(q)) && std::memcmp(&p, &q, sizeof(float)) != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---- the decoded interpreter (MH4N_PICA_DECODED) ---------------------------------------------------
//
// InterpretDecoded is Interpret with each instruction word decoded once: the record says which handler, which
// registers the sources are (kind, index, the address register a uniform is offset by), their selectors and negation,
// the destination and its write mask. A record is used only while the program's word at its address and the operand
// descriptor it was decoded with are the words it was made from (compared at every step); otherwise the word is decoded
// again. Every handler does what Interpret's does, with the same f24 operations in the same order, and the flow
// control (the stacks, the budget, the last word taken as END) is Interpret's own code.

namespace {

enum class Exec : u8 {
    Add, Mul, Flr, Max, Min, Dp3, Dp4, Dph, Rcp, Rsq, Mova, Mov, Sge, Slt, Cmp, Ex2, Lg2,
    BadArithmetic,  // DST, DSTI, LIT: Interpret looks the sources up, then stops (Unhandled::Instruction)
    Mad,
    End, Jmpc, Jmpu, Call, Callu, Callc, Nop, Ifu, Ifc, Loop, Break, Breakc, Emit, SetEmit,
    BadOther,       // an opcode without a handler
};

struct DecodedSource {
    u8 type = 0;   // RegisterType: 0 input, 1 temporary, 2 float uniform
    u8 index = 0;  // SourceRegister::GetIndex()
    u8 addr = 0;   // the address register it is offset by: 0 none, 1 a0.x, 2 a0.y, 3 aL (float uniforms only)
    u8 negate = 0;
    u8 sel[4] = {};
};

struct DecodedOp {
    u32 hex = 0;  // the instruction word it was decoded from
    u32 swz = 0;  // the operand descriptor word it was decoded with (swizzle_data[desc])
    u16 desc = 0;
    u8 valid = 0;
    Exec exec = Exec::BadOther;
    u8 dest_output = 0;
    u8 dest_index = 0;
    u8 mask = 0;  // bit i: component i written (SwizzlePattern::DestComponentEnabled(i))
    u8 compare[2] = {};
    DecodedSource src[3];
#if MH4N_DECODED_NEON
    // The same as vectors (FillVectors): each source's selectors as a table of byte indices (TBL), its negation as a
    // sign mask (EOR), the write mask as whole lanes (BSL)
    alignas(16) u8 tbl[3][16] = {};
    alignas(16) u32 negm[3][4] = {};
    alignas(16) u32 maskv[4] = {};
#endif
};

DecodedSource MakeSource(isa::SourceRegister reg, u32 address_register_index, bool negate, u32 sel0, u32 sel1, u32 sel2,
                         u32 sel3) {
    DecodedSource s;
    s.type = static_cast<u8>(reg.GetRegisterType());
    s.index = static_cast<u8>(reg.GetIndex());
    s.addr = static_cast<u8>(address_register_index);
    s.negate = negate ? 1 : 0;
    s.sel[0] = static_cast<u8>(sel0);
    s.sel[1] = static_cast<u8>(sel1);
    s.sel[2] = static_cast<u8>(sel2);
    s.sel[3] = static_cast<u8>(sel3);
    return s;
}

u8 MaskOf(SwizzlePattern swizzle) {
    u8 mask = 0;
    for (u32 i = 0; i < 4; ++i) {
        if (swizzle.DestComponentEnabled(i)) mask = static_cast<u8>(mask | (1u << i));
    }
    return mask;
}

DecodedOp DecodeFields(u32 hex, const u32* swizzle_data) {
    DecodedOp d;
    d.hex = hex;
    d.valid = 1;
    const Instruction instr{hex};
    const isa::OpInfo& info = instr.GetInfo();
    switch (info.type) {
    case Type::Arithmetic: {
        d.desc = static_cast<u16>(instr.common_operand_desc_id());
        d.swz = swizzle_data[d.desc];
        const SwizzlePattern swizzle{d.swz};
        const bool is_inverted = info.src_inversed;
        const u32 address = instr.common_address_register_index();
        d.src[0] = MakeSource(instr.common_src1(is_inverted), !is_inverted * address, swizzle.negate_src1(),
                              swizzle.selector_src1(0), swizzle.selector_src1(1), swizzle.selector_src1(2),
                              swizzle.selector_src1(3));
        d.src[1] = MakeSource(instr.common_src2(is_inverted), is_inverted * address, swizzle.negate_src2(),
                              swizzle.selector_src2(0), swizzle.selector_src2(1), swizzle.selector_src2(2),
                              swizzle.selector_src2(3));
        d.dest_output = instr.common_dest().IsOutput() ? 1 : 0;
        d.dest_index = static_cast<u8>(instr.common_dest().GetIndex());
        d.mask = MaskOf(swizzle);
        d.compare[0] = static_cast<u8>(instr.common_compare_x());
        d.compare[1] = static_cast<u8>(instr.common_compare_y());
        switch (instr.EffectiveOpCode()) {
        case Opcode::ADD: d.exec = Exec::Add; break;
        case Opcode::MUL: d.exec = Exec::Mul; break;
        case Opcode::FLR: d.exec = Exec::Flr; break;
        case Opcode::MAX: d.exec = Exec::Max; break;
        case Opcode::MIN: d.exec = Exec::Min; break;
        case Opcode::DP3: d.exec = Exec::Dp3; break;
        case Opcode::DP4: d.exec = Exec::Dp4; break;
        case Opcode::DPH:
        case Opcode::DPHI: d.exec = Exec::Dph; break;
        case Opcode::RCP: d.exec = Exec::Rcp; break;
        case Opcode::RSQ: d.exec = Exec::Rsq; break;
        case Opcode::MOVA: d.exec = Exec::Mova; break;
        case Opcode::MOV: d.exec = Exec::Mov; break;
        case Opcode::SGE:
        case Opcode::SGEI: d.exec = Exec::Sge; break;
        case Opcode::SLT:
        case Opcode::SLTI: d.exec = Exec::Slt; break;
        case Opcode::CMP: d.exec = Exec::Cmp; break;
        case Opcode::EX2: d.exec = Exec::Ex2; break;
        case Opcode::LG2: d.exec = Exec::Lg2; break;
        default: d.exec = Exec::BadArithmetic; break;
        }
        return d;
    }
    case Type::MultiplyAdd: {
        d.desc = static_cast<u16>(instr.mad_operand_desc_id());
        d.swz = swizzle_data[d.desc];
        if (instr.EffectiveOpCode() != Opcode::MAD && instr.EffectiveOpCode() != Opcode::MADI) {
            d.exec = Exec::BadOther;  // (Interpret's else branch: no source looked up)
            return d;
        }
        const SwizzlePattern mad_swizzle{d.swz};
        const bool is_inverted = instr.EffectiveOpCode() == Opcode::MADI;
        const u32 address = instr.mad_address_register_index();
        d.src[0] = MakeSource(instr.mad_src1(is_inverted), 0, mad_swizzle.negate_src1(), mad_swizzle.selector_src1(0),
                              mad_swizzle.selector_src1(1), mad_swizzle.selector_src1(2), mad_swizzle.selector_src1(3));
        d.src[1] = MakeSource(instr.mad_src2(is_inverted), !is_inverted * address, mad_swizzle.negate_src2(),
                              mad_swizzle.selector_src2(0), mad_swizzle.selector_src2(1), mad_swizzle.selector_src2(2),
                              mad_swizzle.selector_src2(3));
        d.src[2] = MakeSource(instr.mad_src3(is_inverted), is_inverted * address, mad_swizzle.negate_src3(),
                              mad_swizzle.selector_src3(0), mad_swizzle.selector_src3(1), mad_swizzle.selector_src3(2),
                              mad_swizzle.selector_src3(3));
        d.dest_output = instr.mad_dest().IsOutput() ? 1 : 0;
        d.dest_index = static_cast<u8>(instr.mad_dest().GetIndex());
        d.mask = MaskOf(mad_swizzle);
        d.exec = Exec::Mad;
        return d;
    }
    default:
        d.desc = static_cast<u16>(instr.common_operand_desc_id());  // (only so the record is checked as the others)
        d.swz = swizzle_data[d.desc];
        switch (static_cast<Opcode>(instr.opcode())) {
        case Opcode::END: d.exec = Exec::End; break;
        case Opcode::JMPC: d.exec = Exec::Jmpc; break;
        case Opcode::JMPU: d.exec = Exec::Jmpu; break;
        case Opcode::CALL: d.exec = Exec::Call; break;
        case Opcode::CALLU: d.exec = Exec::Callu; break;
        case Opcode::CALLC: d.exec = Exec::Callc; break;
        case Opcode::NOP: d.exec = Exec::Nop; break;
        case Opcode::IFU: d.exec = Exec::Ifu; break;
        case Opcode::IFC: d.exec = Exec::Ifc; break;
        case Opcode::LOOP: d.exec = Exec::Loop; break;
        case Opcode::BREAK: d.exec = Exec::Break; break;
        case Opcode::BREAKC: d.exec = Exec::Breakc; break;
        case Opcode::EMIT: d.exec = Exec::Emit; break;
        case Opcode::SETEMIT: d.exec = Exec::SetEmit; break;
        default: d.exec = Exec::BadOther; break;
        }
        return d;
    }
}

DecodedOp Decode(u32 hex, const u32* swizzle_data) {
    DecodedOp d = DecodeFields(hex, swizzle_data);
#if MH4N_DECODED_NEON
    for (u32 k = 0; k < 3; ++k) {
        for (u32 lane = 0; lane < 4; ++lane) {
            for (u32 b = 0; b < 4; ++b) {
                d.tbl[k][4 * lane + b] = static_cast<u8>(4 * d.src[k].sel[lane] + b);
            }
            d.negm[k][lane] = d.src[k].negate ? 0x80000000u : 0u;
        }
    }
    for (u32 lane = 0; lane < 4; ++lane) {
        d.maskv[lane] = (d.mask & (1u << lane)) != 0 ? 0xFFFFFFFFu : 0u;
    }
#endif
    return d;
}

const u32 kDecodedNoSwizzles[128] = {};  // (the END record decodes against it)
const f24 kDecodedZeros[4] = {f24::Zero(), f24::Zero(), f24::Zero(), f24::Zero()};
const f24 kDecodedOnes[4] = {f24::One(), f24::One(), f24::One(), f24::One()};

// LookupSourceRegister of Interpret, from a record.
inline const f24* SourceOf(const DecodedSource& s, const ShaderUnit& state, const Uniforms& uniforms) {
    switch (s.type) {
    case 0:
        return &state.input[s.index].c[0];
    case 1:
        return &state.temporary[s.index].c[0];
    case 2: {
        int index = s.index;
        if (s.addr != 0) {
            int offset = state.address_registers[s.addr - 1];
            if (offset < std::numeric_limits<s8>::min() || offset > std::numeric_limits<s8>::max()) [[unlikely]] {
                offset = 0;
            }
            index = (index + offset) & 0x7F;
            if (index >= 96) [[unlikely]] {
                return kDecodedOnes;
            }
        }
        return &uniforms.f[static_cast<size_t>(index)].c[0];
    }
    default:
        return kDecodedZeros;
    }
}

// A source's four lanes as Interpret makes them: the selected components, then the negation.
inline void Load(const DecodedSource& s, const f24* p, f24 (&out)[4]) {
    out[0] = p[s.sel[0]];
    out[1] = p[s.sel[1]];
    out[2] = p[s.sel[2]];
    out[3] = p[s.sel[3]];
    if (s.negate) {
        out[0] = -out[0];
        out[1] = -out[1];
        out[2] = -out[2];
        out[3] = -out[3];
    }
}

#if MH4N_DECODED_NEON
// A source's four lanes as Load makes them: the register's 16 bytes through the selectors' table, then the sign mask.
inline float32x4_t VLoad(const DecodedOp& d, u32 k, const f24* p) {
    const uint8x16_t raw = vreinterpretq_u8_f32(vld1q_f32(reinterpret_cast<const float*>(p)));
    const uint8x16_t picked = vqtbl1q_u8(raw, vld1q_u8(d.tbl[k]));
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_u8(picked), vld1q_u32(d.negm[k])));
}
// f24's multiplication, lane by lane: the IEEE product, and +0 where it is NaN though neither factor is (0 x infinity)
inline float32x4_t VMul(float32x4_t a, float32x4_t b) {
    const float32x4_t r = vmulq_f32(a, b);
    const uint32x4_t r_nan = vmvnq_u32(vceqq_f32(r, r));
    const uint32x4_t factors = vandq_u32(vceqq_f32(a, a), vceqq_f32(b, b));
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(r), vandq_u32(r_nan, factors)));
}
// The lanes the write mask enables written, the others left as they are
inline void VStore(f24* dest, const DecodedOp& d, float32x4_t r) {
    float* const out = reinterpret_cast<float*>(dest);
    vst1q_f32(out, vbslq_f32(vld1q_u32(d.maskv), r, vld1q_f32(out)));
}
#endif

std::atomic<bool> g_decoded_off{false};
std::atomic<u64> g_work_steps{0}, g_work_decodes{0};  // TakeDecodedWork
std::atomic<bool> g_decoded_scalar{false};  // MH4N_PICA_DECODED=scalar, or the MH4N_AB phase "decvec": no lanes at once
std::atomic<u64> g_decoded_runs{0};
std::atomic<u64> g_decoded_differences{0};
std::atomic<u64> g_decoded_plain_ns{0};
std::atomic<u64> g_decoded_fast_ns{0};

enum class DecodedMode { On, Off, Check };
DecodedMode DecodedModeOf() {
    static const DecodedMode mode = [] {
        const char* v = std::getenv("MH4N_PICA_DECODED");
        const std::string_view s = v != nullptr ? v : "on";
        if (s == "scalar") g_decoded_scalar.store(true, std::memory_order_relaxed);
        return s == "off" || s == "0" ? DecodedMode::Off : s == "check" ? DecodedMode::Check : DecodedMode::On;
    }();
    return mode;
}

u64 SteadyNs() {
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

// The decoded programs: one array of records for each program array the engine is given (a unit's PicaState words: the
// vertex and the geometry programs), the least recently used one given to a new array. A record holds only while its
// words are the program's (checked at every step), so a slot handed to another array decodes as it goes.
struct InterpreterEngine::DecodedCache {
    struct Slot {
        const u32* code = nullptr;
        u64 used = 0;
        std::unique_ptr<DecodedOp[]> ops;
    };
    std::array<Slot, 2> slots;
    u64 clock = 0;

    DecodedOp* For(const u32* code) {
        Slot* oldest = &slots[0];
        for (Slot& s : slots) {
            if (s.code == code && s.ops != nullptr) {
                s.used = ++clock;
                return s.ops.get();
            }
            if (s.used < oldest->used) oldest = &s;
        }
        if (oldest->ops == nullptr) {
            oldest->ops = std::make_unique<DecodedOp[]>(kMaxProgramCodeLength);
        }
        oldest->code = code;
        oldest->used = ++clock;
        return oldest->ops.get();
    }
};

InterpreterEngine::InterpreterEngine() = default;
InterpreterEngine::~InterpreterEngine() = default;

DecodedCheckCounts GetDecodedCheckCounts() {
    return {g_decoded_runs.load(std::memory_order_relaxed), g_decoded_differences.load(std::memory_order_relaxed),
            g_decoded_plain_ns.load(std::memory_order_relaxed), g_decoded_fast_ns.load(std::memory_order_relaxed)};
}

void SetDecodedInterpreterOff(bool off) {
    g_decoded_off.store(off, std::memory_order_relaxed);
}

void SetDecodedVectorsOff(bool off) {
    g_decoded_scalar.store(off, std::memory_order_relaxed);
}

DecodedWork TakeDecodedWork() {
    return {g_work_steps.exchange(0, std::memory_order_relaxed), g_work_decodes.exchange(0, std::memory_order_relaxed)};
}

bool InterpreterEngine::InterpretDecoded(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const {
    error = {};
    // SetupBatch: ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH).
    if (setup.entry_point >= kMaxProgramCodeLength) {
        error = {Unhandled::EntryPoint, setup.entry_point, 0};
        return false;
    }
    if (decoded_ == nullptr) {
        decoded_ = std::make_unique<DecodedCache>();
    }
    DecodedOp* const ops = decoded_->For(setup.program_code);

    CircularStack<IfStackElement, 8> if_stack;
    CircularStack<CallStackElement, 4> call_stack;
    CircularStack<LoopStackElement, 4> loop_stack;
    u32 program_counter = setup.entry_point;

    const auto do_if = [&](Instruction instr, bool condition) {
        if (condition) {
            if_stack.push_back({
                instr.flow_dest_offset(),
                instr.flow_dest_offset() + instr.flow_num_instructions(),
            });
        } else {
            program_counter = instr.flow_dest_offset() - 1;
        }
    };

    const auto do_call = [&](Instruction instr) {
        call_stack.push_back({
            instr.flow_dest_offset() + instr.flow_num_instructions(),
            program_counter + 1,
        });
        program_counter = instr.flow_dest_offset() - 1;
    };

    const auto do_loop = [&](Instruction instr, const std::array<u8, 4>& loop_param) {
        const u8 previous_aL = static_cast<u8>(state.address_registers[2]);
        loop_stack.push_back({
            program_counter + 1,
            instr.flow_dest_offset() + 1,
            loop_param[0],
            loop_param[2],
            previous_aL,
        });
        state.address_registers[2] = loop_param[1];
    };

    auto evaluate_condition = [&state](Instruction instr) {
        bool result_x = instr.flow_refx() == state.conditional_code[0];
        bool result_y = instr.flow_refy() == state.conditional_code[1];

        switch (instr.flow_op()) {
        case FlowOp::Or:
            return result_x || result_y;
        case FlowOp::And:
            return result_x && result_y;
        case FlowOp::JustX:
            return result_x;
        case FlowOp::JustY:
            return result_y;
        }
        return false; // unreachable: the field has two bits
    };

    const Uniforms& uniforms = *setup.uniforms;
    const u32* swizzle_data = setup.swizzle_data;
    const u32* program_code = setup.program_code;
    static const DecodedOp kEnd = Decode(static_cast<u32>(Opcode::END) << 26, kDecodedNoSwizzles);

    u32 iteration = 0;
    // (the work counted once a run: TakeDecodedWork)
    struct Work {
        const u32& steps;
        u32 decodes = 0;
        ~Work() {
            g_work_steps.fetch_add(steps, std::memory_order_relaxed);
            if (decodes != 0) g_work_decodes.fetch_add(decodes, std::memory_order_relaxed);
        }
    } work{iteration};
    bool should_stop = false;
    while (!should_stop) {
        bool is_break = false;
        const u32 old_program_counter = program_counter;

        if (iteration >= kMaxInstructions) {
            error = {Unhandled::InstructionBudget, old_program_counter, 0};
            return false;
        }

        // (Interpret: the last word of the program memory is always END)
        const DecodedOp* op;
        if (program_counter < kMaxProgramCodeLength - 1) {
            const u32 hex = program_code[program_counter];
            DecodedOp& d = ops[program_counter];
            if (!d.valid || d.hex != hex || swizzle_data[d.desc] != d.swz) [[unlikely]] {
                d = Decode(hex, swizzle_data);
                ++work.decodes;
            }
            op = &d;
        } else {
            op = &kEnd;
        }
        const DecodedOp& d = *op;

#if MH4N_DECODED_NEON
        if (d.exec <= Exec::Mad && d.exec != Exec::BadArithmetic && d.exec != Exec::Rcp && d.exec != Exec::Rsq &&
            d.exec != Exec::Mova && d.exec != Exec::Cmp && d.exec != Exec::Ex2 && d.exec != Exec::Lg2 &&
            !g_decoded_scalar.load(std::memory_order_relaxed)) {
            // The same handlers four lanes at once (each lane the scalar handler's operation on that lane, the lanes the
            // mask does not enable left as they were), sources first as below
            const float32x4_t a = VLoad(d, 0, SourceOf(d.src[0], state, uniforms));
            const float32x4_t b = VLoad(d, 1, SourceOf(d.src[1], state, uniforms));
            f24* dest = d.dest_output ? &state.output[state.output_bank][d.dest_index].c[0]
                                      : &state.temporary[d.dest_index].c[0];
            switch (d.exec) {
            case Exec::Add:
                VStore(dest, d, vaddq_f32(a, b));
                break;
            case Exec::Mul:
                VStore(dest, d, VMul(a, b));
                break;
            case Exec::Flr:
                VStore(dest, d, vrndmq_f32(a));
                break;
            case Exec::Max:
                VStore(dest, d, vbslq_f32(vcgtq_f32(a, b), a, b));
                break;
            case Exec::Min:
                VStore(dest, d, vbslq_f32(vcltq_f32(a, b), a, b));
                break;
            case Exec::Mov:
                VStore(dest, d, a);
                break;
            case Exec::Sge:
                VStore(dest, d, vbslq_f32(vcgeq_f32(a, b), vdupq_n_f32(1.0f), vdupq_n_f32(0.0f)));
                break;
            case Exec::Slt:
                VStore(dest, d, vbslq_f32(vcltq_f32(a, b), vdupq_n_f32(1.0f), vdupq_n_f32(0.0f)));
                break;
            case Exec::Mad: {
                const float32x4_t c = VLoad(d, 2, SourceOf(d.src[2], state, uniforms));
                VStore(dest, d, vaddq_f32(VMul(a, b), c));
                break;
            }
            default: {  // Dp3, Dp4, Dph: the products at once, summed in Interpret's order from +0
                const float32x4_t p = VMul(d.exec == Exec::Dph ? vsetq_lane_f32(1.0f, a, 3) : a, b);
                f24 dot = f24::Zero();
                dot = dot + f24::FromFloat32(vgetq_lane_f32(p, 0));
                dot = dot + f24::FromFloat32(vgetq_lane_f32(p, 1));
                dot = dot + f24::FromFloat32(vgetq_lane_f32(p, 2));
                if (d.exec != Exec::Dp3) {
                    dot = dot + f24::FromFloat32(vgetq_lane_f32(p, 3));
                }
                VStore(dest, d, vdupq_n_f32(dot.ToFloat32()));
                break;
            }
            }
        } else
#endif
        if (d.exec <= Exec::Mad) {
            // Arithmetic and multiply-add: the sources' lanes first (a destination may be a source), then the handler.
            f24 src1[4], src2[4], src3[4];
            Load(d.src[0], SourceOf(d.src[0], state, uniforms), src1);
            Load(d.src[1], SourceOf(d.src[1], state, uniforms), src2);
            if (d.exec == Exec::Mad) {
                Load(d.src[2], SourceOf(d.src[2], state, uniforms), src3);
            }
            f24* dest = d.dest_output ? &state.output[state.output_bank][d.dest_index].c[0]
                                      : &state.temporary[d.dest_index].c[0];
            const u32 mask = d.mask;
            switch (d.exec) {
            case Exec::Add:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = src1[i] + src2[i];
                }
                break;
            case Exec::Mul:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = src1[i] * src2[i];
                }
                break;
            case Exec::Flr:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = f24::FromFloat32(std::floor(src1[i].ToFloat32()));
                }
                break;
            case Exec::Max:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = (src1[i] > src2[i]) ? src1[i] : src2[i];
                }
                break;
            case Exec::Min:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = (src1[i] < src2[i]) ? src1[i] : src2[i];
                }
                break;
            case Exec::Dp3:
            case Exec::Dp4:
            case Exec::Dph: {
                if (d.exec == Exec::Dph) src1[3] = f24::One();
                const int num_components = d.exec == Exec::Dp3 ? 3 : 4;
                f24 dot = f24::Zero();
                for (int i = 0; i < num_components; ++i) {
                    dot = dot + src1[i] * src2[i];
                }
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = dot;
                }
                break;
            }
            case Exec::Rcp: {
                const f24 rcp_res = f24::FromFloat32(1.0f / src1[0].ToFloat32());
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = rcp_res;
                }
                break;
            }
            case Exec::Rsq: {
                const f24 rsq_res = f24::FromFloat32(1.0f / std::sqrt(src1[0].ToFloat32()));
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = rsq_res;
                }
                break;
            }
            case Exec::Mova:
                for (int i = 0; i < 2; ++i) {
                    if (mask & (1u << i)) state.address_registers[i] = FloatToS32(src1[i].ToFloat32());
                }
                break;
            case Exec::Mov:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = src1[i];
                }
                break;
            case Exec::Sge:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = (src1[i] >= src2[i]) ? f24::One() : f24::Zero();
                }
                break;
            case Exec::Slt:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = (src1[i] < src2[i]) ? f24::One() : f24::Zero();
                }
                break;
            case Exec::Cmp:
                for (int i = 0; i < 2; ++i) {
                    switch (static_cast<CompareOp>(d.compare[i])) {
                    case CompareOp::Equal:
                        state.conditional_code[i] = (src1[i] == src2[i]);
                        break;
                    case CompareOp::NotEqual:
                        state.conditional_code[i] = (src1[i] != src2[i]);
                        break;
                    case CompareOp::LessThan:
                        state.conditional_code[i] = (src1[i] < src2[i]);
                        break;
                    case CompareOp::LessEqual:
                        state.conditional_code[i] = (src1[i] <= src2[i]);
                        break;
                    case CompareOp::GreaterThan:
                        state.conditional_code[i] = (src1[i] > src2[i]);
                        break;
                    case CompareOp::GreaterEqual:
                        state.conditional_code[i] = (src1[i] >= src2[i]);
                        break;
                    default:
                        error = {Unhandled::CompareMode, old_program_counter, d.hex};
                        return false;
                    }
                }
                break;
            case Exec::Ex2: {
                const f24 ex2_res = f24::FromFloat32(std::exp2(src1[0].ToFloat32()));
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = ex2_res;
                }
                break;
            }
            case Exec::Lg2: {
                const f24 lg2_res = f24::FromFloat32(std::log2(src1[0].ToFloat32()));
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = lg2_res;
                }
                break;
            }
            case Exec::Mad:
                for (int i = 0; i < 4; ++i) {
                    if (mask & (1u << i)) dest[i] = src1[i] * src2[i] + src3[i];
                }
                break;
            default:  // Exec::BadArithmetic
                error = {Unhandled::Instruction, old_program_counter, d.hex};
                return false;
            }
        } else {
            const Instruction instr{d.hex};
            switch (d.exec) {
            case Exec::End:
                should_stop = true;
                break;

            case Exec::Jmpc:
                if (evaluate_condition(instr)) {
                    program_counter = instr.flow_dest_offset() - 1;
                }
                break;

            case Exec::Jmpu:
                if (uniforms.b[instr.flow_bool_uniform_id()] == !(instr.flow_num_instructions() & 1)) {
                    program_counter = instr.flow_dest_offset() - 1;
                }
                break;

            case Exec::Call:
                do_call(instr);
                break;

            case Exec::Callu:
                if (uniforms.b[instr.flow_bool_uniform_id()]) {
                    do_call(instr);
                }
                break;

            case Exec::Callc:
                if (evaluate_condition(instr)) {
                    do_call(instr);
                }
                break;

            case Exec::Nop:
                break;

            case Exec::Ifu: {
                const bool cond = uniforms.b[instr.flow_bool_uniform_id()];
                do_if(instr, cond);
                break;
            }

            case Exec::Ifc: {
                const bool cond = evaluate_condition(instr);
                do_if(instr, cond);
                break;
            }

            case Exec::Loop: {
                const std::array<u8, 4>& loop_param = uniforms.i[instr.flow_int_uniform_id()];
                state.address_registers[2] = loop_param[1];
                do_loop(instr, loop_param);
                break;
            }

            case Exec::Break:
                is_break = true;
                break;

            case Exec::Breakc:
                if (evaluate_condition(instr)) {
                    is_break = true;
                }
                break;

            case Exec::Emit: {
                auto* emitter = state.emitter_ptr;
                if (emitter == nullptr) {
                    error = {Unhandled::EmitOnVertexShader, old_program_counter, instr.hex};
                    return false;
                }
                if (emitter->emit_state.vertex_id >= 3) {
                    error = {Unhandled::EmitVertexId, old_program_counter, instr.hex};
                    return false;
                }
                emitter->Emit(state.output[state.output_bank]);
                state.output_bank = !state.output_bank;
                break;
            }

            case Exec::SetEmit: {
                auto* emitter = state.emitter_ptr;
                if (emitter == nullptr) {
                    error = {Unhandled::EmitOnVertexShader, old_program_counter, instr.hex};
                    return false;
                }
                emitter->emit_state.vertex_id = static_cast<u8>(instr.setemit_vertex_id());
                emitter->emit_state.prim_emit = instr.setemit_prim_emit();
                emitter->emit_state.winding = instr.setemit_winding();
                break;
            }

            default:  // Exec::BadOther
                error = {Unhandled::Instruction, old_program_counter, instr.hex};
                return false;
            }
        }

        ++program_counter;
        ++iteration;

        // (Interpret's own: the stacks, CALL -> IF -> LOOP)
        u32 next_program_counter = old_program_counter + 1;
        for (u32 i = 0; i < 4; i++) {
            if (call_stack.empty() || call_stack.back().end_address != next_program_counter)
                break;
            if (i < 3) {
                program_counter = call_stack.back().return_address;
                next_program_counter = program_counter;
            }
            call_stack.pop_back();
        }

        if (!if_stack.empty() && if_stack.back().else_address == old_program_counter + 1) {
            program_counter = if_stack.back().end_address;
            if_stack.pop_back();
        }

        if (!loop_stack.empty() &&
            (loop_stack.back().end_address == old_program_counter + 1 || is_break)) {
            auto& loop = loop_stack.back();
            state.address_registers[2] += loop.address_increment;
            if (!is_break && loop.loop_downcounter--) {
                program_counter = loop.entry_address;
            } else {
                program_counter = loop.end_address;
                if (loop_stack.size() > 1)
                    state.address_registers[2] = loop.previous_aL;
                loop_stack.pop_back();
            }
        }
    }
    return true;
}

// MH4N_PICA_DECODED=check: Interpret for real (its vertices go on to the sink), InterpretDecoded on a copy of the unit and
// of its emitter, the two compared as RunChecked compares a compiled program with the interpreter.
bool InterpreterEngine::InterpretBoth(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const {
    ShaderUnit before = state;
    GeometryEmitter emitter_before;
    GeometryEmitter* const emitter = state.emitter_ptr;
    if (emitter != nullptr) {
        emitter_before = *emitter;
    }
    RecordingSink plain(emitter != nullptr ? emitter->sink : nullptr);
    if (emitter != nullptr) {
        emitter->sink = &plain;
    }
    const u64 t0 = SteadyNs();
    const bool ok = Interpret(setup, state, error);
    const u64 t1 = SteadyNs();
    if (emitter != nullptr) {
        emitter->sink = emitter_before.sink;
    }
    RecordingSink decoded(nullptr);
    GeometryEmitter emitter_copy = emitter_before;
    emitter_copy.sink = &decoded;
    ShaderUnit copy = before;
    copy.emitter_ptr = emitter != nullptr ? &emitter_copy : nullptr;
    RunError decoded_error;
    const u64 t2 = SteadyNs();
    const bool decoded_ok = InterpretDecoded(setup, copy, decoded_error);
    const u64 t3 = SteadyNs();

    bool same = ok == decoded_ok && error.kind == decoded_error.kind &&
                error.program_counter == decoded_error.program_counter && error.instruction == decoded_error.instruction &&
                std::memcmp(state.address_registers, copy.address_registers, sizeof(copy.address_registers)) == 0 &&
                state.conditional_code[0] == copy.conditional_code[0] &&
                state.conditional_code[1] == copy.conditional_code[1] &&
                SameBits(state.temporary.data(), copy.temporary.data(), sizeof(copy.temporary)) &&
                SameBits(state.output.data(), copy.output.data(), sizeof(copy.output)) &&
                state.output_bank == copy.output_bank && plain.vertices.size() == decoded.vertices.size() &&
                plain.windings == decoded.windings;
    for (size_t i = 0; same && i < decoded.vertices.size(); ++i) {
        same = SameBits(plain.vertices[i].data(), decoded.vertices[i].data(), sizeof(AttributeBuffer));
    }
    if (same && emitter != nullptr) {
        same = emitter->emit_state.vertex_id == emitter_copy.emit_state.vertex_id &&
               emitter->emit_state.prim_emit == emitter_copy.emit_state.prim_emit &&
               emitter->emit_state.winding == emitter_copy.emit_state.winding &&
               SameBits(emitter->buffer.data(), emitter_copy.buffer.data(), sizeof(emitter_copy.buffer));
    }
    const u64 runs = g_decoded_runs.fetch_add(1, std::memory_order_relaxed) + 1;
    const u64 plain_ns = g_decoded_plain_ns.fetch_add(t1 - t0, std::memory_order_relaxed) + (t1 - t0);
    const u64 fast_ns = g_decoded_fast_ns.fetch_add(t3 - t2, std::memory_order_relaxed) + (t3 - t2);
    u64 differences = g_decoded_differences.load(std::memory_order_relaxed);
    if (!same) {
        differences = g_decoded_differences.fetch_add(1, std::memory_order_relaxed) + 1;
        if (differences <= 8) {
            MH4N_LOG(Warning, "CpuVertex", "the decoded interpreter differs from the word-by-word one (entry {}): ok {}/{}, "
                     "{}/{} vertices", setup.entry_point, ok, decoded_ok, plain.vertices.size(), decoded.vertices.size());
        }
    }
    if ((runs & (runs - 1)) == 0 && runs >= 1024) {  // (at 1024, 2048, 4096, ... runs)
        MH4N_LOG(Info, "CpuVertex", "decoded interpreter check: {} runs, {} differ; the word-by-word one {} ms, the decoded "
                 "one {} ms", runs, differences, plain_ns / 1000000, fast_ns / 1000000);
    }
    return ok;
}

NativeCheckCounts GetNativeCheckCounts() {
    return {g_check_runs.load(std::memory_order_relaxed), g_check_differences.load(std::memory_order_relaxed)};
}

bool InterpreterEngine::Run(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const {
    if (setup.native == nullptr) {
        switch (DecodedModeOf()) {
        case DecodedMode::Off:
            return Interpret(setup, state, error);
        case DecodedMode::Check:
            return InterpretBoth(setup, state, error);
        case DecodedMode::On:
            break;
        }
        return g_decoded_off.load(std::memory_order_relaxed) ? Interpret(setup, state, error)
                                                             : InterpretDecoded(setup, state, error);
    }
    return setup.check_native ? RunChecked(setup, state, error) : RunNative(setup, state, error);
}

bool InterpreterEngine::RunNative(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const {
    error = {};
    NativeContext context{&state, Unhandled::None};
    PicaNativeState s;
    s.input = reinterpret_cast<float(*)[4]>(state.input.data());
    s.temporary = reinterpret_cast<float(*)[4]>(state.temporary.data());
    s.output = reinterpret_cast<float(*)[16][4]>(state.output.data());
    s.output_bank = reinterpret_cast<unsigned char*>(&state.output_bank);
    s.address = reinterpret_cast<int*>(state.address_registers);
    s.conditional = reinterpret_cast<unsigned char*>(state.conditional_code);
    s.uniform_f = reinterpret_cast<const float(*)[4]>(setup.uniforms->f.data());
    s.uniform_b = reinterpret_cast<const unsigned char*>(setup.uniforms->b.data());
    s.uniform_i = reinterpret_cast<const unsigned char(*)[4]>(setup.uniforms->i.data());
    s.emit = NativeEmit;
    s.setemit = NativeSetEmit;
    s.exp2 = NativeExp2;
    s.log2 = NativeLog2;
    s.ctx = &context;
    const int result = setup.native(&s);
    if (result == 1) {
        return true;
    }
    const u32 pc = static_cast<u32>(-result - 1);
    error = {context.failure != Unhandled::None ? context.failure : Unhandled::Instruction, pc,
             pc < kMaxProgramCodeLength ? setup.program_code[pc] : 0};
    return false;
}

bool InterpreterEngine::RunChecked(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const {
    // the unit and its emitter as they are before the run
    ShaderUnit before = state;
    GeometryEmitter emitter_before;
    GeometryEmitter* const emitter = state.emitter_ptr;
    if (emitter != nullptr) {
        emitter_before = *emitter;
    }
    // the interpreter, for real (its vertices go on to the sink), kept
    RecordingSink interpreted(emitter != nullptr ? emitter->sink : nullptr);
    if (emitter != nullptr) {
        emitter->sink = &interpreted;
    }
    const bool ok = Interpret(setup, state, error);
    if (emitter != nullptr) {
        emitter->sink = emitter_before.sink;
    }
    // the compiled program on the copy
    RecordingSink compiled(nullptr);
    GeometryEmitter emitter_copy = emitter_before;
    emitter_copy.sink = &compiled;
    ShaderUnit copy = before;
    copy.emitter_ptr = emitter != nullptr ? &emitter_copy : nullptr;
    RunError native_error;
    const bool native_ok = RunNative(setup, copy, native_error);

    bool same = ok == native_ok &&
                std::memcmp(state.address_registers, copy.address_registers, sizeof(copy.address_registers)) == 0 &&
                state.conditional_code[0] == copy.conditional_code[0] &&
                state.conditional_code[1] == copy.conditional_code[1] &&
                SameBits(state.temporary.data(), copy.temporary.data(), sizeof(copy.temporary)) &&
                SameBits(state.output.data(), copy.output.data(), sizeof(copy.output)) &&
                state.output_bank == copy.output_bank && interpreted.vertices.size() == compiled.vertices.size() &&
                interpreted.windings == compiled.windings;
    for (size_t i = 0; same && i < compiled.vertices.size(); ++i) {
        same = SameBits(interpreted.vertices[i].data(), compiled.vertices[i].data(), sizeof(AttributeBuffer));
    }
    if (same && emitter != nullptr) {
        same = emitter->emit_state.vertex_id == emitter_copy.emit_state.vertex_id &&
               emitter->emit_state.prim_emit == emitter_copy.emit_state.prim_emit &&
               emitter->emit_state.winding == emitter_copy.emit_state.winding &&
               SameBits(emitter->buffer.data(), emitter_copy.buffer.data(), sizeof(emitter_copy.buffer));
    }
    g_check_runs.fetch_add(1, std::memory_order_relaxed);
    if (!same) {
        const u64 n = g_check_differences.fetch_add(1, std::memory_order_relaxed);
        if (n < 8) {
            MH4N_LOG(Warning, "CpuVertex", "a compiled shader program (entry {}) differs from the interpreter: ok {}/{}, {}/{} "
                     "vertices", setup.entry_point, ok, native_ok, interpreted.vertices.size(), compiled.vertices.size());
        }
    }
    return ok;
}

bool InterpreterEngine::Interpret(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const {
    error = {};
    // SetupBatch: ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH).
    if (setup.entry_point >= kMaxProgramCodeLength) {
        error = {Unhandled::EntryPoint, setup.entry_point, 0};
        return false;
    }

    CircularStack<IfStackElement, 8> if_stack;
    CircularStack<CallStackElement, 4> call_stack;
    CircularStack<LoopStackElement, 4> loop_stack;
    u32 program_counter = setup.entry_point;

    const auto do_if = [&](Instruction instr, bool condition) {
        if (condition) {
            if_stack.push_back({
                instr.flow_dest_offset(),
                instr.flow_dest_offset() + instr.flow_num_instructions(),
            });
        } else {
            program_counter = instr.flow_dest_offset() - 1;
        }
    };

    const auto do_call = [&](Instruction instr) {
        call_stack.push_back({
            instr.flow_dest_offset() + instr.flow_num_instructions(),
            program_counter + 1,
        });
        program_counter = instr.flow_dest_offset() - 1;
    };

    const auto do_loop = [&](Instruction instr, const std::array<u8, 4>& loop_param) {
        const u8 previous_aL = static_cast<u8>(state.address_registers[2]);
        loop_stack.push_back({
            program_counter + 1,
            instr.flow_dest_offset() + 1,
            loop_param[0],
            loop_param[2],
            previous_aL,
        });
        state.address_registers[2] = loop_param[1];
    };

    auto evaluate_condition = [&state](Instruction instr) {
        bool result_x = instr.flow_refx() == state.conditional_code[0];
        bool result_y = instr.flow_refy() == state.conditional_code[1];

        switch (instr.flow_op()) {
        case FlowOp::Or:
            return result_x || result_y;
        case FlowOp::And:
            return result_x && result_y;
        case FlowOp::JustX:
            return result_x;
        case FlowOp::JustY:
            return result_y;
        }
        return false; // unreachable: the field has two bits
    };

    const Uniforms& uniforms = *setup.uniforms;
    const u32* swizzle_data = setup.swizzle_data;
    const u32* program_code = setup.program_code;

    // Constants for handling invalid inputs. In Azahar these are static arrays that a write to an
    // invalid destination would modify; the destination field has five bits, so none exists, and
    // these are constant here.
    static const f24 dummy_vec4_float24_zeros[4] = {f24::Zero(), f24::Zero(), f24::Zero(), f24::Zero()};
    static const f24 dummy_vec4_float24_ones[4] = {f24::One(), f24::One(), f24::One(), f24::One()};

    u32 iteration = 0;
    bool should_stop = false;
    while (!should_stop) {
        bool is_break = false;
        const u32 old_program_counter = program_counter;

        if (iteration >= kMaxInstructions) {
            error = {Unhandled::InstructionBudget, old_program_counter, 0};
            return false;
        }

        // Always treat the last instruction of the program code as an end instruction. This fixes
        // some games such as Thunder Blade or After Burner II which have malformed geo shaders
        // without an end instruction crashing the emulator due to the program counter growing
        // uncontrollably. (Azahar's comment; its TODO: find how real hardware reacts.)
        Instruction instr{};
        if (program_counter < kMaxProgramCodeLength - 1) {
            instr.hex = program_code[program_counter];
        } else {
            instr.hex = static_cast<u32>(Opcode::END) << 26;
        }

        const SwizzlePattern swizzle = {swizzle_data[instr.common_operand_desc_id()]};

        auto LookupSourceRegister = [&](const isa::SourceRegister& source_reg,
                                        int address_register_index) -> const f24* {
            int index = source_reg.GetIndex();
            switch (source_reg.GetRegisterType()) {
            case RegisterType::Input:
                return &state.input[static_cast<size_t>(index)].c[0];

            case RegisterType::Temporary:
                return &state.temporary[static_cast<size_t>(index)].c[0];

            case RegisterType::FloatUniform:
                if (address_register_index != 0) {
                    int offset = state.address_registers[address_register_index - 1];
                    if (offset < std::numeric_limits<s8>::min() ||
                        offset > std::numeric_limits<s8>::max()) [[unlikely]] {
                        offset = 0;
                    }
                    index = (index + offset) & 0x7F;
                    // If the index is above 96, the result is all one.
                    if (index >= 96) [[unlikely]] {
                        return dummy_vec4_float24_ones;
                    }
                }
                return &uniforms.f[static_cast<size_t>(index)].c[0];

            default:
                return dummy_vec4_float24_zeros;
            }
        };

        switch (instr.GetInfo().type) {
        case Type::Arithmetic: {
            const bool is_inverted = instr.GetInfo().src_inversed;

            const f24* src1_ = LookupSourceRegister(
                instr.common_src1(is_inverted),
                !is_inverted * static_cast<int>(instr.common_address_register_index()));
            const f24* src2_ = LookupSourceRegister(
                instr.common_src2(is_inverted),
                is_inverted * static_cast<int>(instr.common_address_register_index()));

            const bool negate_src1 = swizzle.negate_src1();
            const bool negate_src2 = swizzle.negate_src2();

            f24 src1[4] = {
                src1_[swizzle.selector_src1(0)],
                src1_[swizzle.selector_src1(1)],
                src1_[swizzle.selector_src1(2)],
                src1_[swizzle.selector_src1(3)],
            };
            if (negate_src1) {
                src1[0] = -src1[0];
                src1[1] = -src1[1];
                src1[2] = -src1[2];
                src1[3] = -src1[3];
            }
            f24 src2[4] = {
                src2_[swizzle.selector_src2(0)],
                src2_[swizzle.selector_src2(1)],
                src2_[swizzle.selector_src2(2)],
                src2_[swizzle.selector_src2(3)],
            };
            if (negate_src2) {
                src2[0] = -src2[0];
                src2[1] = -src2[1];
                src2[2] = -src2[2];
                src2[3] = -src2[3];
            }

            // Deviation: Azahar's `dest` can also point at a dummy array for a register value of
            // 0x20 and above; the field is five bits wide, so that arm cannot be taken.
            f24* dest = instr.common_dest().IsOutput()
                            ? &state.output[state.output_bank][static_cast<size_t>(instr.common_dest().GetIndex())].c[0]
                            : &state.temporary[static_cast<size_t>(instr.common_dest().GetIndex())].c[0];

            switch (instr.EffectiveOpCode()) {
            case Opcode::ADD: {
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = src1[i] + src2[i];
                }
                break;
            }

            case Opcode::MUL: {
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = src1[i] * src2[i];
                }
                break;
            }

            case Opcode::FLR:
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = f24::FromFloat32(std::floor(src1[i].ToFloat32()));
                }
                break;

            case Opcode::MAX:
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    // NOTE: Exact form required to match NaN semantics to hardware:
                    //   max(0, NaN) -> NaN
                    //   max(NaN, 0) -> 0
                    dest[i] = (src1[i] > src2[i]) ? src1[i] : src2[i];
                }
                break;

            case Opcode::MIN:
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    // NOTE: Exact form required to match NaN semantics to hardware:
                    //   min(0, NaN) -> NaN
                    //   min(NaN, 0) -> 0
                    dest[i] = (src1[i] < src2[i]) ? src1[i] : src2[i];
                }
                break;

            case Opcode::DP3:
            case Opcode::DP4:
            case Opcode::DPH:
            case Opcode::DPHI: {
                const Opcode opcode = instr.EffectiveOpCode();
                if (opcode == Opcode::DPH || opcode == Opcode::DPHI)
                    src1[3] = f24::One();

                const int num_components = (opcode == Opcode::DP3) ? 3 : 4;
                // std::inner_product(src1, src1 + n, src2, f24::Zero()): init = init + a * b
                f24 dot = f24::Zero();
                for (int i = 0; i < num_components; ++i) {
                    dot = dot + src1[i] * src2[i];
                }

                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = dot;
                }
                break;
            }

            // Reciprocal
            case Opcode::RCP: {
                f24 rcp_res = f24::FromFloat32(1.0f / src1[0].ToFloat32());
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = rcp_res;
                }
                break;
            }

            // Reciprocal Square Root
            case Opcode::RSQ: {
                f24 rsq_res = f24::FromFloat32(1.0f / std::sqrt(src1[0].ToFloat32()));
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = rsq_res;
                }
                break;
            }

            case Opcode::MOVA: {
                for (int i = 0; i < 2; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    // TODO (Azahar): Figure out how the rounding is done on hardware
                    state.address_registers[i] = FloatToS32(src1[i].ToFloat32());
                }
                break;
            }

            case Opcode::MOV: {
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = src1[i];
                }
                break;
            }

            case Opcode::SGE:
            case Opcode::SGEI:
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = (src1[i] >= src2[i]) ? f24::One() : f24::Zero();
                }
                break;

            case Opcode::SLT:
            case Opcode::SLTI:
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = (src1[i] < src2[i]) ? f24::One() : f24::Zero();
                }
                break;

            case Opcode::CMP:
                for (int i = 0; i < 2; ++i) {
                    // TODO (Azahar): Can you restrict to one compare via dest masking?

                    const CompareOp op = (i == 0) ? instr.common_compare_x() : instr.common_compare_y();

                    switch (op) {
                    case CompareOp::Equal:
                        state.conditional_code[i] = (src1[i] == src2[i]);
                        break;

                    case CompareOp::NotEqual:
                        state.conditional_code[i] = (src1[i] != src2[i]);
                        break;

                    case CompareOp::LessThan:
                        state.conditional_code[i] = (src1[i] < src2[i]);
                        break;

                    case CompareOp::LessEqual:
                        state.conditional_code[i] = (src1[i] <= src2[i]);
                        break;

                    case CompareOp::GreaterThan:
                        state.conditional_code[i] = (src1[i] > src2[i]);
                        break;

                    case CompareOp::GreaterEqual:
                        state.conditional_code[i] = (src1[i] >= src2[i]);
                        break;

                    default:
                        // Deviation: Azahar logs "Unknown compare mode" and leaves the code as it
                        // is; the draw is dropped here.
                        error = {Unhandled::CompareMode, old_program_counter, instr.hex};
                        return false;
                    }
                }
                break;

            case Opcode::EX2: {
                // EX2 only takes first component exp2 and writes it to all dest components
                f24 ex2_res = f24::FromFloat32(std::exp2(src1[0].ToFloat32()));
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = ex2_res;
                }
                break;
            }

            case Opcode::LG2: {
                // LG2 only takes the first component log2 and writes it to all dest components
                f24 lg2_res = f24::FromFloat32(std::log2(src1[0].ToFloat32()));
                for (int i = 0; i < 4; ++i) {
                    if (!swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = lg2_res;
                }
                break;
            }

            default:
                // Azahar: LOG_ERROR "Unhandled arithmetic instruction" (DST, DSTI and LIT have no
                // handler in its interpreter) and goes on. Deviation: the draw is dropped.
                error = {Unhandled::Instruction, old_program_counter, instr.hex};
                return false;
            }

            break;
        }

        case Type::MultiplyAdd: {
            if ((instr.EffectiveOpCode() == Opcode::MAD) || (instr.EffectiveOpCode() == Opcode::MADI)) {
                const SwizzlePattern mad_swizzle = {swizzle_data[instr.mad_operand_desc_id()]};

                bool is_inverted = (instr.EffectiveOpCode() == Opcode::MADI);

                const f24* src1_ = LookupSourceRegister(instr.mad_src1(is_inverted), 0);
                const f24* src2_ = LookupSourceRegister(
                    instr.mad_src2(is_inverted),
                    !is_inverted * static_cast<int>(instr.mad_address_register_index()));
                const f24* src3_ = LookupSourceRegister(
                    instr.mad_src3(is_inverted),
                    is_inverted * static_cast<int>(instr.mad_address_register_index()));

                const bool negate_src1 = mad_swizzle.negate_src1();
                const bool negate_src2 = mad_swizzle.negate_src2();
                const bool negate_src3 = mad_swizzle.negate_src3();

                f24 src1[4] = {
                    src1_[mad_swizzle.selector_src1(0)],
                    src1_[mad_swizzle.selector_src1(1)],
                    src1_[mad_swizzle.selector_src1(2)],
                    src1_[mad_swizzle.selector_src1(3)],
                };
                if (negate_src1) {
                    src1[0] = -src1[0];
                    src1[1] = -src1[1];
                    src1[2] = -src1[2];
                    src1[3] = -src1[3];
                }
                f24 src2[4] = {
                    src2_[mad_swizzle.selector_src2(0)],
                    src2_[mad_swizzle.selector_src2(1)],
                    src2_[mad_swizzle.selector_src2(2)],
                    src2_[mad_swizzle.selector_src2(3)],
                };
                if (negate_src2) {
                    src2[0] = -src2[0];
                    src2[1] = -src2[1];
                    src2[2] = -src2[2];
                    src2[3] = -src2[3];
                }
                f24 src3[4] = {
                    src3_[mad_swizzle.selector_src3(0)],
                    src3_[mad_swizzle.selector_src3(1)],
                    src3_[mad_swizzle.selector_src3(2)],
                    src3_[mad_swizzle.selector_src3(3)],
                };
                if (negate_src3) {
                    src3[0] = -src3[0];
                    src3[1] = -src3[1];
                    src3[2] = -src3[2];
                    src3[3] = -src3[3];
                }

                f24* dest = instr.mad_dest().IsOutput()
                                ? &state.output[state.output_bank][static_cast<size_t>(instr.mad_dest().GetIndex())].c[0]
                                : &state.temporary[static_cast<size_t>(instr.mad_dest().GetIndex())].c[0];

                for (int i = 0; i < 4; ++i) {
                    if (!mad_swizzle.DestComponentEnabled(i))
                        continue;

                    dest[i] = src1[i] * src2[i] + src3[i];
                }
            } else {
                error = {Unhandled::Instruction, old_program_counter, instr.hex};
                return false;
            }
            break;
        }

        default: {
            // Handle each instruction on its own
            switch (static_cast<Opcode>(instr.opcode())) {
            case Opcode::END:
                should_stop = true;
                break;

            case Opcode::JMPC:
                if (evaluate_condition(instr)) {
                    program_counter = instr.flow_dest_offset() - 1;
                }
                break;

            case Opcode::JMPU:
                if (uniforms.b[instr.flow_bool_uniform_id()] == !(instr.flow_num_instructions() & 1)) {
                    program_counter = instr.flow_dest_offset() - 1;
                }
                break;

            case Opcode::CALL:
                do_call(instr);
                break;

            case Opcode::CALLU:
                if (uniforms.b[instr.flow_bool_uniform_id()]) {
                    do_call(instr);
                }
                break;

            case Opcode::CALLC:
                if (evaluate_condition(instr)) {
                    do_call(instr);
                }
                break;

            case Opcode::NOP:
                break;

            case Opcode::IFU: {
                const bool cond = uniforms.b[instr.flow_bool_uniform_id()];
                do_if(instr, cond);
                break;
            }

            case Opcode::IFC: {
                // TODO (Azahar): Do we need to consider swizzlers here?
                const bool cond = evaluate_condition(instr);
                do_if(instr, cond);
                break;
            }

            case Opcode::LOOP: {
                const std::array<u8, 4>& loop_param = uniforms.i[instr.flow_int_uniform_id()];
                state.address_registers[2] = loop_param[1];

                // Quirk kept from Azahar: aL has just been assigned, so do_loop's `previous_aL`
                // is this loop's own start value, not the value of a surrounding loop.
                do_loop(instr, loop_param);
                break;
            }

            case Opcode::BREAK: {
                is_break = true;
                break;
            }

            case Opcode::BREAKC: {
                if (evaluate_condition(instr)) {
                    is_break = true;
                }
                break;
            }

            case Opcode::EMIT: {
                auto* emitter = state.emitter_ptr;
                if (emitter == nullptr) {
                    // Azahar: ASSERT_MSG(emitter, "execute EMIT on VS")
                    error = {Unhandled::EmitOnVertexShader, old_program_counter, instr.hex};
                    return false;
                }
                if (emitter->emit_state.vertex_id >= 3) {
                    // Azahar: ASSERT(emit_state.vertex_id < 3) in GeometryEmitter::Emit
                    error = {Unhandled::EmitVertexId, old_program_counter, instr.hex};
                    return false;
                }
                emitter->Emit(state.output[state.output_bank]);
                state.output_bank = !state.output_bank;
                break;
            }

            case Opcode::SETEMIT: {
                auto* emitter = state.emitter_ptr;
                if (emitter == nullptr) {
                    // Azahar: ASSERT_MSG(emitter, "execute SETEMIT on VS")
                    error = {Unhandled::EmitOnVertexShader, old_program_counter, instr.hex};
                    return false;
                }
                emitter->emit_state.vertex_id = static_cast<u8>(instr.setemit_vertex_id());
                emitter->emit_state.prim_emit = instr.setemit_prim_emit();
                emitter->emit_state.winding = instr.setemit_winding();
                break;
            }

            default:
                // Azahar: LOG_ERROR "Unhandled instruction" and goes on. Deviation: the draw is dropped.
                error = {Unhandled::Instruction, old_program_counter, instr.hex};
                return false;
            }

            break;
        }
        }

        ++program_counter;
        ++iteration;

        // Stacks are checked in the order CALL -> IF -> LOOP. The CALL stack can be popped multiple
        // times per instruction. A JMP at the end of a scope is never taken, this is why we compare
        // against old_program_counter + 1 here.
        u32 next_program_counter = old_program_counter + 1;
        for (u32 i = 0; i < 4; i++) {
            if (call_stack.empty() || call_stack.back().end_address != next_program_counter)
                break;
            // Hardware bug: when popping four CALL scopes at once, the last one doesn't update the
            // program counter
            if (i < 3) {
                program_counter = call_stack.back().return_address;
                next_program_counter = program_counter;
            }
            call_stack.pop_back();
        }

        // The other two stacks can only pop one entry per instruction. They are checked against the
        // original program counter before any CALL scopes were closed and they overwrite any
        // previous program counter updates.
        if (!if_stack.empty() && if_stack.back().else_address == old_program_counter + 1) {
            program_counter = if_stack.back().end_address;
            if_stack.pop_back();
        }

        if (!loop_stack.empty() &&
            (loop_stack.back().end_address == old_program_counter + 1 || is_break)) {
            auto& loop = loop_stack.back();
            state.address_registers[2] += loop.address_increment;
            if (!is_break && loop.loop_downcounter--) {
                program_counter = loop.entry_address;
            } else {
                program_counter = loop.end_address;
                // Only restore previous value if there is a surrounding LOOP scope.
                if (loop_stack.size() > 1)
                    state.address_registers[2] = loop.previous_aL;
                loop_stack.pop_back();
            }
        }
    }
    return true;
}

} // namespace mh4n::gpu::cpuvertex

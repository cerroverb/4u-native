// Derived from Azahar src/video_core/shader/shader_interpreter.cpp, shader_interpreter.h, shader.h,
// shader.cpp, src/video_core/pica/shader_unit.h, shader_unit.cpp, shader_setup.h, output_vertex.h
// and src/video_core/pica_types.h at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later;
// every one of those files says "Licensed under GPLv2 or any later version"): the shader unit
// state, the geometry emitter, the uniforms and the interpreter's declarations.
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (shader_interpreter.cpp)
// Copyright 2014 Citra Emulator Project (shader_interpreter.h)
// Copyright 2015 Citra Emulator Project (shader.h, shader.cpp)
// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project (shader_unit.h,
//   shader_unit.cpp, shader_setup.h)
// Copyright 2023-2024 Citra Emulator Project / Azahar Emulator Project (output_vertex.h)
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (pica_types.h)
//
// The shader interpreter and the shader unit (N2-36). Internal to the module: the public API is
// cpu_vertex.h.
//
// What is the same as Azahar: the register file of a unit (input, temporary and two banks of
// output registers, the address registers, the condition codes), `ShaderUnit::LoadInput` and
// `WriteOutput`, the geometry emitter (`SETEMIT` state, three buffered vertices, the output bank
// that switches at every EMIT), and the interpreter: every handler of shader_interpreter.cpp.
//
// What differs, on purpose:
//  - Azahar's ShaderSetup (program, swizzles, uniforms, entry point) is a view here (`ShaderSetup`
//    below): the program and swizzle words stay in the command processor's PicaState and the
//    uniforms are the runner's copy.
//  - The vertex and winding handlers (std::function in Azahar) are a `VertexSink`.
//  - An instruction Azahar logs as unhandled and goes on from (DST, DSTI, LIT, an unknown opcode,
//    an unknown compare mode), and the places where it asserts (EMIT or SETEMIT on a unit without
//    emitter, an entry point or a vertex id that does not exist), stop the run and are reported as
//    an `Unhandled` code, so the runner drops the draw instead of approximating (WP N2-36). A
//    shader that does not end is stopped after kMaxInstructions.
//  - A default-constructed unit has all its registers at 0 (Azahar leaves the emitter's buffers
//    and emit state uninitialised).
#pragma once

#include <array>
#include <memory>
#include <span>

#include "gpu/cpuvertex/f24.h"
#include "gpu/cpuvertex/instruction.h"
#include "mh4n/types.h"
#include "pica_native.h"

namespace mh4n::gpu::cpuvertex {

inline constexpr u32 kMaxProgramCodeLength = 4096;
// An invocation that executes more instructions than this is stopped (a program with a loop that
// never leaves): Azahar has no limit.
inline constexpr u32 kMaxInstructions = 1u << 22;

// Pica::Uniforms
struct Uniforms {
    std::array<Vec4, 96> f{};
    std::array<bool, 16> b{};
    std::array<std::array<u8, 4>, 4> i{}; // x, y, z, w
};

// What the interpreter reads of Azahar's ShaderSetup. The arrays are kMaxProgramCodeLength words
// (program) and 4096 words (swizzle table: 7-bit operand descriptor ids reach at most 128).
struct ShaderSetup {
    const u32* program_code = nullptr;
    const u32* swizzle_data = nullptr;
    const Uniforms* uniforms = nullptr;
    u32 entry_point = 0;
    // The program compiled by the player's code library (tools/recomp/runtime/pica_native.h), when the runner found
    // one for it: Run calls it instead of interpreting. With `check_native`, Run interprets and then runs the function
    // on a copy of the unit, compares the two (registers, emitted vertices) and counts any difference
    // (NativeCheckCounts).
    PicaNativeFunction native = nullptr;
    bool check_native = false;
};

// MH4N_PICA_NATIVE=check: the runs compared and those that differed (all threads).
struct NativeCheckCounts {
    u64 runs = 0;
    u64 differences = 0;
};
NativeCheckCounts GetNativeCheckCounts();

// Why a run stopped without finishing; the draw is dropped.
enum class Unhandled : u8 {
    None,
    Instruction,        // DST, DSTI, LIT or an opcode that does not exist
    CompareMode,        // CMP with an unknown compare operation (6 or 7)
    EmitOnVertexShader, // EMIT or SETEMIT in a unit that has no emitter
    EmitVertexId,       // EMIT with SETEMIT vertex id 3 (the emitter buffers three vertices)
    EntryPoint,         // the entry point is outside the program memory
    InstructionBudget,  // more than kMaxInstructions executed
    // ---- raised by the geometry pipeline and the runner ----
    GeometryConfig,     // inconsistent geometry pipeline set-up (Azahar asserts)
    GeometryNeedsIndex, // variable primitive mode on a draw that has no index buffer
    GeometryOverflow,   // vertices would not fit the input registers or uniforms
    LoaderBuffer,       // a vertex the loader did not provide
    OutputSemantic,     // an output semantic id of 24 to 30 on an Arrays/Indexed draw (Azahar asserts)
};

struct RunError {
    Unhandled kind = Unhandled::None;
    u32 program_counter = 0;
    u32 instruction = 0;
};

// Receives what a geometry shader emits, in order (the handlers of Azahar's GeometryEmitter).
class VertexSink {
public:
    virtual ~VertexSink() = default;
    virtual void SubmitVertex(const AttributeBuffer& output) = 0;
    // The winding flag of the primitive about to be emitted: Azahar's `winding_setter`.
    virtual void SetWinding() = 0;
};

// GeometryEmitter
struct GeometryEmitter {
    // Buffers the output registers of the vertex `emit_state.vertex_id` and, when `prim_emit` is
    // set, sends the winding flag and the three buffered vertices to the sink.
    void Emit(std::span<const Vec4, 16> output_regs);

    struct EmitState {
        bool winding = false;
        bool prim_emit = false;
        u8 vertex_id = 0; // 2 bits in the instruction
    } emit_state;

    u32 output_mask = 0;
    std::array<AttributeBuffer, 3> buffer{};
    VertexSink* sink = nullptr;
};

// What LoadInput needs of a unit's ShaderRegs.
struct ShaderInputConfig {
    u32 max_input_attribute_index = 0;
    u64 input_attribute_to_register_map = 0; // 4 bits per attribute
    u32 GetRegisterForAttribute(u32 attribute_index) const {
        return static_cast<u32>((input_attribute_to_register_map >> (attribute_index * 4)) & 0b1111);
    }
};

// ShaderUnit
struct ShaderUnit {
    explicit ShaderUnit(GeometryEmitter* emitter = nullptr) : emitter_ptr(emitter) {}

    void LoadInput(const ShaderInputConfig& config, const AttributeBuffer& input);
    // Fills `buffer` from the output registers named by the bits of `output_mask`, in order.
    void WriteOutput(u32 output_mask, AttributeBuffer& buffer) const;

    s32 address_registers[3] = {};
    bool conditional_code[2] = {};
    std::array<Vec4, 16> input{};
    std::array<Vec4, 16> temporary{};

    // Programmers only have access to a single set of output registers (o0-o15), however there is
    // a hardware quirk with output registers when used in geometry shaders: when emitting a series
    // of vertices with the emit instruction, if the output registers are not written between two
    // consecutive emits, then the unmodified output registers take the values they had 2 emits
    // ago (Azahar: verified on real hardware).
    std::array<std::array<Vec4, 16>, 2> output{};
    bool output_bank = false;

    GeometryEmitter* emitter_ptr = nullptr;
};

// GeometryShaderUnit: the unit that can run both the vertex and the geometry shader.
struct GeometryShaderUnit : public ShaderUnit {
    GeometryShaderUnit() : ShaderUnit(&emitter) {}
    GeometryShaderUnit(const GeometryShaderUnit&) = delete;
    GeometryShaderUnit& operator=(const GeometryShaderUnit&) = delete;

    void SetVertexSink(VertexSink* sink) {
        emitter.sink = sink;
    }
    void ConfigOutput(u32 output_mask) {
        emitter.output_mask = output_mask;
    }

    // Back to the state of a new unit (the sink stays).
    void Reset();

    GeometryEmitter emitter;
};

// A program the code library does not carry compiled is interpreted. MH4N_PICA_DECODED (read once, at the first run):
//   on (the default)  each instruction word is decoded once into a record (its operands, selectors, write mask,
//                     jump fields) that later runs read, checked against the program's and the operand descriptors'
//                     words at every step, so a word the game uploads again is decoded again: the same operations in
//                     the same order as the word-by-word interpreter, without its decoding at every instruction (the
//                     High Rank field on a Moto g06 spent a tenth of the renderer's thread interpreting)
//   scalar            decoded, one lane at a time (on arm64 the arithmetic otherwise takes the four lanes at once)
//   off               word by word (Interpret), as before
//   check             both, the word-by-word one for real and the decoded one on a copy of the unit, compared run by
//                     run (registers, condition codes, address registers, emitted vertices: DecodedCheckCounts), each
//                     one's time summed
struct DecodedCheckCounts {
    u64 runs = 0;
    u64 differences = 0;
    u64 plain_ns = 0;    // the word-by-word interpreter's time in those runs
    u64 decoded_ns = 0;  // the decoded one's
};
DecodedCheckCounts GetDecodedCheckCounts();
// The renderer's MH4N_AB phase "decoded" turns the decoded interpreter off (any thread; a measurement aid).
void SetDecodedInterpreterOff(bool off);
// ... and its four lanes at once (on arm64; MH4N_PICA_DECODED=scalar, the MH4N_AB phase "decvec": one lane at a time).
void SetDecodedVectorsOff(bool off);
// What the decoded interpreter did since the last call (the renderer's frame counters pica_steps, pica_decodes): the
// instructions it ran and those it had to decode first (the High Rank field on a Moto g06 at 2x: about 28,000 and 65 a
// frame, so its time, a sixth of the renderer's thread there, is the instructions' own). Reading resets them.
struct DecodedWork {
    u64 steps = 0;
    u64 decodes = 0;
};
DecodedWork TakeDecodedWork();

// InterpreterEngine::Run
class InterpreterEngine {
public:
    InterpreterEngine();
    ~InterpreterEngine();
    InterpreterEngine(const InterpreterEngine&) = delete;
    InterpreterEngine& operator=(const InterpreterEngine&) = delete;

    // Runs the shader from `setup.entry_point` until END. False when it stopped for a reason in
    // `error`.
    bool Run(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const;

    struct DecodedCache;  // (shader_interpreter.cpp)

private:
    bool Interpret(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const;
    bool InterpretDecoded(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const;
    bool InterpretBoth(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const;
    bool RunNative(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const;
    bool RunChecked(const ShaderSetup& setup, ShaderUnit& state, RunError& error) const;

    // The decoded programs (InterpretDecoded): an engine runs on one thread at a time (the renderer's runner).
    mutable std::unique_ptr<DecodedCache> decoded_;
};

} // namespace mh4n::gpu::cpuvertex

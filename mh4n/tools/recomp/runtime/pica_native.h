/* The PICA shader programs a code library carries compiled (tools/pica_native.py): the interface between the
 * generated functions, which the player's build makes from the game's own shader package, and the runtime's CPU
 * vertex path (native/src/gpu/cpuvertex), which runs them instead of its interpreter when one matches the program
 * a draw runs.
 *
 * A code library exports PICA_NATIVE_TABLE_SYMBOL, a function that returns the table and its length. An entry is one
 * entry point of one shader binary: the program words [0, code_words) and the operand descriptors [0, desc_count) as
 * the game uploads them (their hashes, pica_native_hash) and the entry point. The runtime checks both hashes against
 * the unit's memory before it uses an entry. A library without the symbol, or a program without an entry, runs on the
 * interpreter.
 *
 * A function does what the interpreter (shader_interpreter.cpp) does from the entry point to END, with the same float
 * operations in the same order. Its state is the shader unit's: the registers are float[4] (the runtime's f24 is a
 * float), the bool uniforms and the conditional code are one byte each. It returns 1 at END, or -(1 + pc) for the word
 * at pc that the interpreter would stop at with an error (an EMIT or a SETEMIT the emitter refuses). */
#ifndef PICA_NATIVE_H
#define PICA_NATIVE_H

#define PICA_NATIVE_VERSION 1u
#define PICA_NATIVE_TABLE_SYMBOL "pica_native_table"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PicaNativeState {
    float (*input)[4];          /* v0-v15 */
    float (*temporary)[4];      /* r0-r15 */
    float (*output)[16][4];     /* o0-o15, two banks (the geometry unit's emitter alternates them) */
    unsigned char *output_bank; /* the bank written now (0 or 1) */
    int *address;               /* a0.x, a0.y, aL */
    unsigned char *conditional; /* the conditional code x, y */
    const float (*uniform_f)[4];        /* c0-c95 */
    const unsigned char *uniform_b;     /* b0-b15 */
    const unsigned char (*uniform_i)[4];/* i0-i3 */
    /* EMIT: the runtime emits bank *output_bank (and the generated code then flips it); 0 when it refuses */
    int (*emit)(void *ctx);
    int (*setemit)(void *ctx, unsigned vertex_id, unsigned prim_emit, unsigned winding);  /* 0 when it refuses */
    float (*exp2)(float);       /* EX2 and LG2: the runtime's, so that both paths give the same bits */
    float (*log2)(float);
    void *ctx;
} PicaNativeState;

typedef int (*PicaNativeFunction)(PicaNativeState *state);

typedef struct PicaNativeEntry {
    unsigned entry_point;
    unsigned code_words;
    unsigned desc_count;
    unsigned geometry; /* 1: the binary's geometry shader entry, 0: its vertex shader */
    unsigned long long code_hash;
    unsigned long long desc_hash;
    PicaNativeFunction function;
} PicaNativeEntry;

typedef const PicaNativeEntry *(*PicaNativeTableFunction)(unsigned *count, unsigned *version);

/* FNV-1a over the words' bytes, little endian: the generator's and the runtime's hash */
static inline unsigned long long pica_native_hash(const unsigned *words, unsigned count) {
    unsigned long long h = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i < count; ++i) {
        for (unsigned b = 0; b < 4; ++b) {
            h ^= (words[i] >> (8 * b)) & 0xFFu;
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

#ifdef __cplusplus
}
#endif

#endif

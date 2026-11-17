// The colour filter ([Colour] in mh4u-app.ini, the menu's Graphics & Audio > Colour): our own screen adjustment, a few
// ALU operations folded into the draws that already put the game's two screens into the window (shared_screens.cpp:
// the render target's program and the screens' own), never a pass of its own. The MHGU port has the same file
// (runtime/colour_filter.h, with its HDR output's numbers too): change both.
//
// Every control is a whole number whose 0 is neutral, so a file without the [Colour] keys, and Reset colour, give the
// picture as before: with all of them at 0 the screens are drawn as before, byte for byte (SDL's own draw and the
// render target's program, unchanged), and nothing here runs.
//
// What it changes: the game's two screens as they reach the window (the 3D, the game's HUD, its menus, the touch
// screen), not our own overlay (the touch controls, this menu, the notices), which is drawn over them afterwards: the
// menu used to adjust the colour stays readable and true whatever the sliders say, so Reset colour is always within reach.
//
// The picture is the game's own sRGB-encoded one; each step works on those values, as a television's picture menu does:
//   gamma        c^(2^(-gamma/100))         +: the mid-tones brighter, -: darker; black and white stay where they are
//   temperature  per-channel gains <= 1     +: warmer (blue and a little green down), -: cooler (red and a little green down)
//   tint         per-channel gains <= 1     +: magenta (green down), -: green (red and blue down)
//   hue          a turn round the grey axis (degrees)
//   saturation   luma + s (c - luma), s = 1 + saturation/100 (Rec. 709 luma)
//   contrast     k (c - 1/2) + 1/2, k = 1 + contrast/100
//   brightness   + brightness/400
// then clamped to 0..1. Gains never above 1: nothing white clips. All but the gamma is one affine map: three rows of four.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <string.h>

typedef struct ColourAdjust {
    int hue;          // [Colour] Hue: degrees, -180..180
    int temperature;  // [Colour] Temperature: -100..100
    int tint;         // [Colour] Tint: -100..100
    int brightness;   // [Colour] Brightness: -100..100
    int contrast;     // [Colour] Contrast: -100..100
    int saturation;   // [Colour] Saturation: -100..100
    int gamma;        // [Colour] Gamma: -100..100
} ColourAdjust;

#define COLOUR_HUE_MAX 180
#define COLOUR_MAX 100

static inline bool colour_neutral(const ColourAdjust *a) {
    return !a->hue && !a->temperature && !a->tint && !a->brightness && !a->contrast && !a->saturation && !a->gamma;
}

// The filter as the shader runs it: out = clamp(rows * vec4(pow(c, gamma), 1), 0, 1)
typedef struct {
    float row[3][4];
    float gamma;  // the exponent (1: none)
} ColourMatrix;

static inline void colour_matrix(const ColourAdjust *a, ColourMatrix *out) {
    // gains (temperature, tint), never above 1
    float t = a->temperature / 100.0f, m = a->tint / 100.0f;
    float gain[3] = {1, 1, 1};
    if (t > 0) gain[1] *= 1 - 0.06f * t, gain[2] *= 1 - 0.18f * t;
    else if (t < 0) gain[0] *= 1 + 0.18f * t, gain[1] *= 1 + 0.06f * t;
    if (m > 0) gain[1] *= 1 - 0.15f * m;
    else if (m < 0) gain[0] *= 1 + 0.10f * m, gain[2] *= 1 + 0.10f * m;
    // the hue's turn round (1, 1, 1)
    const float th = a->hue * 3.14159265f / 180.0f, c = cosf(th), s = sinf(th), k3 = (1 - c) / 3.0f, r3 = 0.57735027f * s;
    const float hue[3][3] = {{c + k3, k3 - r3, k3 + r3}, {k3 + r3, c + k3, k3 - r3}, {k3 - r3, k3 + r3, c + k3}};
    // saturation round Rec. 709 luma
    const float sat = 1 + a->saturation / 100.0f, w[3] = {0.2126f, 0.7152f, 0.0722f};
    float satm[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) satm[i][j] = (1 - sat) * w[j] + (i == j ? sat : 0);
    const float con = 1 + a->contrast / 100.0f, offset = 0.5f * (1 - con) + a->brightness / 400.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float v = 0;  // (satm * hue)[i][j] * gain[j]
            for (int k = 0; k < 3; k++) v += satm[i][k] * hue[k][j];
            out->row[i][j] = con * v * gain[j];
        }
        out->row[i][3] = offset;
    }
    out->gamma = exp2f(-a->gamma / 100.0f);
}

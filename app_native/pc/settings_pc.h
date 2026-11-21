// MH4U for PC: the settings only the PC has (settings_pc.inc, included by settings_panel.cpp; MHGU for PC's same rows):
// the window ([Display] WindowMode, FullscreenMode), VSync, the 3D picture's filter, the HUD anchor, the PC readout, the
// GPU and the HDR output. Their draft is part of settings_panel.cpp's (Draft::pc), loaded and saved with it.
#pragma once

#include <string>

struct PcDraft {
    int window_mode = 0;               // [Display] WindowMode: 0 borderless, 1 windowed, 2 fullscreen (pc_display.h)
    std::string fullscreen_mode;       // [Display] FullscreenMode: WxH@Hz, "" the desktop's
    bool vsync = true;                 // [Graphics] VSync
    int upscale = 0;                   // [Graphics] Upscale: 0 smooth, 1 sharp, 2 nearest, 3 FSR 1 (1.0.2)
    int stats = 0;                     // [Graphics] PcStats: 0 off, 1 basic, 2 full (FpsCounter on with 1 and 2)
    std::string hud_anchor = "whole";  // mh4u.ini's hud_anchor: whole, 16:9, 21:9, 32:9
    std::string gpu;                   // [Graphics] Gpu: "" the renderer's own choice, else a part of a GPU's name
    bool hdr = false;                  // [Colour] HDR (pc_hdr.h: a restart)
    int hdr_brightness = 0;            // [Colour] HDRBrightness: -100..100, the paper white 203 nits times 2^(x/100)
    int hdr_highlights = 0;            // [Colour] HDRHighlights: -100..100, the brightest part towards the screen's peak
    bool operator==(const PcDraft&) const = default;
};

// (settings_panel.cpp's load and save)
void pc_draft_load(PcDraft& d);
void pc_draft_save(const PcDraft& d, const PcDraft& was);
// The readout's level from the saved settings (the start, pc_main.cpp; Save): perfstats' PC lines
void pc_stats_apply();
// (tests) the settings shown at a section ("graphics") or a page ("graphics/screen"), after a '#' the row picked; the
// panel opened by the caller. Save: as the Save button
void pc_settings_test_page(const std::string& at);
void pc_settings_test_save();
// (perfstats.cpp) the PC readout's level: 0 or 1 the GPU's temperature on its line, 2 the GPU's and the CPU's lines too
void perfstats_set_pc_level(int level);
// The FPS limit's "no limit" in mh4u.ini's fps (the hooks' emulated refresh rate follows it: a VBlank a frame)
constexpr int kPcFpsUnlimited = 1000;

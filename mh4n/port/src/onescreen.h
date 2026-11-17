// One screen: the game's touch-screen menus drawn on the top screen by the game itself (single_screen in mh4u.ini).
#pragma once

#include <cstdint>
#include <string>

struct RCtx;

namespace Port::OneScreen {

/// hooks.cpp's sMhMain::frame, before the game's frame. Returns a line for the log, or "". `wide`: the top screen is
/// rendered wider than the console's 400 pixels (wide_render), so the menus placed on it are cut to those 400.
std::string Frame(RCtx* ctx, bool on, bool wide = false);

/// hooks.cpp's nLayout::objectConstants, after the game's, for every object of a layout pass (`screen` as below): the
/// scissor the object is drawn with, cut to the console's 400 pixels for a group placed on a wider top screen.
void ObjectScissor(RCtx* ctx, std::uint32_t object, std::uint32_t draw, std::uint32_t screen);

/// hooks.cpp's nLayout::objectConstants, after the game wrote the object's constants at `constants` (x, y, z, w, then
/// the colour) for the layout pass of `screen` (0 the top screen, 7 the touch screen).
void ObjectConstants(RCtx* ctx, std::uint32_t object, std::uint32_t draw, std::uint32_t constants,
                     std::uint32_t screen);

}  // namespace Port::OneScreen

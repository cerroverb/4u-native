// The app's options the runtime takes while the game runs (the settings panel's Save) and at the start (android_main.cpp):
// the volume (the SDL audio device), the reverb (the HLE DSP's effect sends) and the shadows (the native renderer). As
// MHGU's: each applies at once, no restart.
//   [Audio]    Volume = 0..100 (android_main.cpp also passes it as --volume, what the device opens with), Reverb = on|off
//   [Graphics] Shadows = on|off
#pragma once

// Before the game starts (android_main.cpp): the reverb and the shadows as mh4u-app.ini has them.
void app_live_start();
// The volume, from the game's next frame (the SDL audio device is the runtime's; before the game runs, --volume).
void app_live_volume(int percent);
void app_live_reverb(bool on);
void app_live_shadows(bool on);

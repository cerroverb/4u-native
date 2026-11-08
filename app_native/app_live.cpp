// The app's options the runtime takes while the game runs (app_live.h).
#include "app_live.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

#include "app_settings.h"
#include "audio/app_audio.h"
#include "dsp_hle/mixers.h"
#include "gpu/native/native_gpu.h"
#include "mh4n/registry.h"
#include "mh4n/runtime.h"

namespace {

std::atomic<int> g_volume{-1};  // a volume the game's thread is to set (-1: none)

// The volume, on the game's thread at its next frame: the runtime (and its SDL audio device) lives while frames come.
void Hook(mh4n::Runtime& rt) {
    rt.OnFrame([&rt](mh4n::u32, mh4n::u64) {
        const int volume = g_volume.exchange(-1);
        if (volume >= 0 && !mh4n::app_audio::SetVolume(rt, volume)) {
            std::fprintf(stderr, "[Warning] App: the volume (%d %%) could not be set: no SDL audio device\n", volume);
        }
    });
}
MH4N_REGISTER(mh4n::BootStep, "zz.android.live", mh4n::BootStep{mh4n::Phase::Started, Hook});

}  // namespace

void app_live_start() {
    app_live_reverb(app_setting_bool("Audio", "Reverb", true));
    app_live_shadows(app_setting_bool("Graphics", "Shadows", true));
}

void app_live_volume(int percent) {
    g_volume.store(std::clamp(percent, 0, 100));
}

void app_live_reverb(bool on) {
    mh4n::dsp_hle::SetReverb(on);
    std::fprintf(stderr, "[Info] App: reverb %s\n", on ? "on" : "off");
}

void app_live_shadows(bool on) {
    mh4n::gpu::native::SetShadows(on);
    std::fprintf(stderr, "[Info] App: shadows %s\n", on ? "on" : "off");
}

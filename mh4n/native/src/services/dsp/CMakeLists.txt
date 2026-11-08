# mh4n_services_dsp: the dsp::DSP service, the audio frame timer and --capture-dsp (N1-18). The DSP
# behind it (--dsp, N3-11) is our HLE DSP, mh4n_dsp_hle (the default), or N1-18's model of mh4n_audio.
mh4n_module(
    SOURCES
        dsp_capture.cpp
        dsp_dsp.cpp
    DEPENDS
        mh4n_audio
        mh4n_dsp_hle
        mh4n_ipc
        mh4n_kernel
        mh4n_memory
        mh4n_runtime
    THIRD_PARTY
        mh4n_fmt
)
target_include_directories(mh4n_services_dsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

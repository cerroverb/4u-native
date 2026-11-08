# mh4n_applets: the HLE library applets of the APT service (N1-14): what an applet is and sees of the
# applet manager (applet.h), and the software keyboard answering as Azahar's default keyboard does
# (swkbd.h), and the error, Mint and Mii selector applets that close at once (N1-14c: erreula.h,
# mint.h, mii_selector.h). The applet manager itself is services/apt, which uses this module. The app's
# frontends of the keyboard and the Error applet (N3-12: mh4n/applet_frontend.h) meet the applets in
# frontend_hub.h, which a boot step of applet_frontend.cpp hands to the service context.
mh4n_module(
    SOURCES
        applet.cpp
        applet_frontend.cpp
        swkbd.cpp
        erreula.cpp
        mint.cpp
        mii_selector.cpp
    DEPENDS
        mh4n_ipc
        mh4n_kernel
        mh4n_runtime
        mh4n_services_cfg
)
# The headers are included as "applets/<name>.h" (the applet manager, the tests).
target_include_directories(mh4n_applets PUBLIC "${MH4N_SRC_DIR}")

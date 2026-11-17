# mh4n_services_am: am:app (N1-19); the add-on content titles it answers for come from mh4n_services_fs (1.0.1).
mh4n_module(
    SOURCES
        am_app.cpp
    DEPENDS
        mh4n_ipc
        mh4n_kernel
        mh4n_services_fs
)

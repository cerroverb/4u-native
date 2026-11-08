# mh4n_services_nwm: nwm::UDS, the local-wireless service (N1-29), over the network room the process's room client is
# in (N4-02, native/src/net): beacons, connections, data frames and node ids, as Azahar carries them.
mh4n_module(
    SOURCES
        nwm_uds.cpp
        uds_crypto.cpp
        uds_frames.cpp
    DEPENDS
        mh4n_ipc
        mh4n_kernel
        mh4n_net
        mh4n_runtime
        mh4n_services_cfg
)

# mh4n_net: the network rooms (N4-01, D7), Azahar's room protocol over ENet: the room client, the room server and a
# lobby's list of rooms (mh4n/rooms.h). ENet 1.3.18 is native/third_party/enet (mh4n_enet).
add_subdirectory("${MH4N_ROOT}/third_party/enet" "${CMAKE_CURRENT_BINARY_DIR}/enet")
mh4n_module(
    SOURCES
        lobby.cpp
        packet.cpp
        room_client.cpp
        room_server.cpp
    DEPENDS
        mh4n_services_cfg
    THIRD_PARTY
        mh4n_enet
)

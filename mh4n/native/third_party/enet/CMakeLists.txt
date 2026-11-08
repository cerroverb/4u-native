# ENet 1.3.18 (native/THIRD_PARTY.md) as the static library mh4n_enet, for the network rooms
# (native/src/net, N4-01). Ours, not upstream's CMakeLists.txt: the same sources and the same
# configure checks and definitions as upstream's at the tag, without its install rules. Added by
# native/src/net/CMakeLists.txt.
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckTypeSize)

set(MH4N_ENET_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(mh4n_enet STATIC
    ${MH4N_ENET_DIR}/callbacks.c
    ${MH4N_ENET_DIR}/compress.c
    ${MH4N_ENET_DIR}/host.c
    ${MH4N_ENET_DIR}/list.c
    ${MH4N_ENET_DIR}/packet.c
    ${MH4N_ENET_DIR}/peer.c
    ${MH4N_ENET_DIR}/protocol.c
    ${MH4N_ENET_DIR}/unix.c
    ${MH4N_ENET_DIR}/win32.c)
target_include_directories(mh4n_enet SYSTEM PUBLIC "${MH4N_ENET_DIR}/include")
target_compile_options(mh4n_enet PRIVATE $<IF:$<BOOL:${MSVC}>,/w,-w>)

if (WIN32)
    target_link_libraries(mh4n_enet PUBLIC winmm ws2_32)
else()
    check_function_exists("fcntl" MH4N_ENET_HAS_FCNTL)
    check_function_exists("poll" MH4N_ENET_HAS_POLL)
    check_function_exists("getaddrinfo" MH4N_ENET_HAS_GETADDRINFO)
    check_function_exists("getnameinfo" MH4N_ENET_HAS_GETNAMEINFO)
    check_function_exists("gethostbyname_r" MH4N_ENET_HAS_GETHOSTBYNAME_R)
    check_function_exists("gethostbyaddr_r" MH4N_ENET_HAS_GETHOSTBYADDR_R)
    check_function_exists("inet_pton" MH4N_ENET_HAS_INET_PTON)
    check_function_exists("inet_ntop" MH4N_ENET_HAS_INET_NTOP)
    check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h"
                            MH4N_ENET_HAS_MSGHDR_FLAGS)
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" MH4N_ENET_HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    foreach (feature FCNTL POLL GETADDRINFO GETNAMEINFO GETHOSTBYNAME_R GETHOSTBYADDR_R INET_PTON
                     INET_NTOP MSGHDR_FLAGS SOCKLEN_T)
        if (MH4N_ENET_HAS_${feature})
            target_compile_definitions(mh4n_enet PRIVATE HAS_${feature}=1)
        endif()
    endforeach()
endif()

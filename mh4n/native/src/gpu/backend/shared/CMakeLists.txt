# mh4n_gpu_backend_shared: images the backend draws into and the system's other graphics read with no copy (an
# AHardwareBuffer bound to a VkImage on Android, VK_ANDROID_external_memory_android_hardware_buffer), the queue-family
# hand-over around them, and a sync file of a fence (VK_KHR_external_fence_fd) for those readers to wait on. The
# renderer draws its screens into them when the Android app asks for shared pictures (present.h SharedPicture). On
# other platforms SharedImages::Create says no and nothing else is used.
mh4n_module(
    SOURCES
        shared_images.cpp
    DEPENDS
        mh4n_gpu_backend_core
        mh4n_gpu_backend_targets
)
# The header is included as "gpu/backend/shared/shared_images.h".
target_include_directories(mh4n_gpu_backend_shared PUBLIC "${MH4N_SRC_DIR}")
if (ANDROID)
    target_link_libraries(mh4n_gpu_backend_shared PUBLIC nativewindow)
endif()

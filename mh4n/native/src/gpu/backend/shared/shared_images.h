// Images the backend draws into and the system's other graphics read with no copy (written for this repository).
//
// On Android an image of this kind is an AHardwareBuffer (R8G8B8A8, a colour target and a sampled image) bound to a
// VkImage through VK_ANDROID_external_memory_android_hardware_buffer: the renderer draws a screen into it as into any
// target, and the app's OpenGL ES samples the same memory through an EGLImage (app_native/shared_screens.cpp). Nothing
// is copied through the CPU.
//
// The hand-over follows the Vulkan rules for memory another API reads: after the last write of a picture the image is
// released to VK_QUEUE_FAMILY_FOREIGN_EXT (Release), and before the next picture is drawn into it, it is acquired back
// (Acquire; the picture before is not kept, every picture is drawn whole). The image stays in the backend's one
// layout (GENERAL) throughout. Who reads the memory must be done with it before the renderer draws into it again: the
// renderer only draws into an image nobody holds (native_gpu.cpp's pool), and the app holds a picture until its own GPU
// work that read it has finished.
//
// The reader waits for the GPU on its own side: ExportFence gives a sync file (VK_KHR_external_fence_fd) that signals
// once everything submitted so far has finished, for EGL_ANDROID_native_fence_sync; WaitFor is the CPU's way.
//
// Threads: Create, Make, Acquire, Release and ExportFence on the thread that owns the images (the renderer thread);
// SyncFile, WaitFor and SharedImage::BufferRef on any thread.
#pragma once

#include <memory>
#include <string>

#include "gpu/backend/rhi.h"
#include "mh4n/types.h"

namespace mh4n::gpu::rhi {

// A sync file (a file descriptor that signals once): closed when the last holder lets go.
class SyncFile {
public:
    explicit SyncFile(int fd) : fd_(fd) {}
    ~SyncFile();
    SyncFile(const SyncFile&) = delete;
    SyncFile& operator=(const SyncFile&) = delete;
    // A new descriptor of the same file, owned by the caller; -1 when there is none.
    int Dup() const;
    int Fd() const { return fd_; }
    // It has signalled (a descriptor of -1 had already when it was made). Any thread.
    bool Signaled() const;

private:
    int fd_ = -1;
};

class SharedImage {
public:
    struct Impl;
    explicit SharedImage(std::unique_ptr<Impl> impl);
    ~SharedImage();  // the image, its memory, the buffer: retire it (Backend::Retire) while the GPU may still use it
    SharedImage(const SharedImage&) = delete;
    SharedImage& operator=(const SharedImage&) = delete;

    // The backend's image over it: Rgba8Unorm, Width() x Height(), a colour target, sampled, copied from and to.
    Image& GetImage();
    // The AHardwareBuffer, held for as long as the returned pointer (or a copy) lives, whatever becomes of this object.
    std::shared_ptr<void> BufferRef() const;
    u32 Width() const;
    u32 Height() const;
    // A number no other shared image of the process had.
    u64 Id() const;

    Impl& impl() { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

class SharedImages {
public:
    struct Impl;
    // Null when the device cannot share images (not Android, a missing extension, the format refused); `why` says.
    static std::unique_ptr<SharedImages> Create(const std::shared_ptr<Backend>& backend, std::string* why);
    explicit SharedImages(std::unique_ptr<Impl> impl);
    ~SharedImages();

    Result<std::shared_ptr<SharedImage>> Make(u32 width, u32 height);
    // Raw barriers into `recorder` (no pass open): the image back from the other API before it is drawn into again,
    // and handed to it after the last write of a picture.
    void Acquire(Recorder& recorder, SharedImage& image);
    void Release(Recorder& recorder, SharedImage& image);
    // The images can also be copied from and to (what a render target's own image needs).
    bool CanCopy() const;
    // As Acquire and Release, for an image the renderer uses in every way (a render target: drawn into, sampled, copied
    // from and to, read back): the barriers wait for and make visible every stage's accesses. AcquireAll keeps the
    // contents, as Acquire does (the hand-over keeps the layout).
    void AcquireAll(Recorder& recorder, SharedImage& image);
    void ReleaseAll(Recorder& recorder, SharedImage& image);
    // The device can make sync files of fences.
    bool CanExportFences() const;
    // A sync file that signals once everything submitted to the queue so far has finished; null when the device
    // cannot make one (or it failed: logged once). Takes the queue's lock for a moment.
    std::shared_ptr<SyncFile> ExportFence();

    // Blocks the calling thread until `backend`'s timeline has reached `value`, or for `timeout_ns` at most; whether it
    // has. Any thread.
    static bool WaitFor(Backend& backend, TimelineValue value, u64 timeout_ns);

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace mh4n::gpu::rhi

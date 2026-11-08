// A file read by position (N1-12): `pread` on POSIX, `ReadFile` with an OVERLAPPED offset on Windows.
// There is no cursor, so any number of threads can read one object at the same time. RomFS images are
// gigabytes: nothing here reads a file whole.
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "mh4n/types.h"

namespace mh4n::fs {

class PositionedFile {
public:
    // Opens `path` for reading; null, and a message in *error, if it cannot be opened.
    static std::shared_ptr<PositionedFile> Open(const std::string& path, std::string* error);
    // A part of a file, [offset, offset + size), seen as a file of its own (a RomFS inside the player's .cci or
    // .cia: the Android app reads the dump as it is, nothing is extracted). Null if the part leaves the file.
    static std::shared_ptr<PositionedFile> OpenWindow(const std::string& path, u64 offset, u64 size,
                                                      std::string* error);
    ~PositionedFile();
    PositionedFile(const PositionedFile&) = delete;
    PositionedFile& operator=(const PositionedFile&) = delete;

    // The size when it was opened.
    u64 Size() const { return size_; }
    // Reads up to `length` bytes at `offset` (fewer at the end of the file or when the host fails; *failed
    // says which of the two when it is not null). Any thread.
    std::size_t ReadAt(u64 offset, void* buffer, std::size_t length, bool* failed = nullptr) const;

private:
    PositionedFile() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    u64 size_ = 0;
    u64 base_ = 0;  // where the window starts in the file (0: the whole file)
};

}  // namespace mh4n::fs

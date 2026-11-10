// The process's and the system's memory, for the renderer's memory line and its texture budget (4u-native: a long session
// on a 3-4 GB phone). The app's own [mem] line (app_native/perf_hint.c, on a thread of its own) counts the map entries:
// reading /proc/self/maps takes the process's memory map lock, so it is not done here, on the renderer thread.
#pragma once

#include "mh4n/types.h"

namespace mh4n::gpu::native {

struct ProcessMemory {
    long resident_kb = -1;      // resident (Windows: the working set)
    long anon_kb = -1;          // of it, the process's own (Windows: its private bytes)
    long file_kb = -1;          // of it, mapped files (pages the system can drop and read again)
    long available_kb = -1;     // the system's available memory (MemAvailable)
};

ProcessMemory ReadProcessMemory();
// The system's available memory in KB (-1 when unknown); cheap enough every few seconds.
long AvailableMemoryKb();
// The device's memory in MB (0 when unknown).
u64 DeviceMemoryMb();

}  // namespace mh4n::gpu::native

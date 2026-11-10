#include "gpu/native/process_memory.h"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mh4n::gpu::native {

#if defined(_WIN32)

ProcessMemory ReadProcessMemory() {
    ProcessMemory m;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof counters;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters)) {
        m.resident_kb = static_cast<long>(counters.WorkingSetSize / 1024);
        m.anon_kb = static_cast<long>(counters.PrivateUsage / 1024);
    }
    m.available_kb = AvailableMemoryKb();
    return m;
}

long AvailableMemoryKb() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? static_cast<long>(status.ullAvailPhys / 1024) : -1;
}

u64 DeviceMemoryMb() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys >> 20 : 0;
}

#else

namespace {

// The number after `name` in a /proc file of "Name: value kB" lines (-1 when it has none).
long KbField(const char* text, const char* name) {
    const char* p = std::strstr(text, name);
    return p != nullptr ? std::atol(p + std::strlen(name)) : -1;
}

// A small /proc file whole (status, meminfo: a few KB).
bool ReadSmall(const char* path, char* buf, std::size_t size) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::size_t total = 0;
    ssize_t r;
    while (total + 1 < size && (r = read(fd, buf + total, size - 1 - total)) > 0) {
        total += static_cast<std::size_t>(r);
    }
    close(fd);
    buf[total] = 0;
    return total > 0;
}

}  // namespace

ProcessMemory ReadProcessMemory() {
    ProcessMemory m;
    char buf[8192];
    if (ReadSmall("/proc/self/status", buf, sizeof buf)) {
        m.resident_kb = KbField(buf, "VmRSS:");
        m.anon_kb = KbField(buf, "RssAnon:");
        m.file_kb = KbField(buf, "RssFile:");
    }
    m.available_kb = AvailableMemoryKb();
    return m;
}

long AvailableMemoryKb() {
    char buf[4096];
    return ReadSmall("/proc/meminfo", buf, sizeof buf) ? KbField(buf, "MemAvailable:") : -1;
}

u64 DeviceMemoryMb() {
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? (static_cast<u64>(pages) * static_cast<u64>(page)) >> 20 : 0;
}

#endif

}  // namespace mh4n::gpu::native

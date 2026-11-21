// Writing a small .zip (zipw.c): its entries stored as they are (no compression: a control profile's text and its PNG
// pictures, already compressed), each with its CRC-32, the central directory and its end. The MHGU port's
// runtime/zipw.h is the same. Paths are UTF-8 and go through SDL's file functions; names use '/'.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;  // inside the .zip ("images/a.png")
    const void *data;
    size_t size;
} ZipItem;

// The .zip `path` (created or replaced) with those entries; false: it couldn't be written (what was written is removed)
bool zip_write(const char *path, const ZipItem *items, int count);

#ifdef __cplusplus
}
#endif

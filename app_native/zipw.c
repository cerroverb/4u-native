// A small .zip written stored (zipw.h): a local header and the data for each entry, then the central directory and its
// end record. No ZIP64: a control profile is far below 4 GB.
#include "zipw.h"

#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

static uint32_t crc32_of(const void *data, size_t n) {
    static uint32_t table[256];
    static SDL_InitState once;
    if (SDL_ShouldInit(&once)) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ c >> 1 : c >> 1;
            table[i] = c;
        }
        SDL_SetInitialized(&once, true);
    }
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *p++) & 0xFF] ^ c >> 8;
    return ~c;
}

static void le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v & 0xFFFF), le16(p + 2, v >> 16); }

bool zip_write(const char *path, const ZipItem *items, int count) {
    if (count < 0 || count > 0xFFFF) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;
    bool ok = true;
    uint32_t *crc = SDL_calloc((size_t)count + 1, sizeof *crc), *at = SDL_calloc((size_t)count + 1, sizeof *at);
    uint32_t pos = 0;
    // (a fixed time: 1980-01-01 00:00, so the same profile makes the same bytes)
    const unsigned TIME = 0, DATE = (0 << 9) | (1 << 5) | 1;
    for (int i = 0; ok && crc && at && i < count; i++) {
        size_t nlen = strlen(items[i].name);
        if (nlen > 0xFFFF || items[i].size > 0xFFFFFFF0u || (uint64_t)pos + 30 + nlen + items[i].size > 0xFFFFFFF0u) {
            ok = false;
            break;
        }
        crc[i] = crc32_of(items[i].data, items[i].size), at[i] = pos;
        uint8_t h[30] = {0};
        le32(h, 0x04034b50), le16(h + 4, 20), le16(h + 6, 1 << 11);  // (version 2.0; bit 11: the name is UTF-8)
        le16(h + 8, 0), le16(h + 10, TIME), le16(h + 12, DATE);
        le32(h + 14, crc[i]), le32(h + 18, (uint32_t)items[i].size), le32(h + 22, (uint32_t)items[i].size);
        le16(h + 26, (unsigned)nlen), le16(h + 28, 0);
        ok = SDL_WriteIO(io, h, sizeof h) == sizeof h && SDL_WriteIO(io, items[i].name, nlen) == nlen &&
             (!items[i].size || SDL_WriteIO(io, items[i].data, items[i].size) == items[i].size);
        pos += (uint32_t)(30 + nlen + items[i].size);
    }
    uint32_t dir = pos, dir_size = 0;
    for (int i = 0; ok && crc && at && i < count; i++) {
        size_t nlen = strlen(items[i].name);
        uint8_t c[46] = {0};
        le32(c, 0x02014b50), le16(c + 4, 20), le16(c + 6, 20), le16(c + 8, 1 << 11), le16(c + 10, 0);
        le16(c + 12, TIME), le16(c + 14, DATE), le32(c + 16, crc[i]);
        le32(c + 20, (uint32_t)items[i].size), le32(c + 24, (uint32_t)items[i].size), le16(c + 28, (unsigned)nlen);
        le32(c + 42, at[i]);
        ok = SDL_WriteIO(io, c, sizeof c) == sizeof c && SDL_WriteIO(io, items[i].name, nlen) == nlen;
        dir_size += (uint32_t)(46 + nlen);
    }
    if (ok && crc && at) {
        uint8_t end[22] = {0};
        le32(end, 0x06054b50), le16(end + 8, (unsigned)count), le16(end + 10, (unsigned)count);
        le32(end + 12, dir_size), le32(end + 16, dir);
        ok = SDL_WriteIO(io, end, sizeof end) == sizeof end;
    }
    ok = ok && crc && at;
    SDL_free(crc), SDL_free(at);
    if (!SDL_CloseIO(io)) ok = false;
    if (!ok) SDL_RemovePath(path);
    return ok;
}

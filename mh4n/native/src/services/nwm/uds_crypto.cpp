// See uds_crypto.h. Written from the standards: FIPS 180-4 (SHA-1), RFC 1321 (MD5), FIPS 197 (AES).
#include "uds_crypto.h"

#include <cstring>
#include <vector>

namespace mh4n::nwm {

namespace {

u32 Rol(u32 x, int n) {
    return (x << n) | (x >> (32 - n));
}

}  // namespace

std::array<u8, 20> Sha1(const u8* data, std::size_t size) {
    u32 h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<u8> msg(data, data + size);
    const u64 bits = static_cast<u64>(size) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<u8>(bits >> (8 * i)));
    }
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        u32 w[80];
        for (int i = 0; i < 16; ++i) {
            const u8* p = &msg[chunk + 4 * i];
            w[i] = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) | p[3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            u32 f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const u32 t = Rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<u8, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[4 * i] = static_cast<u8>(h[i] >> 24);
        out[4 * i + 1] = static_cast<u8>(h[i] >> 16);
        out[4 * i + 2] = static_cast<u8>(h[i] >> 8);
        out[4 * i + 3] = static_cast<u8>(h[i]);
    }
    return out;
}

std::array<u8, 16> Md5(const u8* data, std::size_t size) {
    static const u32 kK[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int kS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    u32 h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    std::vector<u8> msg(data, data + size);
    const u64 bits = static_cast<u64>(size) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(static_cast<u8>(bits >> (8 * i)));
    }
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        u32 m[16];
        for (int i = 0; i < 16; ++i) {
            const u8* p = &msg[chunk + 4 * i];
            m[i] = p[0] | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
        }
        u32 a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            u32 f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            const u32 t = d;
            d = c;
            c = b;
            b = b + Rol(a + f + kK[i] + m[g], kS[i]);
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    std::array<u8, 16> out{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[4 * i + j] = static_cast<u8>(h[i] >> (8 * j));
        }
    }
    return out;
}

namespace {

// The S-box of FIPS 197, computed once (the multiplicative inverse in GF(2^8), then the affine map).
struct SBox {
    u8 s[256];
    SBox() {
        u8 p = 1, q = 1;
        do {
            // p * 3
            p = static_cast<u8>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
            // q / 3
            q = static_cast<u8>(q ^ (q << 1));
            q = static_cast<u8>(q ^ (q << 2));
            q = static_cast<u8>(q ^ (q << 4));
            if (q & 0x80) {
                q ^= 0x09;
            }
            const u8 x = static_cast<u8>(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^
                                         ((q << 4) | (q >> 4)));
            s[p] = static_cast<u8>(x ^ 0x63);
        } while (p != 1);
        s[0] = 0x63;
    }
};

u8 Xtime(u8 x) {
    return static_cast<u8>((x << 1) ^ ((x & 0x80) ? 0x1B : 0));
}

}  // namespace

std::array<u8, 16> Aes128EncryptBlock(const std::array<u8, 16>& key, const std::array<u8, 16>& block) {
    static const SBox sbox;
    const u8* s = sbox.s;
    // key expansion: 11 round keys
    u8 rk[176];
    std::memcpy(rk, key.data(), 16);
    u8 rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        u8 t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
        if (i % 16 == 0) {
            const u8 first = t[0];
            t[0] = static_cast<u8>(s[t[1]] ^ rcon);
            t[1] = s[t[2]];
            t[2] = s[t[3]];
            t[3] = s[first];
            rcon = Xtime(rcon);
        }
        for (int j = 0; j < 4; ++j) {
            rk[i + j] = static_cast<u8>(rk[i - 16 + j] ^ t[j]);
        }
    }
    u8 st[16];
    for (int i = 0; i < 16; ++i) {
        st[i] = static_cast<u8>(block[i] ^ rk[i]);
    }
    for (int round = 1; round <= 10; ++round) {
        // SubBytes and ShiftRows (the state is column-major: st[4 * column + row])
        u8 t[16];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                t[4 * c + r] = s[st[4 * ((c + r) % 4) + r]];
            }
        }
        if (round != 10) {
            for (int c = 0; c < 4; ++c) {
                const u8 a0 = t[4 * c], a1 = t[4 * c + 1], a2 = t[4 * c + 2], a3 = t[4 * c + 3];
                const u8 all = static_cast<u8>(a0 ^ a1 ^ a2 ^ a3);
                st[4 * c] = static_cast<u8>(a0 ^ all ^ Xtime(static_cast<u8>(a0 ^ a1)));
                st[4 * c + 1] = static_cast<u8>(a1 ^ all ^ Xtime(static_cast<u8>(a1 ^ a2)));
                st[4 * c + 2] = static_cast<u8>(a2 ^ all ^ Xtime(static_cast<u8>(a2 ^ a3)));
                st[4 * c + 3] = static_cast<u8>(a3 ^ all ^ Xtime(static_cast<u8>(a3 ^ a0)));
            }
        } else {
            std::memcpy(st, t, 16);
        }
        for (int i = 0; i < 16; ++i) {
            st[i] ^= rk[16 * round + i];
        }
    }
    std::array<u8, 16> out{};
    std::memcpy(out.data(), st, 16);
    return out;
}

void Aes128Ctr(const std::array<u8, 16>& key, std::array<u8, 16> counter, u8* data, std::size_t size) {
    for (std::size_t offset = 0; offset < size; offset += 16) {
        const std::array<u8, 16> pad = Aes128EncryptBlock(key, counter);
        const std::size_t n = size - offset < 16 ? size - offset : 16;
        for (std::size_t i = 0; i < n; ++i) {
            data[offset + i] ^= pad[i];
        }
        for (int i = 15; i >= 0; --i) {
            if (++counter[static_cast<std::size_t>(i)] != 0) {
                break;
            }
        }
    }
}

}  // namespace mh4n::nwm

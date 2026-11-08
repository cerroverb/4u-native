// The hashes and the cipher of the local-wireless beacons (N4-02): Azahar builds its beacons with CryptoPP's SHA-1
// (the network information tag's hash), MD5 (the node data's hash) and AES-128 in CTR mode (the node data, under an
// all-zero key). These are our own implementations of the standards (FIPS 180-4, RFC 1321, FIPS 197 and SP 800-38A's
// CTR with the whole 16-byte counter block incremented as a big-endian number, as CryptoPP's CTR_Mode does).
#pragma once

#include <array>
#include <cstddef>

#include "mh4n/types.h"

namespace mh4n::nwm {

std::array<u8, 20> Sha1(const u8* data, std::size_t size);
std::array<u8, 16> Md5(const u8* data, std::size_t size);

/// AES-128 of one block.
std::array<u8, 16> Aes128EncryptBlock(const std::array<u8, 16>& key, const std::array<u8, 16>& block);

/// AES-128-CTR over `data` in place (encryption and decryption are the same).
void Aes128Ctr(const std::array<u8, 16>& key, std::array<u8, 16> counter, u8* data, std::size_t size);

}  // namespace mh4n::nwm

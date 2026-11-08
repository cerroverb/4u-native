// Derived from Azahar src/core/hle/service/nwm/uds_data.cpp, uds_beacon.cpp and uds_connection.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each says "GPLv2 or any later version"): the expected
// layouts of the frames (LLC and SecureData headers, EAPoL-Start and -Logoff, authentication, association response,
// beacon tags) are the ones those sources build.
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (uds_data.cpp, uds_beacon.cpp)
// Copyright 2017 Citra Emulator Project (uds_connection.cpp)
//
// Tests of N4-02's frames and of the beacon's hashes and cipher (the standards' own test vectors). Everything is
// synthetic.
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "../src/services/nwm/uds_crypto.h"
#include "../src/services/nwm/uds_frames.h"

namespace {

using namespace mh4n;
using namespace mh4n::nwm;

std::string Hex(const u8* data, std::size_t size) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 15]);
    }
    return out;
}

template <std::size_t N> std::string Hex(const std::array<u8, N>& a) {
    return Hex(a.data(), N);
}

NodeInfo Node(u64 seed, const char* name, u16 id) {
    NodeInfo node{};
    node.friend_code_seed = seed;
    for (int i = 0; i < 10 && name[i] != '\0'; ++i) {
        node.username[i] = static_cast<u16>(name[i]);
    }
    node.network_node_id = id;
    return node;
}

NetworkInfo Network(u8 max_nodes) {
    NetworkInfo info{};
    const u8 host[6] = {0x00, 0x1F, 0x32, 0x12, 0x34, 0x56};
    std::memcpy(info.host_mac_address, host, 6);
    info.channel = 11;
    info.oui_value[0] = 0x00;
    info.oui_value[1] = 0x1F;
    info.oui_value[2] = 0x32;
    info.oui_type = 21;
    const u8 comm[4] = {0x00, 0x12, 0x63, 0x00};  // big-endian 0x00126300
    std::memcpy(info.wlan_comm_id, comm, 4);
    info.id = 7;
    const u8 net[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::memcpy(info.network_id, net, 4);
    info.total_nodes = 1;
    info.max_nodes = max_nodes;
    info.application_data_size = 3;
    info.application_data[0] = 'M';
    info.application_data[1] = 'H';
    info.application_data[2] = '4';
    return info;
}

}  // namespace

TEST_CASE("crypto: the standards' test vectors") {
    const std::string abc = "abc";
    CHECK(Hex(Sha1(reinterpret_cast<const u8*>(abc.data()), abc.size())) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(Hex(Md5(reinterpret_cast<const u8*>(abc.data()), abc.size())) == "900150983cd24fb0d6963f7d28e17f72");
    const std::string long_text(1000, 'a');
    CHECK(Hex(Sha1(reinterpret_cast<const u8*>(long_text.data()), long_text.size())) ==
          "291e9a6c66994949b57ba5e650361e98fc36b1ba");
    CHECK(Hex(Md5(nullptr, 0)) == "d41d8cd98f00b204e9800998ecf8427e");
    // FIPS 197, appendix C.1
    std::array<u8, 16> key{}, block{};
    for (int i = 0; i < 16; ++i) {
        key[i] = static_cast<u8>(i);
        block[i] = static_cast<u8>(i * 0x11);
    }
    CHECK(Hex(Aes128EncryptBlock(key, block)) == "69c4e0d86a7b0430d8cdb78070b4c55a");
    // SP 800-38A, F.5.1 (CTR-AES128.Encrypt), the first two blocks
    const u8 k[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    std::array<u8, 16> ctr_key{};
    std::memcpy(ctr_key.data(), k, 16);
    std::array<u8, 16> counter = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                                  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
    u8 data[32] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
                   0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51};
    Aes128Ctr(ctr_key, counter, data, sizeof(data));
    CHECK(Hex(data, 32) == "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff");
}

TEST_CASE("data frames: the LLC and SecureData headers, big-endian") {
    const std::vector<u8> payload = {0xAA, 0xBB, 0xCC};
    const std::vector<u8> frame = GenerateDataPayload(payload, 1, 0xFFFF, 2, 0);
    const std::vector<u8> expected = {0xAA, 0xAA, 0x03, 0, 0, 0, 0x87, 0x6D,  // LLC, SecureData
                                      0, 17, 0, 0, 0, 13, 0, 1, 0, 0, 0xFF, 0xFF, 0, 2, 0xAA, 0xBB, 0xCC};
    CHECK(frame == expected);
    CHECK(FrameEtherType(frame) == static_cast<u16>(EtherType::SecureData));
    const auto header = ParseSecureDataHeader(frame);
    REQUIRE(header);
    CHECK(header->ActualDataSize() == 3);
    CHECK(header->data_channel == 1);
    CHECK(header->dest_node_id == 0xFFFF);
    CHECK(header->src_node_id == 2);
    CHECK(!ParseSecureDataHeader(std::vector<u8>(10, 0)));
}

TEST_CASE("EAPoL: Start and Logoff round trips, the Logoff naming its console") {
    const NodeInfo me = Node(0x1122334455667788, "Hunter", 0);
    const std::vector<u8> start = GenerateEapolStartFrame(1, ConnectionType::Client, me);
    CHECK(start.size() == kLlcHeaderSize + kEapolStartSize);
    CHECK(FrameEtherType(start) == static_cast<u16>(EtherType::EAPoL));
    CHECK(EapolFrameType(start) == kEapolStartMagic);
    const auto parsed_start = ParseEapolStartFrame(start);
    REQUIRE(parsed_start);
    CHECK(parsed_start->association_id == 1);
    CHECK(parsed_start->connection_type == 1);
    CHECK(parsed_start->node.friend_code_seed == me.friend_code_seed);
    CHECK(parsed_start->node.username[0] == 'H');
    // the friend code seed is big-endian on the wire
    CHECK(start[kLlcHeaderSize + 8] == 0x11);

    const std::vector<NodeInfo> nodes = {Node(1, "Host", 1), Node(2, "Guest", 2), NodeInfo{}, NodeInfo{}};
    const MacAddress client = {0x00, 0x1F, 0x32, 0xAA, 0xBB, 0xCC};
    const std::vector<u8> logoff = GenerateEapolLogoffFrame(client, 2, nodes, 4, 2);
    CHECK(logoff.size() == kLlcHeaderSize + kEapolLogoffSize);
    CHECK(EapolFrameType(logoff) == kEapolLogoffMagic);
    const auto parsed = ParseEapolLogoffFrame(logoff);
    REQUIRE(parsed);
    CHECK(parsed->assigned_node_id == 2);
    CHECK(parsed->client_mac == client);
    CHECK(parsed->connected_nodes == 2);
    CHECK(parsed->max_nodes == 4);
    CHECK(parsed->nodes[0].network_node_id == 1);
    CHECK(parsed->nodes[1].network_node_id == 2);
    CHECK(parsed->nodes[1].username[0] == 'G');
    CHECK(parsed->nodes[2].network_node_id == 0);
    CHECK(!ParseEapolLogoffFrame(std::vector<u8>(logoff.begin(), logoff.begin() + 100)));
}

TEST_CASE("connection frames: authentication and association response") {
    const std::vector<u8> auth = GenerateAuthenticationFrame(AuthenticationSeq::SEQ2);
    CHECK(auth == std::vector<u8>{0, 0, 2, 0, 0, 0});
    CHECK(AuthenticationSeqNumber(auth) == 2);
    const std::vector<u8> assoc = GenerateAssocResponseFrame(0, 1, 0xDEADBEEF);
    CHECK(assoc.size() == 16);
    CHECK(std::string(assoc.begin() + 8, assoc.end()) == "DEADBEEF");
    const auto result = ParseAssociationResponse(assoc);
    REQUIRE(result);
    CHECK(result->status == 0);
    CHECK(result->association_id == 1);
}

TEST_CASE("beacons: the tags, the network information's SHA-1, and the node list decrypts back") {
    const NetworkInfo info = Network(4);
    const std::vector<NodeInfo> nodes = {Node(0xABCDEF, "Host", 1), NodeInfo{}, NodeInfo{}, NodeInfo{}};
    const std::vector<u8> frame = GenerateBeaconFrame(info, nodes);
    // fixed parameters (12), SSID tag (10), dummy tag (9), then the network information tag
    REQUIRE(frame.size() > 31 + 2);
    CHECK(frame[8] == 100);    // the beacon interval, little-endian
    CHECK(frame[12] == 0);     // SSID
    CHECK(frame[13] == 8);
    CHECK(frame[22] == 221);   // the dummy tag
    CHECK(frame[27] == 20);
    const std::size_t tag = 31;
    CHECK(frame[tag] == 221);
    CHECK(frame[tag + 1] == 52 + 3);
    CHECK(frame[tag + 2] == 0x00);
    CHECK(frame[tag + 5] == 21);  // the OUI type of the network information
    // the hash is over the tag from its OUI to the end of the application data, with the hash zeroed
    std::vector<u8> zeroed(frame.begin() + static_cast<std::ptrdiff_t>(tag + 2),
                           frame.begin() + static_cast<std::ptrdiff_t>(tag + 2 + 0x1F + 20 + 1 + 3));
    std::memset(zeroed.data() + 0x1F, 0, 20);
    const std::array<u8, 20> expected = Sha1(zeroed.data(), zeroed.size());
    CHECK(std::memcmp(frame.data() + tag + 2 + 0x1F, expected.data(), 20) == 0);
    CHECK(frame[tag + 2 + 0x1F + 20] == 3);  // the application data's size, then the data
    CHECK(frame[tag + 2 + 0x1F + 20 + 1] == 'M');
    // the encrypted node list: one tag (4 nodes of 0x1E bytes and 0x12 of header are under 0xFA)
    const std::size_t enc = tag + 2 + 52 + 3;
    REQUIRE(frame.size() == enc + 6 + 0x12 + 4 * 0x1E);
    CHECK(frame[enc] == 221);
    CHECK(frame[enc + 5] == 24);
    const std::vector<u8> data0(frame.begin() + static_cast<std::ptrdiff_t>(enc + 2), frame.end());
    const auto decrypted = DecryptBeaconNodes(info, data0, {});
    REQUIRE(decrypted);
    REQUIRE(decrypted->size() == 4);
    CHECK((*decrypted)[0].friend_code_seed == 0xABCDEF);
    CHECK((*decrypted)[0].network_node_id == 1);
    CHECK((*decrypted)[0].username[0] == 'H');
    CHECK((*decrypted)[1].network_node_id == 0);
    CHECK(!DecryptBeaconNodes(info, std::vector<u8>{1, 2, 3, 4}, {}));

    // 16 nodes: the list goes over two tags and still decrypts back
    const NetworkInfo big = Network(16);
    std::vector<NodeInfo> many(16);
    many[15] = Node(42, "Last", 16);
    const std::vector<u8> frame16 = GenerateBeaconFrame(big, many);
    const std::size_t enc16 = tag + 2 + 52 + 3;
    const std::size_t len0 = frame16[enc16 + 1];
    CHECK(len0 == 4 + 0xFA);
    const std::size_t second = enc16 + 2 + len0;
    REQUIRE(frame16.size() > second + 6);
    CHECK(frame16[second + 5] == 25);
    const std::vector<u8> d0(frame16.begin() + static_cast<std::ptrdiff_t>(enc16 + 2),
                             frame16.begin() + static_cast<std::ptrdiff_t>(second));
    const std::vector<u8> d1(frame16.begin() + static_cast<std::ptrdiff_t>(second + 2), frame16.end());
    const auto nodes16 = DecryptBeaconNodes(big, d0, d1);
    REQUIRE(nodes16);
    CHECK((*nodes16)[15].friend_code_seed == 42);
    CHECK((*nodes16)[15].network_node_id == 16);
}

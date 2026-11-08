// Derived from Azahar src/core/hle/service/nwm/uds_data.cpp, uds_beacon.cpp and uds_connection.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each says "GPLv2 or any later version"), with the port's
// change to GenerateEAPoLLogoffFrame (port/azahar/azahar.patch): the frames, as the header next to this file
// describes them.
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (uds_data.cpp, uds_beacon.cpp)
// Copyright 2017 Citra Emulator Project (uds_connection.cpp)
#include "uds_frames.h"

#include <algorithm>
#include <cstring>

#include <fmt/format.h>

#include "uds_crypto.h"

namespace mh4n::nwm {

namespace {

// ---- byte helpers ----

void PutBe16(std::vector<u8>& out, u16 v) {
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

void PutLe16(std::vector<u8>& out, u16 v) {
    out.push_back(static_cast<u8>(v));
    out.push_back(static_cast<u8>(v >> 8));
}

void PutBe64(std::vector<u8>& out, u64 v) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<u8>(v >> (8 * i)));
    }
}

void PutZeros(std::vector<u8>& out, std::size_t n) {
    out.insert(out.end(), n, 0);
}

u16 Be16(const u8* p) {
    return static_cast<u16>((p[0] << 8) | p[1]);
}

u16 Le16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}

u64 Be64(const u8* p) {
    u64 v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | p[i];
    }
    return v;
}

u32 Be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) | p[3];
}

// ---- tags and constants ----

constexpr u8 kTagSsid = 0;
constexpr u8 kTagVendorSpecific = 221;
constexpr u8 kNintendoTagDummy = 20;
constexpr u8 kNintendoTagEncryptedData0 = 24;
constexpr u8 kNintendoTagEncryptedData1 = 25;
constexpr u64 kDefaultNetworkUptime = 900000000;  // 15 minutes in microseconds
// Taken from a packet capture of an o3DS XL broadcasting a Super Smash Bros. 4 lobby (Azahar's note).
constexpr u16 kDefaultExtraCapabilities = 0x0431;
constexpr u8 kUdsBeaconSsidSize = 8;
constexpr std::size_t kEncryptedDataSizeCutoff = 0xFA;
constexpr std::size_t kBeaconNodeInfoSize = 0x1E;
constexpr std::size_t kBeaconDataSize = 0x12;  // the MD5 hash and the bitmask
constexpr std::size_t kEapolNodeInfoSize = 0x28;
// NWM's beacon data key: Azahar stubs it with zeros, which is all its consoles need (a real 3DS would want the real
// key, and the port does not have it).
constexpr std::array<u8, 16> kNwmBeaconKey = {};

void PutLlcHeader(std::vector<u8>& out, EtherType protocol) {
    // 802.2 with SNAP: dsap and ssap 0xAA, unnumbered information, OUI 0, then the protocol
    out.push_back(0xAA);
    out.push_back(0xAA);
    out.push_back(0x03);
    PutZeros(out, 3);
    PutBe16(out, static_cast<u16>(protocol));
}

void PutEapolNodeInfo(std::vector<u8>& out, const NodeInfo& node, bool with_node_id) {
    PutBe64(out, node.friend_code_seed);
    for (const u16 c : node.username) {
        PutBe16(out, c);
    }
    PutZeros(out, 4);
    PutBe16(out, with_node_id ? node.network_node_id : 0);
    PutZeros(out, 6);
}

NodeInfo ReadEapolNodeInfo(const u8* p) {
    NodeInfo node{};
    node.friend_code_seed = Be64(p);
    for (int i = 0; i < 10; ++i) {
        node.username[i] = Be16(p + 8 + 2 * i);
    }
    node.network_node_id = Be16(p + 0x20);
    return node;
}

// The counter of the beacon's AES-CTR (BeaconDataCryptoCTR): the host's MAC, the wlan comm id (little-endian), the id,
// a zero byte, and the network id (little-endian).
std::array<u8, 16> BeaconCryptoCounter(const NetworkInfo& info) {
    std::array<u8, 16> counter{};
    std::memcpy(counter.data(), info.host_mac_address, 6);
    const u32 wlan_comm_id = info.WlanCommId();
    const u32 network_id = info.NetworkId();
    for (int i = 0; i < 4; ++i) {
        counter[6 + i] = static_cast<u8>(wlan_comm_id >> (8 * i));
        counter[12 + i] = static_cast<u8>(network_id >> (8 * i));
    }
    counter[10] = info.id;
    counter[11] = 0;
    return counter;
}

std::vector<u8> EncryptedBeaconData(const NetworkInfo& info, const std::vector<NodeInfo>& nodes) {
    std::vector<u8> buffer(kBeaconDataSize, 0);  // the hash, then the bitmask (0)
    for (const NodeInfo& node : nodes) {
        PutBe64(buffer, node.friend_code_seed);
        for (const u16 c : node.username) {
            PutBe16(buffer, c);
        }
        PutBe16(buffer, node.network_node_id);
    }
    const std::array<u8, 16> hash = Md5(buffer.data() + 16, buffer.size() - 16);
    std::memcpy(buffer.data(), hash.data(), hash.size());
    Aes128Ctr(kNwmBeaconKey, BeaconCryptoCounter(info), buffer.data(), buffer.size());
    return buffer;
}

}  // namespace

u32 NetworkInfo::WlanCommId() const {
    return Be32(wlan_comm_id);
}

u32 NetworkInfo::NetworkId() const {
    return Be32(network_id);
}

MacAddress NetworkInfo::HostMac() const {
    MacAddress mac{};
    std::memcpy(mac.data(), host_mac_address, 6);
    return mac;
}

void NetworkInfo::SetHostMac(const MacAddress& mac) {
    std::memcpy(host_mac_address, mac.data(), 6);
}

// ---- data frames ----

std::optional<u16> FrameEtherType(std::span<const u8> frame) {
    if (frame.size() < kLlcHeaderSize) {
        return std::nullopt;
    }
    return Be16(frame.data() + 6);
}

std::vector<u8> GenerateDataPayload(std::span<const u8> data, u8 channel, u16 dest_node, u16 src_node,
                                    u16 sequence_number) {
    std::vector<u8> out;
    out.reserve(kLlcHeaderSize + kSecureDataHeaderSize + data.size());
    PutLlcHeader(out, EtherType::SecureData);
    const u16 size = static_cast<u16>(data.size());
    PutBe16(out, static_cast<u16>(size + kSecureDataHeaderSize));
    PutZeros(out, 2);
    // everything except the first 4 bytes of the header (Azahar: perhaps another container protocol's header)
    PutBe16(out, static_cast<u16>(size + kSecureDataHeaderSize - 4));
    out.push_back(0);  // frames sent by the application are never UDS management frames
    out.push_back(channel);
    PutBe16(out, sequence_number);
    PutBe16(out, dest_node);
    PutBe16(out, src_node);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::optional<SecureDataHeader> ParseSecureDataHeader(std::span<const u8> frame) {
    if (frame.size() < kLlcHeaderSize + kSecureDataHeaderSize) {
        return std::nullopt;
    }
    const u8* p = frame.data() + kLlcHeaderSize;
    SecureDataHeader header;
    header.protocol_size = Be16(p);
    header.securedata_size = Be16(p + 4);
    header.is_management = p[6];
    header.data_channel = p[7];
    header.sequence_number = Be16(p + 8);
    header.dest_node_id = Be16(p + 10);
    header.src_node_id = Be16(p + 12);
    return header;
}

std::optional<u16> EapolFrameType(std::span<const u8> frame) {
    if (frame.size() < kLlcHeaderSize + 2) {
        return std::nullopt;
    }
    return Be16(frame.data() + kLlcHeaderSize);
}

std::vector<u8> GenerateEapolStartFrame(u16 association_id, ConnectionType conn_type, const NodeInfo& node) {
    std::vector<u8> out;
    out.reserve(kLlcHeaderSize + kEapolStartSize);
    PutLlcHeader(out, EtherType::EAPoL);
    PutBe16(out, kEapolStartMagic);
    PutBe16(out, association_id);
    out.push_back(static_cast<u8>(conn_type));
    PutZeros(out, 3);
    // The network_node_id and unknown bytes seem to be uninitialized in the NWM module (Azahar's note): zero here.
    PutEapolNodeInfo(out, node, false);
    return out;
}

std::optional<EapolStart> ParseEapolStartFrame(std::span<const u8> frame) {
    if (frame.size() < kLlcHeaderSize + kEapolStartSize) {
        return std::nullopt;
    }
    const u8* p = frame.data() + kLlcHeaderSize;
    EapolStart start;
    start.association_id = Be16(p + 2);
    start.connection_type = p[4];
    start.node = ReadEapolNodeInfo(p + 8);
    return start;
}

std::vector<u8> GenerateEapolLogoffFrame(const MacAddress& client_mac, u16 network_node_id,
                                         const std::vector<NodeInfo>& nodes, u8 max_nodes, u8 total_nodes) {
    std::vector<u8> out;
    out.reserve(kLlcHeaderSize + kEapolLogoffSize);
    PutLlcHeader(out, EtherType::EAPoL);
    PutBe16(out, kEapolLogoffMagic);
    PutZeros(out, 2);
    PutBe16(out, network_node_id);
    // the console the node id is for: the packet goes to every member (the port's fix, see HandleEapol)
    out.insert(out.end(), client_mac.begin(), client_mac.end());
    PutZeros(out, 6);
    out.push_back(total_nodes);
    out.push_back(max_nodes);
    PutZeros(out, 4);
    for (std::size_t index = 0; index < kUdsMaxNodes; ++index) {
        if (index < max_nodes && index < nodes.size()) {
            PutEapolNodeInfo(out, nodes[index], true);
        } else {
            PutZeros(out, kEapolNodeInfoSize);
        }
    }
    return out;
}

std::optional<EapolLogoff> ParseEapolLogoffFrame(std::span<const u8> frame) {
    if (frame.size() < kLlcHeaderSize + kEapolLogoffSize) {
        return std::nullopt;
    }
    const u8* p = frame.data() + kLlcHeaderSize;
    EapolLogoff logoff;
    logoff.assigned_node_id = Be16(p + 4);
    std::memcpy(logoff.client_mac.data(), p + 6, 6);
    logoff.connected_nodes = p[0x12];
    logoff.max_nodes = p[0x13];
    for (std::size_t i = 0; i < kUdsMaxNodes; ++i) {
        logoff.nodes[i] = ReadEapolNodeInfo(p + 0x18 + i * kEapolNodeInfoSize);
    }
    return logoff;
}

// ---- connection frames ----

std::vector<u8> GenerateAuthenticationFrame(AuthenticationSeq seq) {
    std::vector<u8> out;
    PutLe16(out, 0);  // open system
    PutLe16(out, static_cast<u16>(seq));
    PutLe16(out, 0);  // successful
    return out;
}

std::optional<u16> AuthenticationSeqNumber(std::span<const u8> body) {
    if (body.size() < 6) {
        return std::nullopt;
    }
    return Le16(body.data() + 2);
}

std::vector<u8> GenerateAssocResponseFrame(u16 status, u16 association_id, u32 network_id) {
    std::vector<u8> out;
    PutLe16(out, kDefaultExtraCapabilities);
    PutLe16(out, status);
    constexpr u16 kAssociationIdMagic = 0xC000;
    PutLe16(out, static_cast<u16>(association_id | kAssociationIdMagic));
    // an SSID tag with the network id as 8 hex digits
    out.push_back(kTagSsid);
    out.push_back(8);
    const std::string name = fmt::format("{:08X}", network_id);
    out.insert(out.end(), name.begin(), name.begin() + 8);
    return out;
}

std::optional<AssociationResult> ParseAssociationResponse(std::span<const u8> body) {
    if (body.size() < 6) {
        return std::nullopt;
    }
    AssociationResult result;
    result.status = Le16(body.data() + 2);
    constexpr u16 kAssociationIdMask = 0x3FFF;
    result.association_id = static_cast<u16>(Le16(body.data() + 4) & kAssociationIdMask);
    return result;
}

// ---- beacons ----

std::vector<u8> GenerateBeaconFrame(const NetworkInfo& info, const std::vector<NodeInfo>& nodes) {
    std::vector<u8> out;
    // fixed parameters: the timestamp, the beacon interval and the capabilities (little-endian)
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<u8>(kDefaultNetworkUptime >> (8 * i)));
    }
    PutLe16(out, kDefaultBeaconInterval);
    PutLe16(out, kDefaultExtraCapabilities);
    // the SSID tag: 8 zero bytes
    out.push_back(kTagSsid);
    out.push_back(kUdsBeaconSsidSize);
    PutZeros(out, kUdsBeaconSsidSize);
    // the Great N's dummy tag (values from a packet capture, Azahar's note)
    out.insert(out.end(), {kTagVendorSpecific, 7, kNintendoOui[0], kNintendoOui[1], kNintendoOui[2], kNintendoTagDummy,
                           0x0A, 0x00, 0x00});
    // the network information tag: the network structure from its OUI (0x1F bytes), its SHA-1, the application data
    const u8 appdata_size = static_cast<u8>(std::min<std::size_t>(info.application_data_size, kApplicationDataSize));
    std::vector<u8> tag;
    tag.push_back(kTagVendorSpecific);
    tag.push_back(static_cast<u8>(54 - 2 + appdata_size));
    tag.insert(tag.end(), info.oui_value, info.oui_value + 0x1F);
    PutZeros(tag, 20);  // the hash, filled below
    tag.push_back(appdata_size);
    tag.insert(tag.end(), info.application_data, info.application_data + appdata_size);
    const std::array<u8, 20> sha = Sha1(tag.data() + 2, tag.size() - 2);
    std::memcpy(tag.data() + 2 + 0x1F, sha.data(), sha.size());
    out.insert(out.end(), tag.begin(), tag.end());
    // the node list, encrypted, in one tag or two
    const std::vector<u8> encrypted = EncryptedBeaconData(info, nodes);
    const std::size_t first = std::min(kEncryptedDataSizeCutoff, encrypted.size());
    out.insert(out.end(), {kTagVendorSpecific, static_cast<u8>(4 + first), kNintendoOui[0], kNintendoOui[1],
                           kNintendoOui[2], kNintendoTagEncryptedData0});
    out.insert(out.end(), encrypted.begin(), encrypted.begin() + static_cast<std::ptrdiff_t>(first));
    if (encrypted.size() > kEncryptedDataSizeCutoff) {
        const std::size_t second = std::min<std::size_t>(encrypted.size() - kEncryptedDataSizeCutoff, 255 - 4);
        out.insert(out.end(), {kTagVendorSpecific, static_cast<u8>(4 + second), kNintendoOui[0], kNintendoOui[1],
                               kNintendoOui[2], kNintendoTagEncryptedData1});
        out.insert(out.end(), encrypted.begin() + static_cast<std::ptrdiff_t>(kEncryptedDataSizeCutoff),
                   encrypted.begin() + static_cast<std::ptrdiff_t>(kEncryptedDataSizeCutoff + second));
    }
    return out;
}

std::optional<std::vector<NodeInfo>> DecryptBeaconNodes(const NetworkInfo& info, std::span<const u8> data0,
                                                        std::span<const u8> data1) {
    if (data0.size() < 4 || data0[0] != kNintendoOui[0] || data0[1] != kNintendoOui[1] ||
        data0[2] != kNintendoOui[2] || data0[3] != kNintendoTagEncryptedData0) {
        return std::nullopt;
    }
    std::vector<u8> data(data0.begin() + 4, data0.end());
    if (data1.size() >= 4) {
        data.insert(data.end(), data1.begin() + 4, data1.end());
    }
    Aes128Ctr(kNwmBeaconKey, BeaconCryptoCounter(info), data.data(), data.size());
    // (the MD5 of the data is not checked, as in Azahar)
    std::vector<NodeInfo> nodes;
    for (std::size_t i = 0; i < info.max_nodes && i < kUdsMaxNodes; ++i) {
        NodeInfo node{};
        const std::size_t offset = kBeaconDataSize + i * kBeaconNodeInfoSize;
        if (offset + kBeaconNodeInfoSize <= data.size()) {
            const u8* p = data.data() + offset;
            node.friend_code_seed = Be64(p);
            for (int j = 0; j < 10; ++j) {
                node.username[j] = Be16(p + 8 + 2 * j);
            }
            node.network_node_id = Be16(p + 0x1C);
        }
        nodes.push_back(node);
    }
    return nodes;
}

}  // namespace mh4n::nwm

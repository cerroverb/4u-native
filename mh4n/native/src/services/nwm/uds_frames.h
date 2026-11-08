// Derived from Azahar src/core/hle/service/nwm/nwm_uds.h, uds_data.h, uds_beacon.h, uds_connection.h and uds_common.h
// at 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: each says "GPLv2 or any later version"), and the
// port's change to uds_data.cpp (port/azahar/azahar.patch: the EAPoL-Logoff names the console it is for): the
// structures of the local-wireless service and the frames consoles exchange through a room.
// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project (nwm_uds.h)
// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project (uds_data.h, uds_data.cpp)
// Copyright 2017 Citra Emulator Project (uds_beacon.h, uds_connection.h)
// Copyright 2025 Citra Emulator Project / Azahar Emulator Project (uds_common.h)
//
// The structures that cross the IPC boundary (NodeInfo, ConnectionStatus, NetworkInfo, the beacon entry header) keep
// Azahar's in-memory layout; the frames are built and read byte by byte, big-endian where Azahar's structures are
// (u16_be, u32_be, u64_be), so no host's byte order or padding gets into them. Frames that are too short to read are
// refused (Azahar copies past their end).
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "mh4n/types.h"

namespace mh4n::nwm {

using MacAddress = std::array<u8, 6>;

constexpr std::size_t kApplicationDataSize = 0xC8;
constexpr u32 kUdsMaxNodes = 16;
constexpr u16 kNodeIdSpec = 0;  // NodeIDSpec: the node id of a spectator
constexpr std::array<u8, 3> kNintendoOui = {0x00, 0x1F, 0x32};
constexpr u16 kDefaultBeaconInterval = 100;  // in time units of 1.024 ms

enum class ConnectionType : u8 { Client = 0x1, Spectator = 0x2 };

struct NodeInfo {
    u64 friend_code_seed;
    u16 username[10];
    u8 padding0[4];
    u16 network_node_id;
    u8 padding1[6];
};
static_assert(sizeof(NodeInfo) == 40, "NodeInfo has incorrect size.");

// Azahar's NetworkInfo, with the big-endian and padding members as bytes.
struct NetworkInfo {
    u8 host_mac_address[6];
    u8 channel;
    u8 unk1;
    u8 initialized;
    u8 padding0[3];
    u8 oui_value[3];
    u8 oui_type;
    u8 wlan_comm_id[4];  // big-endian
    u8 id;
    u8 padding1;
    u8 attributes[2];    // big-endian
    u8 network_id[4];    // big-endian
    u8 total_nodes;
    u8 max_nodes;
    u8 padding2[2];
    u8 padding3[0x1F];
    u8 application_data_size;
    u8 application_data[kApplicationDataSize];

    u32 WlanCommId() const;
    u32 NetworkId() const;
    MacAddress HostMac() const;
    void SetHostMac(const MacAddress& mac);
};
static_assert(offsetof(NetworkInfo, oui_value) == 0xC, "oui_value is at the wrong offset.");
static_assert(offsetof(NetworkInfo, wlan_comm_id) == 0x10, "wlancommid is at the wrong offset.");
static_assert(offsetof(NetworkInfo, application_data_size) == 0x3F, "application_data_size offset");
static_assert(sizeof(NetworkInfo) == 0x108, "NetworkInfo has incorrect size.");

struct BeaconEntryHeader {
    u32 total_size;
    u8 padding0;
    u8 wifi_channel;
    u8 padding1[2];
    u8 mac_address[6];
    u8 padding2[6];
    u32 unk_size;
    u32 header_size;
};
static_assert(sizeof(BeaconEntryHeader) == 0x1C, "BeaconEntryHeader has incorrect size.");

struct BeaconDataReplyHeader {
    u32 max_output_size;
    u32 total_size;
    u32 total_entries;
};
static_assert(sizeof(BeaconDataReplyHeader) == 12, "BeaconDataReplyHeader has incorrect size.");

// ---- data frames (uds_data) ---------------------------------------------------------------------------------------

enum class EtherType : u16 { SecureData = 0x876D, EAPoL = 0x888E };
constexpr std::size_t kLlcHeaderSize = 8;
constexpr std::size_t kSecureDataHeaderSize = 14;
constexpr u16 kEapolStartMagic = 0x201;
constexpr u16 kEapolLogoffMagic = 0x202;
constexpr std::size_t kEapolStartSize = 0x30;
constexpr std::size_t kEapolLogoffSize = 0x298;

struct SecureDataHeader {
    u16 protocol_size = 0;
    u16 securedata_size = 0;
    u8 is_management = 0;
    u8 data_channel = 0;
    u16 sequence_number = 0;
    u16 dest_node_id = 0;
    u16 src_node_id = 0;
    u32 ActualDataSize() const { return protocol_size >= kSecureDataHeaderSize ? protocol_size - kSecureDataHeaderSize : 0; }
};

/// The LLC header's protocol (none for a frame shorter than the header).
std::optional<u16> FrameEtherType(std::span<const u8> frame);
std::vector<u8> GenerateDataPayload(std::span<const u8> data, u8 channel, u16 dest_node, u16 src_node,
                                    u16 sequence_number);
std::optional<SecureDataHeader> ParseSecureDataHeader(std::span<const u8> frame);

/// The EAPoL type (Start / Logoff) of an EAPoL frame.
std::optional<u16> EapolFrameType(std::span<const u8> frame);
std::vector<u8> GenerateEapolStartFrame(u16 association_id, ConnectionType conn_type, const NodeInfo& node);
struct EapolStart {
    u16 association_id = 0;
    u8 connection_type = 0;
    NodeInfo node{};  // network_node_id as sent (uninitialised on a console)
};
std::optional<EapolStart> ParseEapolStartFrame(std::span<const u8> frame);

/// `client_mac` is the console the node id is for (the port's fix: Azahar's frame has zeros there).
std::vector<u8> GenerateEapolLogoffFrame(const MacAddress& client_mac, u16 network_node_id,
                                         const std::vector<NodeInfo>& nodes, u8 max_nodes, u8 total_nodes);
struct EapolLogoff {
    u16 assigned_node_id = 0;
    MacAddress client_mac{};
    u8 connected_nodes = 0;
    u8 max_nodes = 0;
    std::array<NodeInfo, kUdsMaxNodes> nodes{};
};
std::optional<EapolLogoff> ParseEapolLogoffFrame(std::span<const u8> frame);

// ---- connection frames (uds_connection) ---------------------------------------------------------------------------

enum class AuthenticationSeq : u16 { SEQ1 = 1, SEQ2 = 2 };
std::vector<u8> GenerateAuthenticationFrame(AuthenticationSeq seq);
std::optional<u16> AuthenticationSeqNumber(std::span<const u8> body);
std::vector<u8> GenerateAssocResponseFrame(u16 status, u16 association_id, u32 network_id);
struct AssociationResult {
    u16 status = 0;
    u16 association_id = 0;
};
std::optional<AssociationResult> ParseAssociationResponse(std::span<const u8> body);

// ---- beacons (uds_beacon) -----------------------------------------------------------------------------------------

/// The 802.11 beacon frame of a hosted network, from the management frame header: its fixed parameters, the SSID
/// tag, and the Great N's tags (the dummy one, the network information with its SHA-1, and the node list, encrypted).
std::vector<u8> GenerateBeaconFrame(const NetworkInfo& network_info, const std::vector<NodeInfo>& nodes);

/// DecryptBeaconData: the node list of a scanned network from its two encrypted tags (each with its 4 bytes of OUI
/// and type), `max_nodes` entries, as NodeInfo. None when the first tag is not the Great N's EncryptedData0.
std::optional<std::vector<NodeInfo>> DecryptBeaconNodes(const NetworkInfo& network_info, std::span<const u8> data0,
                                                        std::span<const u8> data1);

}  // namespace mh4n::nwm

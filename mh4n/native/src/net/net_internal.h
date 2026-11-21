// What the room client, the room server and the lobby client of native/src/net share.
#pragma once

struct _ENetPeer;

namespace mh4n::rooms::detail {

/// enet_initialize, once per process (Azahar's Network::Init). False if the network library could not start.
bool EnsureEnet();

/// ENet's packet throttle, on a connected link: it still opens, it never closes (deceleration 0). Every room message is
/// reliable, and the throttle scales the reliable window (64 KB) down to one MTU whenever an acknowledgement comes later
/// than the lowest round trip plus twice its variation: on a hub's slow link (some 800 ms, with a few ms of variation)
/// a phone's late acknowledgements or a datagram that came as the copy (mh4n/hubs.h) did that within a minute, and the
/// room's frames then waited a whole round trip at a time for the window (a host silent for 0.6 to 1 s, which the
/// game takes for gone). The setting goes to the other end too (ENet's THROTTLE_CONFIGURE), so a link with an older
/// end keeps its window as well.
void KeepWindowOpen(_ENetPeer* peer);

}  // namespace mh4n::rooms::detail

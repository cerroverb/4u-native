// What the room client, the room server and the lobby client of native/src/net share.
#pragma once

namespace mh4n::rooms::detail {

/// enet_initialize, once per process (Azahar's Network::Init). False if the network library could not start.
bool EnsureEnet();

}  // namespace mh4n::rooms::detail

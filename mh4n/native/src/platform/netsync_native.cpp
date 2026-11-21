// The net sync's bookkeeping on the native runtime: see netsync_native.h.
#include "platform/netsync_native.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace mh4n::netsync {

std::vector<std::uint8_t> Encode(double clock_ms, const MacAddress& waiting_for, std::uint8_t flags) {
    std::vector<std::uint8_t> data(24, 0);
    std::memcpy(data.data(), &kMagic, 4);
    std::memcpy(data.data() + 4, &kVersion, 4);
    std::memcpy(data.data() + 8, &clock_ms, 8);
    std::memcpy(data.data() + 16, waiting_for.data(), waiting_for.size());
    data[22] = flags;
    return data;
}

std::optional<Report> Decode(const std::vector<std::uint8_t>& data) {
    if (data.size() < 24) return std::nullopt;
    std::uint32_t magic = 0;
    Report r;
    std::memcpy(&magic, data.data(), 4);
    std::memcpy(&r.version, data.data() + 4, 4);
    std::memcpy(&r.clock_ms, data.data() + 8, 8);
    std::memcpy(r.waiting_for.data(), data.data() + 16, r.waiting_for.size());
    if (magic != kMagic || r.version < 1 || r.version > kVersion || !std::isfinite(r.clock_ms)) return std::nullopt;
    r.flags = r.version >= 2 ? data[22] : 0;
    return r;
}

std::string MacText(const MacAddress& mac) {
    char b[24];
    std::snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return b;
}

void Peers::Configure(Port::NetSyncMode mode, double slack_ms) {
    std::lock_guard lock(mutex_);
    mode_ = mode;
    slack_ms_ = slack_ms > 0.0 ? slack_ms : kDefaultSlackMs;
    peers_.clear();
}

Port::NetSyncMode Peers::Mode() const {
    std::lock_guard lock(mutex_);
    return mode_;
}

double Peers::Slack() const {
    std::lock_guard lock(mutex_);
    return slack_ms_;
}

void Peers::Clear() {
    std::lock_guard lock(mutex_);
    peers_.clear();
}

void Peers::SetSelf(const MacAddress& self) {
    std::lock_guard lock(mutex_);
    self_ = self;
}

void Peers::OnReport(const MacAddress& from, const Report& report, double now_s) {
    std::lock_guard lock(mutex_);
    if (mode_ == Port::NetSyncMode::Off || from == self_) return;
    Peer& p = peers_[from];
    p.clock_ms = report.clock_ms;
    p.waiting_for = report.waiting_for;
    p.heard_s = now_s;
    p.reported = true;
}

void Peers::OnData(const MacAddress& from, const MacAddress& to) {
    std::lock_guard lock(mutex_);
    if (mode_ == Port::NetSyncMode::Off || from == self_ || to != self_) return;
    ++peers_[from].data_packets;
}

int Peers::Syncing() const {
    std::lock_guard lock(mutex_);
    int n = 0;
    for (const auto& [mac, p] : peers_) n += p.syncing;
    return n;
}

Peers::Ahead Peers::Compute(double own_ms, double now_s, bool restart, std::vector<std::string>* log) {
    std::lock_guard lock(mutex_);
    Ahead worst;
    char line[160];
    for (auto& [mac, p] : peers_) {
        if (p.data_seen != p.data_packets) {
            p.data_seen = p.data_packets;
            p.data_at_ms = own_ms;
        }
        const bool alive = p.reported && now_s - p.heard_s < kPeerTimeoutS;
        const bool together = mode_ == Port::NetSyncMode::All || own_ms - p.data_at_ms < kSessionMs;
        if (!alive || !together) {
            if (p.syncing && log) {
                std::snprintf(line, sizeof line, "net sync: no longer with %s (%s) at %.0f ms", MacText(mac).c_str(),
                              alive ? "no game data" : "no clock", own_ms);
                log->push_back(line);
            }
            p.syncing = false;
            continue;
        }
        if (!p.syncing || restart) {
            if (!p.syncing && log) {
                std::snprintf(line, sizeof line, "net sync: with %s from %.0f ms", MacText(mac).c_str(), own_ms);
                log->push_back(line);
            }
            p.lead.Start(own_ms, p.clock_ms);
            p.syncing = true;
            continue;
        }
        p.lead.Update(own_ms, p.clock_ms);
        if (p.waiting_for == self_) p.lead.Limit(slack_ms_);  // (it waits for us: we don't wait for it)
        if (p.lead.Value() > worst.lead) worst = {mac, p.lead.Value()};
    }
    return worst;
}

}  // namespace mh4n::netsync

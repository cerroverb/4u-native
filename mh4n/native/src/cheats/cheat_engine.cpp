// Derived from Azahar src/core/cheats/cheats.cpp and src/core/cheats/cheats.h at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-2.0-or-later: both say "GPLv2 or any later version"):
// CheatEngine's list (AddCheat, RemoveCheat, UpdateCheat), LoadCheatFile and SaveCheatFile (the file's
// name and folder, every cheat's ToString in turn) and RunCallback (the cheats that are on, in order,
// under a shared lock). See the header next to this file for the departures.
// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project (cheats.cpp)
// Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project (cheats.h)
#include "cheats/cheat_engine.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>

#include "../common/utf8.h"
#include "mh4n/log.h"

namespace mh4n::cheats {

namespace {
constexpr const char* kLog = "Cheats";

std::mutex g_current_mutex;
std::shared_ptr<CheatEngine> g_current;
}  // namespace

std::string CheatFilePath(const std::string& user_folder, u64 title_id) {
    std::string dir = user_folder;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '/';
    }
    return fmt::format("{}cheats/{:016X}.txt", dir, title_id);
}

CheatEngine::CheatEngine(std::string path) : path_(std::move(path)) {}

std::size_t CheatEngine::Load() {
    auto gateway_cheats = path_.empty() ? std::vector<std::shared_ptr<GatewayCheat>>{} : GatewayCheat::LoadFile(path_);
    const std::size_t count = gateway_cheats.size();
    {
        std::unique_lock lock{cheats_list_mutex_};
        cheats_list_ = std::move(gateway_cheats);
        Changed();
    }
    return count;
}

bool CheatEngine::Save(std::string* error) const {
    if (path_.empty()) {
        if (error) *error = "no cheats file";
        return false;
    }
    std::vector<std::shared_ptr<GatewayCheat>> cheats;
    {
        std::shared_lock lock{cheats_list_mutex_};
        cheats = cheats_list_;
    }
    std::string text;
    for (const auto& cheat : cheats) {
        text += cheat->ToString();
    }
    const std::filesystem::path path = utf8::PathFromUtf8(path_);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        if (!file) {
            if (error) *error = "cannot write " + path_;
            MH4N_LOG(Error, kLog, "cannot write {}", path_);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "cannot write " + path_ + ": " + ec.message();
        MH4N_LOG(Error, kLog, "cannot write {}: {}", path_, ec.message());
        return false;
    }
    return true;
}

std::vector<CheatInfo> CheatEngine::List() const {
    std::shared_lock lock{cheats_list_mutex_};
    std::vector<CheatInfo> out;
    out.reserve(cheats_list_.size());
    const u32 begin = code_begin_.load(), end = code_end_.load();
    for (const auto& cheat : cheats_list_) {
        CheatInfo info;
        info.name = cheat->GetName();
        info.comments = cheat->GetComments();
        info.code = cheat->GetCode();
        info.enabled = cheat->IsEnabled();
        info.invalid_lines = cheat->InvalidLines();
        info.touches_code = end > begin && cheat->TouchesCode(begin, end);
        out.push_back(std::move(info));
    }
    return out;
}

std::size_t CheatEngine::Count() const {
    std::shared_lock lock{cheats_list_mutex_};
    return cheats_list_.size();
}

std::size_t CheatEngine::EnabledCount() const {
    std::shared_lock lock{cheats_list_mutex_};
    std::size_t n = 0;
    for (const auto& cheat : cheats_list_) {
        n += cheat->IsEnabled() ? 1 : 0;
    }
    return n;
}

u64 CheatEngine::Revision() const {
    return revision_.load();
}

bool CheatEngine::SetEnabled(std::size_t index, bool enabled) {
    std::shared_lock lock{cheats_list_mutex_};
    if (index >= cheats_list_.size()) {
        return false;
    }
    cheats_list_[index]->SetEnabled(enabled);
    Changed();
    return true;
}

bool CheatEngine::SetEnabled(const std::string& name, bool enabled) {
    std::shared_lock lock{cheats_list_mutex_};
    for (const auto& cheat : cheats_list_) {
        if (cheat->GetName() == name) {
            cheat->SetEnabled(enabled);
            Changed();
            return true;
        }
    }
    return false;
}

bool CheatEngine::Remove(std::size_t index) {
    std::unique_lock lock{cheats_list_mutex_};
    if (index >= cheats_list_.size()) {
        MH4N_LOG(Error, kLog, "Invalid index {}", index);
        return false;
    }
    cheats_list_.erase(cheats_list_.begin() + static_cast<std::ptrdiff_t>(index));
    Changed();
    return true;
}

int CheatEngine::Import(const std::string& path, std::string* error) {
    std::ifstream probe(utf8::PathFromUtf8(path), std::ios::binary);
    if (!probe) {
        if (error) *error = "unreadable";
        return -1;
    }
    probe.close();
    auto incoming = GatewayCheat::LoadFile(path);
    // (lines before any [Name] make a cheat without a name in Azahar: a file that is not a cheats file)
    incoming.erase(std::remove_if(incoming.begin(), incoming.end(),
                                  [](const std::shared_ptr<GatewayCheat>& c) { return c->GetName().empty(); }),
                   incoming.end());
    if (incoming.empty()) {
        if (error) *error = "no_cheats";
        return -1;
    }
    std::unique_lock lock{cheats_list_mutex_};
    for (auto& cheat : incoming) {
        cheat->SetEnabled(false);  // (each one is turned on by the player)
        auto same = std::find_if(cheats_list_.begin(), cheats_list_.end(),
                                 [&](const std::shared_ptr<GatewayCheat>& c) { return c->GetName() == cheat->GetName(); });
        if (same != cheats_list_.end()) {
            *same = cheat;  // UpdateCheat
        } else {
            cheats_list_.push_back(cheat);  // AddCheat
        }
    }
    Changed();
    return static_cast<int>(incoming.size());
}

void CheatEngine::SetCodeRange(u32 begin, u32 end) {
    code_begin_.store(begin);
    code_end_.store(end);
}

void CheatEngine::Run(CheatTarget& target) const {
    std::shared_lock lock{cheats_list_mutex_};
    for (const auto& cheat : cheats_list_) {
        if (cheat->IsEnabled()) {
            cheat->Execute(target);
        }
    }
}

std::shared_ptr<CheatEngine> Current() {
    std::lock_guard lock(g_current_mutex);
    return g_current;
}

void SetCurrent(std::shared_ptr<CheatEngine> engine) {
    std::lock_guard lock(g_current_mutex);
    g_current = std::move(engine);
}

}  // namespace mh4n::cheats

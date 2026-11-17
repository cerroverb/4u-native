// Derived from Azahar src/core/hle/service/am/am_app.cpp and src/core/hle/service/am/am.cpp at
// 5f3306d13cab40bbd5cf38f2cf3ae8b6e6eedca6 (GPL-3.0-or-later: am.cpp says "GPLv3 or any later
// version"; am_app.cpp says "GPLv2 or any later version"): the command table of am:app with its
// session limit, the title id checks (TID_HIGH_DLC, TID_HIGH_UPDATE) and result codes of the DLC and
// patch queries, the answers for an installed DLC title (GetDLCContentInfoCount: the title metadata's
// content count; FindDLCContentInfos and ListDLCContentInfos: the ContentInfo of each content, with the
// OWNERSHIP_OWNED and OWNERSHIP_DOWNLOADED bits, and 0xFFFFFFFF for an index the title does not have;
// GetDLCTitleInfos: the TitleInfo of each title, from its title metadata) and those of a console with no
// DLC, update or ticket installed.
// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project (am.cpp)
// Copyright 2014 Citra Emulator Project (am_app.cpp)
//
// am:app (N1-19): the application's view of the title manager. Azahar answers from the NAND and SD title
// folders; here the add-on content titles the player brought (1.0.1, ../fs/dlc_titles.h: found in the
// save folder, the user folder's sdmc/ and nand/, or --dlc-folder) are the installed ones, and the runtime
// has no other installed titles and no tickets:
//  - the title queries check the title ids as Azahar does (DLC ids for the DLC queries, update ids for
//    GetPatchTitleInfos): an id with the wrong prefix is answered InvalidTIDInList; a DLC id of a title
//    the player brought is found, any other id with the right prefix finds nothing ("not found"); an
//    empty list succeeds with nothing written;
//  - GetDLCContentInfoCount answers the content count of a title that is there, 1 for one that is not
//    ("the number of content infos plus one" Azahar gives for a missing TMD); ListDLCContentInfos and
//    FindDLCContentInfos write the infos of a title that is there and nothing for one that is not;
//  - no ticket is installed: every content is owned (Azahar's answer for a title without a ticket), and
//    ListDataTitleTicketInfos lists none;
//  - GetNumDataTitleTickets, GetItemRights, IsDataTitleInUse, IsExternalTitleDatabaseInitialized,
//    GetNumExistingContentInfos and ListExistingContentInfos are nullptr entries in Azahar: answered
//    as an unknown command (logged once; the command id, one normal parameter, the result 0).
//
// Departures from Azahar: Azahar runs the DLC queries on a worker thread (ctx.RunAsync) and the
// client wakes when it finishes, in host time; here the reply is immediate (plus the standard IPC
// delay), which no game-visible state depends on. A reply word Azahar's RequestBuilder declares and
// does not write keeps the request's word of the same position in Azahar; the same is done here for
// positions inside the request's normal parameters, and 0 beyond them (Azahar leaves those
// uninitialised). TitleInfos is sized by the buffer the ids came in, not by the guest's count (a count
// that does not fit the buffer is InvalidTIDInList, as the ids Azahar would read past it are not ids).
// A content is found by the index its chunk record holds, and counts as downloaded only when the runtime
// can read it (../fs/dlc_titles.h); the infos written are cut to the buffer the guest gave.
#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

#include "../fs/fs_state.h"
#include "mh4n/ipc.h"
#include "mh4n/log.h"
#include "mh4n/registry.h"
#include "mh4n/service.h"

namespace mh4n {

namespace {

constexpr const char* kLog = "Service.AM";

constexpr u32 kTidHighUpdate = 0x0004000E;
constexpr u32 kTidHighDlc = 0x0004008C;
constexpr u32 kTidHighDemoData = 0x0004000D;  // ListDataTitleTicketInfos' other accepted id

constexpr u32 kErrInvalidTid = 31;
constexpr u32 kErrInvalidTidInList = 60;
constexpr u32 kErrNotFound = 1018;  // ErrorDescription::NotFound
constexpr u32 kModuleAm = 32;
constexpr u32 kSummaryInvalidState = 5;
constexpr u32 kSummaryInvalidArgument = 7;
constexpr u32 kLevelPermanent = 27;
constexpr u32 kLevelUsage = 28;

constexpr Result kResultInvalidTid =
    MakeResult(kErrInvalidTid, kModuleAm, kSummaryInvalidArgument, kLevelUsage);
constexpr Result kResultInvalidTidInList =
    MakeResult(kErrInvalidTidInList, kModuleAm, kSummaryInvalidArgument, kLevelUsage);
constexpr Result kResultTitleNotFound =
    MakeResult(kErrNotFound, kModuleAm, kSummaryInvalidState, kLevelPermanent);
// FindDLCContentInfos for an index the title metadata does not have ("TODO(Steveice10): Find the right
// error code" in Azahar)
constexpr Result kResultNoSuchContent = 0xFFFFFFFFu;

constexpr u8 kOwnershipDownloaded = 0x01;
constexpr u8 kOwnershipOwned = 0x02;

// am.cpp's ContentInfo (0x18 bytes) and TitleInfo (0x18 bytes), little-endian.
constexpr u32 kContentInfoSize = 0x18;
constexpr u32 kTitleInfoSize = 0x18;

void PutLe(std::vector<u8>& out, std::size_t at, u64 value, int bytes) {
    for (int i = 0; i < bytes; ++i) out[at + static_cast<std::size_t>(i)] = static_cast<u8>(value >> (8 * i));
}

std::vector<u8> ContentInfoBytes(const fs::DlcContent& content) {
    std::vector<u8> out(kContentInfoSize, 0);
    PutLe(out, 0, content.index, 2);
    PutLe(out, 2, content.type, 2);
    PutLe(out, 4, content.id, 4);
    PutLe(out, 8, content.size, 8);
    out[16] = static_cast<u8>(kOwnershipOwned |
                              (content.state == fs::DlcContentState::Readable ? kOwnershipDownloaded : 0));
    return out;
}

// The word at `index` of the request when it is one of the request's normal parameters (what an
// undeclared reply word holds in Azahar), else 0.
u32 LeftOver(const Request& request, std::size_t index) {
    const std::span<const u32> words = request.Words();
    return index <= request.NormalParamsSize() && index < words.size() ? words[index] : 0;
}

class AmApp final : public Service {
public:
    explicit AmApp(ServiceContext& context) : Service(context, "am:app") {
        Register(0x1001, "GetDLCContentInfoCount", &AmApp::GetDLCContentInfoCount);
        Register(0x1002, "FindDLCContentInfos", &AmApp::FindDLCContentInfos);
        Register(0x1003, "ListDLCContentInfos", &AmApp::ListDLCContentInfos);
        Register(0x1004, "DeleteContents", &AmApp::DeleteContents);
        Register(0x1005, "GetDLCTitleInfos", &AmApp::GetDLCTitleInfos);
        Register(0x1006, "GetNumDataTitleTickets");
        Register(0x1007, "ListDataTitleTicketInfos", &AmApp::ListDataTitleTicketInfos);
        Register(0x1008, "GetItemRights");
        Register(0x1009, "IsDataTitleInUse");
        Register(0x100A, "IsExternalTitleDatabaseInitialized");
        Register(0x100B, "GetNumExistingContentInfos");
        Register(0x100C, "ListExistingContentInfos");
        Register(0x100D, "GetPatchTitleInfos", &AmApp::GetPatchTitleInfos);
    }

private:
    // 0x100100C0: media type (u8), title id (u64). Reply: result, count.
    void GetDLCContentInfoCount(Request& request) {
        const u8 media_type = request.Pop<u8>();
        const u64 title_id = request.Pop<u64>();
        Reply& reply = request.MakeReply(2, 0);
        if (static_cast<u32>(title_id >> 32) != kTidHighDlc) {
            reply.Push(kResultInvalidTid);
            reply.Push<u32>(0);
            return;
        }
        if (const fs::DlcTitle* title = Dlc(media_type, title_id)) {
            reply.Push(kResultSuccess);
            reply.Push<u32>(static_cast<u32>(title->contents.size()));
            return;
        }
        // Azahar: no TMD on the media -> "number of content infos plus one", with a warning.
        MH4N_LOG(Warning, kLog, "missing TMD, title_id=0x{:016x}", title_id);
        reply.Push(kResultSuccess);
        reply.Push<u32>(1);
    }

    // 0x10020104: media type (u8), title id (u64), count (u32), the requested indices (mapped, in),
    // the content infos (mapped, out). Reply: result (the second declared word is not written).
    void FindDLCContentInfos(Request& request) {
        const u8 media_type = request.Pop<u8>();
        const u64 title_id = request.Pop<u64>();
        const u32 count = request.Pop<u32>();
        const MappedBuffer* indices = request.PopMappedBuffer();
        const MappedBuffer* infos = request.PopMappedBuffer();
        Result result = static_cast<u32>(title_id >> 32) != kTidHighDlc ? kResultInvalidTidInList : kResultSuccess;
        const fs::DlcTitle* title = result == kResultSuccess ? Dlc(media_type, title_id) : nullptr;
        if (title != nullptr) {
            // the indices the buffer holds (a count past its end reads no more)
            const u32 n = indices == nullptr ? 0 : std::min<u32>(count, indices->Size() / 2);
            std::vector<u16> wanted(n);
            if (n != 0) indices->Read(wanted.data(), 0, n * 2);
            std::vector<u8> out;
            for (const u16 index : wanted) {
                const fs::DlcContent* content = title->Content(index);
                if (content == nullptr) {
                    MH4N_LOG(Error, kLog, "Attempted to get info for non-existent content index {:04x}.", index);
                    result = kResultNoSuchContent;
                    out.clear();
                    break;
                }
                const std::vector<u8> info = ContentInfoBytes(*content);
                out.insert(out.end(), info.begin(), info.end());
            }
            WriteCut(infos, out);
            LogQuery(*title, n);
        }
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(result);
        reply.Push(LeftOver(request, 2));
    }

    // 0x10030140: count (u32), media type (u8), title id (u64), start index (u32), the content infos
    // (mapped, out). Reply: result, number written.
    void ListDLCContentInfos(Request& request) {
        const u32 count = request.Pop<u32>();
        const u8 media_type = request.Pop<u8>();
        const u64 title_id = request.Pop<u64>();
        const u32 start = request.Pop<u32>();
        const MappedBuffer* infos = request.PopMappedBuffer();
        const bool dlc_id = static_cast<u32>(title_id >> 32) == kTidHighDlc;
        u32 written = 0;
        if (const fs::DlcTitle* title = dlc_id ? Dlc(media_type, title_id) : nullptr) {
            const u64 total = title->contents.size();
            const u64 room = infos == nullptr ? 0 : infos->Size() / kContentInfoSize;
            const u64 end = std::min<u64>({static_cast<u64>(start) + count, total, static_cast<u64>(start) + room});
            std::vector<u8> out;
            for (u64 i = start; i < end; ++i) {
                const std::vector<u8> info = ContentInfoBytes(title->contents[static_cast<std::size_t>(i)]);
                out.insert(out.end(), info.begin(), info.end());
                ++written;
            }
            WriteCut(infos, out);
            LogQuery(*title, written);
        }
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(dlc_id ? kResultSuccess : kResultInvalidTidInList);
        reply.Push<u32>(written);
    }

    // 0x10040102: media type (u8), title id (u64), count (u32), the content ids (mapped, in).
    // Stubbed in Azahar: succeeds and gives the buffer back.
    void DeleteContents(Request& request) {
        const u8 media_type = request.Pop<u8>();
        const u64 title_id = request.Pop<u64>();
        const u32 content_count = request.Pop<u32>();
        const MappedBuffer* ids = request.PopMappedBuffer();
        MH4N_LOG(Warning, kLog, "(STUBBED) media_type={}, title_id=0x{:016x}, content_count={}",
                 media_type, title_id, content_count);
        Reply& reply = request.MakeReply(1, 2);
        reply.Push(kResultSuccess);
        if (ids != nullptr) {
            reply.PushMappedBuffer(*ids);
        } else {
            reply.Push<u32>(0);
            reply.Push<u32>(0);
        }
    }

    // Shared by GetDLCTitleInfos and GetPatchTitleInfos: media type (u8), count (u32), the title ids
    // (mapped, in), the title infos (mapped, out). Reply: result and both buffers back.
    void TitleInfos(Request& request, u32 required_tid_high) {
        const u8 media_type = request.Pop<u8>();
        const u32 count = request.Pop<u32>();
        const MappedBuffer* ids = request.PopMappedBuffer();
        const MappedBuffer* infos = request.PopMappedBuffer();

        Result result = kResultSuccess;
        // A list that does not fit the buffer reads as zero ids (which fail the TID check); the
        // vector is sized by the buffer, not by the guest's count.
        const bool fits = ids != nullptr && count <= ids->Size() / sizeof(u64);
        std::vector<u64> title_ids(fits ? count : std::min<u32>(count, 1), 0);
        if (fits && count != 0) {
            ids->Read(title_ids.data(), 0, count * static_cast<u32>(sizeof(u64)));
        }
        for (const u64 title_id : title_ids) {
            if (static_cast<u32>(title_id >> 32) != required_tid_high) {
                result = kResultInvalidTidInList;
                break;
            }
        }
        // GetTitleInfoFromList: a title the player brought has its title metadata; no other title has one
        // and there is no cartridge to match, so the first such title id of a list is "not found" (an empty
        // list succeeds). The infos are written only when every title is found.
        if (result == kResultSuccess) {
            std::vector<u8> out;
            for (const u64 title_id : title_ids) {
                const fs::DlcTitle* title =
                    required_tid_high == kTidHighDlc ? Dlc(media_type, title_id) : nullptr;
                if (title == nullptr) {
                    MH4N_LOG(Debug, kLog, "not found title_id={:016X}", title_id);
                    result = kResultTitleNotFound;
                    break;
                }
                std::vector<u8> info(kTitleInfoSize, 0);
                PutLe(info, 0, title->title_id, 8);
                // the size of content 0 ("This comes close but is off", TODO(shinyquagsire23) in Azahar)
                PutLe(info, 8, title->contents.empty() ? 0 : title->contents.front().size, 8);
                PutLe(info, 16, title->version, 2);
                PutLe(info, 20, title->title_type, 4);
                out.insert(out.end(), info.begin(), info.end());
                MH4N_LOG(Debug, kLog, "found title_id={:016X} version={:04X}", title_id, title->version);
            }
            if (result == kResultSuccess) WriteCut(infos, out);
        }
        Reply& reply = request.MakeReply(1, 4);
        reply.Push(result);
        PushBuffer(reply, ids);
        PushBuffer(reply, infos);
    }

    // The add-on content title of that id on that media type, if the player brought one. The file system's
    // state is fetched at the first query (made then if fs:USER has not made it yet), not when the service is
    // installed, so a run without DLC queries starts its file system when it did before.
    const fs::DlcTitle* Dlc(u8 media_type, u64 title_id) {
        if (fs_ == nullptr) fs_ = fs::GetFsState(Context());
        return fs_->dlc.Find(media_type, title_id);
    }

    // Writes `bytes`, cut to the buffer (a missing buffer gets nothing).
    static void WriteCut(const MappedBuffer* buffer, const std::vector<u8>& bytes) {
        if (buffer == nullptr || bytes.empty()) return;
        const u32 size = std::min<u32>(buffer->Size(), static_cast<u32>(bytes.size()));
        if (size != 0) buffer->Write(bytes.data(), 0, size);
    }

    // The first query of each title is logged at Info (what a player's log shows), the others at Debug.
    void LogQuery(const fs::DlcTitle& title, u32 contents) {
        if (logged_.insert(title.title_id).second) {
            MH4N_LOG(Info, kLog, "the game asks about DLC title {:016X}: {} contents asked, {} of its {} readable",
                     title.title_id, contents, title.Readable(), title.contents.size());
        } else {
            MH4N_LOG(Debug, kLog, "DLC title {:016X}: {} contents asked", title.title_id, contents);
        }
    }

    static void PushBuffer(Reply& reply, const MappedBuffer* buffer) {
        if (buffer != nullptr) {
            reply.PushMappedBuffer(*buffer);
        } else {
            reply.Push<u32>(0);
            reply.Push<u32>(0);
        }
    }

    void GetDLCTitleInfos(Request& request) { TitleInfos(request, kTidHighDlc); }
    void GetPatchTitleInfos(Request& request) { TitleInfos(request, kTidHighUpdate); }

    // 0x10070102: ticket count (u32), title id (u64), start index (u32), the
    // ticket infos (mapped, out). Azahar answers an id that is not a data title with an error and then
    // overwrites it with the success reply it builds next, so the result is always success. No
    // tickets are installed: nothing is written.
    void ListDataTitleTicketInfos(Request& request) {
        const u32 ticket_count = request.Pop<u32>();
        const u64 title_id = request.Pop<u64>();
        request.Pop<u32>();
        MH4N_LOG(Debug, kLog, "(STUBBED) called, ticket_count={}", ticket_count);
        const u32 tid_high = static_cast<u32>(title_id >> 32);
        if (tid_high != kTidHighDlc && tid_high != kTidHighDemoData) {
            MH4N_LOG(Error, kLog, "Tried to get infos for non-data title title_id={:016X}",
                     title_id);
        }
        request.PopMappedBuffer();
        Reply& reply = request.MakeReply(2, 0);
        reply.Push(kResultSuccess);
        reply.Push<u32>(0);
    }

    std::shared_ptr<fs::FsState> fs_;
    std::set<u64> logged_;
};

MH4N_REGISTER(ServiceFactory, "am:app",
              ServiceFactory([](ServiceContext& context) { return std::make_unique<AmApp>(context); },
                             5));

}  // namespace

}  // namespace mh4n

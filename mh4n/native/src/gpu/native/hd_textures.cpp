#include "gpu/native/hd_textures.h"

#include "gpu/native/process_memory.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <fmt/format.h>
#include <lodepng.h>

#include "../../common/utf8.h"
#include "gpu/texture/decode.h"
#include "gpu/texture/hash.h"
#include "hdpack/image.h"
#include "hdpack/pack.h"
#include "mh4n/host_thread_name.h"
#include "mh4n/log.h"

namespace mh4n::gpu::native {
namespace {

constexpr const char* kLog = "GPU.HdTextures";
namespace fs = std::filesystem;

// A PNG's size from its header (the signature, then IHDR: width and height big-endian at 16 and 20)
bool PngSize(const std::string& path, u32& width, u32& height) {
    std::ifstream in(utf8::PathFromUtf8(path), std::ios::binary);
    unsigned char h[24];
    if (!in.read(reinterpret_cast<char*>(h), sizeof h) || std::memcmp(h, "\x89PNG", 4) != 0 || std::memcmp(h + 12, "IHDR", 4) != 0) {
        return false;
    }
    width = (u32{h[16]} << 24) | (u32{h[17]} << 16) | (u32{h[18]} << 8) | h[19];
    height = (u32{h[20]} << 24) | (u32{h[21]} << 16) | (u32{h[22]} << 8) | h[23];
    return width > 0 && height > 0 && width <= hdpack::kMaxDimension && height <= hdpack::kMaxDimension;
}

u64 AnswerKey(u64 content, u32 width, u32 height, texture::Format format) {
    return content ^ (u64{width} << 48) ^ (u64{height} << 32) ^ (u64{static_cast<u32>(format)} << 24);
}

// Each 2x2 block of `rgba` (`width` x `height`, both even) as one pixel, the average of the four: the image at half its size,
// in place (MH4N_HD_SCALE=half).
void HalveRgba8(std::vector<u8>& rgba, u32 width, u32 height) {
    const u32 w = width / 2, h = height / 2;
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    for (u32 y = 0; y < h; ++y) {
        const u8* a = rgba.data() + (2 * y) * row;
        const u8* b = a + row;
        u8* out = rgba.data() + static_cast<std::size_t>(y) * w * 4;
        for (u32 x = 0; x < w; ++x, a += 8, b += 8, out += 4) {
            for (int c = 0; c < 4; ++c) out[c] = static_cast<u8>((a[c] + a[4 + c] + b[c] + b[4 + c] + 2) / 4);
        }
    }
    rgba.resize(static_cast<std::size_t>(w) * h * 4);
    rgba.shrink_to_fit();
}

// A pack file read and decoded: RGBA8, upright (turned over when `flip`), of the size its header gave (`width` x `height`),
// at half that when `half`. False when it can't be read or its size changed since (the reason in `error`).
bool ReadPng(const std::string& path, bool flip, u32 width, u32 height, std::vector<u8>& rgba, std::string& error,
             bool half = false) {
    std::ifstream in(utf8::PathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!in) {
        error = "cannot be opened";
        return false;
    }
    const std::streamsize n = in.tellg();
    std::vector<u8> bytes(static_cast<std::size_t>(std::max<std::streamsize>(n, 0)));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), n)) {
        error = "cannot be read";
        return false;
    }
    hdpack::Image image;
    if (!hdpack::DecodePng(bytes.data(), bytes.size(), flip, image, error) || image.width != width || image.height != height) {
        if (error.empty()) error = "its size changed";
        return false;
    }
    rgba = std::move(image.levels[0]);
    if (half) HalveRgba8(rgba, width, height);
    return true;
}

// A pack file read and decoded as `format` (a KTX2's, ktx2.h: RGBA8 or BC7 blocks; a PNG's: RGBA8), at half its size when
// `half` (RGBA8 only)
bool ReadFile(const std::string& path, bool ktx2, HdFormat format, bool flip, u32 width, u32 height, std::vector<u8>& out,
              std::string& error, bool half) {
    if (!ktx2) return ReadPng(path, flip, width, height, out, error, half);
    if (!ReadKtx2(path, format, width, height, out, error)) return false;
    if (half && format == HdFormat::Rgba8) HalveRgba8(out, width, height);
    return true;
}

// The decoded pixels the loader may keep waiting for the renderer: MH4N_HD_READY_MB, else by the device's memory (96 MB
// for every phone in 1.0.1's first build: a 3 GB phone kept as much of them as an 8 GB one)
u64 ReadyCapBytes() {
    static const u64 cap = [] {
        if (const char* v = std::getenv("MH4N_HD_READY_MB"); v != nullptr && std::atoi(v) > 0) {
            return static_cast<u64>(std::atoi(v)) << 20;
        }
        const u64 mb = DeviceMemoryMb();
        return static_cast<u64>(mb == 0 ? 96 : mb <= 3200 ? 32 : mb <= 4600 ? 48 : 96) << 20;
    }();
    return cap;
}

// MH4N_HD_SYNC=1: each file read and decoded on the renderer thread (1.0's way); =0: on the loader's threads even in a
// --deterministic run (a test aid); otherwise as Load is asked
bool SyncFor(bool asked) {
    const char* v = std::getenv("MH4N_HD_SYNC");
    return v != nullptr && (*v == '0' || *v == '1') ? *v == '1' : asked;
}

}  // namespace

// ---- the loader: the files read and decoded on threads of their own ----
// A request per file (TakePixels), first come first served, two at a time; the pixels wait in `ready` until the renderer
// takes them. The threads stop taking requests while the pixels waiting pass kReadyCap. A request no texture asked for
// again within kForgetAfter (an area left behind) is dropped before it is decoded, and pixels not taken within it are let
// go (Tick): each is asked for again if its texture is drawn again. The cap is by the device's memory (ReadyCapBytes).
struct HdTextures::Loader {
    const u64 kReadyCap = ReadyCapBytes();
    static constexpr std::chrono::seconds kForgetAfter{4};
    using Clock = std::chrono::steady_clock;

    struct Job {
        u32 file = 0;
        std::string path;
        bool flip = false;
        bool half = false;
        bool ktx2 = false;
        HdFormat format = HdFormat::Rgba8;
        u32 width = 0, height = 0;  // the file's own size
    };
    struct Done {
        bool ok = false;
        std::string error;
        std::vector<u8> rgba;
        Clock::time_point at;
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::unordered_map<u32, Clock::time_point> wanted;  // file -> the last time a texture asked for it (queued or decoding)
    std::unordered_map<u32, Done> ready;
    u64 ready_bytes = 0;
    bool stop = false;
    std::vector<std::thread> threads;
    u64 decoded = 0, dropped = 0, let_go = 0;
    u64 ready_peak = 0;  // the most pixels that waited at once
    double decode_ms = 0.0;

    Loader() {
        for (int i = 0; i < 2; ++i) {
            threads.emplace_back([this] {
                SetHostThreadName("mh4n-hdtex");
#if defined(_WIN32)
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__linux__)
                setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 5);  // (behind the game and the renderer)
#endif
                Run();
            });
        }
    }
    ~Loader() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        cv.notify_all();
        for (std::thread& t : threads) t.join();
    }

    void Run() {
        std::unique_lock lock(mutex);
        for (;;) {
            cv.wait(lock, [&] { return stop || (!queue.empty() && ready_bytes < kReadyCap); });
            if (stop) return;
            Job job = std::move(queue.front());
            queue.pop_front();
            const auto w = wanted.find(job.file);
            if (w == wanted.end()) continue;
            if (Clock::now() - w->second > kForgetAfter) {  // (no texture asked for it lately: not decoded)
                wanted.erase(w);
                ++dropped;
                continue;
            }
            lock.unlock();
            const auto t0 = Clock::now();
            Done done;
            done.ok = ReadFile(job.path, job.ktx2, job.format, job.flip, job.width, job.height, done.rgba, done.error, job.half);
            done.at = Clock::now();
            const double ms = std::chrono::duration<double, std::milli>(done.at - t0).count();
            lock.lock();
            wanted.erase(job.file);
            ++decoded;
            decode_ms += ms;
            ready_bytes += done.rgba.size();
            ready_peak = std::max(ready_peak, ready_bytes);
            ready[job.file] = std::move(done);
        }
    }
};

HdTextures::HdTextures() = default;
HdTextures::~HdTextures() {
    if (replaced_ > 0) {
        MH4N_LOG(Info, kLog, "HD textures: {} drawn from the packs in this run{}{}", replaced_, LoaderStats(),
                 ktx2_bc7_ + ktx2_rgba8_ ? fmt::format("; KTX2 files: {} as BC7, {} as RGBA8", ktx2_bc7_, ktx2_rgba8_)
                                         : std::string());
    }
}

bool HdTextures::Sized(File& f, u32 game_width, u32 game_height) {
    if (!f.sized) {
        f.sized = true;
        Ktx2Info info;
        bool read = false;
        if (f.ktx2) {  // (its header and key/values: the size, the kind, which way its rows run; ktx2.h)
            read = Ktx2Header(f.path, info) && info.readable;
            if (read) {
                f.file_width = info.width, f.file_height = info.height;
                f.format = Ktx2Target(info, bc7_);
                ++(f.format == HdFormat::Bc7 ? ktx2_bc7_ : ktx2_rgba8_);
            } else {
                MH4N_LOG(Warning, kLog, "{}: not a KTX2 file this port reads ({})", f.path,
                         info.kind.empty() ? "no KTX2 header" : info.kind);
            }
        } else {
            read = PngSize(f.path, f.file_width, f.file_height);
        }
        if (!read) {
            f.width = f.height = f.file_width = f.file_height = 0;
            Refuse(Replacement{static_cast<u32>(&f - files_.data()), 0, 0});
        } else {
            // (half: only a file at least four times the game's texture each way, so that the half is still twice it, and
            // of even sizes; a pack's 2x files stay whole; never a BC7 one)
            f.half = half_ && f.format == HdFormat::Rgba8 && f.file_width % 2 == 0 && f.file_height % 2 == 0 &&
                     f.file_width >= 4 * game_width && f.file_height >= 4 * game_height;
            f.width = f.half ? f.file_width / 2 : f.file_width;
            f.height = f.half ? f.file_height / 2 : f.file_height;
            if (f.half) ++halved_;
        }
    }
    return f.width > 0 && !f.refused;
}

HdTextures::Take HdTextures::TakePixels(const Replacement& r, std::vector<u8>& rgba, bool may_take) {
    if (sync_) return !may_take ? Take::Pending : Pixels(r, rgba) ? Take::Ready : Take::Failed;
    if (!loader_) loader_ = std::make_unique<Loader>();
    Loader& l = *loader_;
    std::lock_guard lock(l.mutex);
    if (const auto it = l.ready.find(r.file); it != l.ready.end()) {
        if (!may_take) {
            it->second.at = Loader::Clock::now();  // (still wanted: not let go by Tick)
            return Take::Pending;
        }
        Loader::Done done = std::move(it->second);
        l.ready_bytes -= done.rgba.size();
        l.ready.erase(it);
        l.cv.notify_all();  // (under the cap again, maybe)
        if (!done.ok) {
            MH4N_LOG(Warning, kLog, "{}: {}", files_[r.file].path, done.error);
            return Take::Failed;
        }
        rgba = std::move(done.rgba);
        return Take::Ready;
    }
    const auto now = Loader::Clock::now();
    const auto [w, asked] = l.wanted.try_emplace(r.file, now);
    w->second = now;
    if (asked) {
        const File& f = files_[r.file];
        l.queue.push_back(Loader::Job{r.file, f.path, f.flip, f.half, f.ktx2, f.format, f.file_width, f.file_height});
        l.cv.notify_one();
    }
    return Take::Pending;
}

std::string HdTextures::LoaderStats() const {
    std::string out;
    if (sync_reads_ > 0) {
        out = fmt::format("; {} files read on the renderer's thread ({:.0f} ms in all, the frames waiting)", sync_reads_, sync_ms_);
    }
    if (!loader_) return out;
    Loader& l = *loader_;
    std::lock_guard lock(l.mutex);
    return out + fmt::format("; the loader: {} files decoded ({:.0f} ms in all, off the renderer's thread), {} waiting, {} MB "
                             "ready (at most {} MB so far, cap {} MB), {} dropped unasked, {} let go{}",
                             l.decoded, l.decode_ms, l.queue.size(), l.ready_bytes >> 20, l.ready_peak >> 20, l.kReadyCap >> 20,
                             l.dropped, l.let_go, half_ ? fmt::format(", {} files at half size", halved_) : std::string());
}

void HdTextures::Tick() {
    if (!loader_) return;
    Loader& l = *loader_;
    std::lock_guard lock(l.mutex);
    const auto now = Loader::Clock::now();
    bool freed = false;
    for (auto it = l.ready.begin(); it != l.ready.end();) {
        if (now - it->second.at > Loader::kForgetAfter) {
            l.ready_bytes -= it->second.rgba.size();
            ++l.let_go;
            freed = true;
            it = l.ready.erase(it);
        } else {
            ++it;
        }
    }
    if (freed) l.cv.notify_all();
}

void HdTextures::Load(const std::string& root, bool sync, bool bc7) {
    sync_ = SyncFor(sync);
    bc7_ = bc7;
    if (const char* v = std::getenv("MH4N_HD_SCALE"); v != nullptr && std::string(v) == "half") {
        half_ = true;
        MH4N_LOG(Info, kLog, "MH4N_HD_SCALE=half: a pack's files four times the game's texture or more are drawn at half their "
                             "size (a quarter of the memory)");
    }
    if (const char* d = std::getenv("MH4N_HD_DUMP"); d != nullptr && *d != '\0') {
        dump_dir_ = d;
        std::error_code ec;
        fs::create_directories(utf8::PathFromUtf8(dump_dir_), ec);
        MH4N_LOG(Info, kLog, "MH4N_HD_DUMP: each texture drawn is written to {} once", dump_dir_);
    }
    if (const char* off = std::getenv("MH4N_HD_TEXTURES"); off != nullptr && *off == '0') {
        MH4N_LOG(Info, kLog, "MH4N_HD_TEXTURES=0: no texture pack is read");
        return;
    }
    std::error_code ec;
    if (root.empty() || !fs::is_directory(utf8::PathFromUtf8(root), ec)) {
        if (!root.empty()) MH4N_LOG(Info, kLog, "no texture packs ({} is not a folder)", root);
        return;
    }
    // each folder under the root is a pack (Azahar's load/textures/<title id>/, or the pack's own folder), and the root
    // itself when it holds texture files or a pack.json
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(utf8::PathFromUtf8(root), fs::directory_options::skip_permission_denied, ec)) {
        std::error_code de;
        if (e.is_directory(de)) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());
    bool root_pack = fs::exists(utf8::PathFromUtf8(root) / "pack.json", ec);
    for (const auto& e : fs::directory_iterator(utf8::PathFromUtf8(root), fs::directory_options::skip_permission_denied, ec)) {
        std::error_code de;
        root_pack |= e.is_regular_file(de) && utf8::PathToUtf8(e.path().filename()).rfind("tex1_", 0) == 0;
    }
    if (root_pack) dirs.assign(1, utf8::PathFromUtf8(root));
    u64 total_files = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const fs::path& dir : dirs) {
        std::string error;
        const std::string dir_text = utf8::PathToUtf8(dir);
        std::optional<hdpack::Pack> pack = hdpack::ScanPack(dir_text, &error);
        if (!pack) {
            MH4N_LOG(Warning, kLog, "texture pack {}: {}", dir_text, error);
            continue;
        }
        const int mode = pack->options.use_new_hash ? 1 : 0;
        u32 png = 0, ktx2 = 0, other = 0, normal = 0, taken = 0;
        for (const auto& [hash, material] : pack->materials) {
            if (material.file[static_cast<int>(hdpack::MapType::Normal)] >= 0) ++normal;
            const int index = material.file[static_cast<int>(hdpack::MapType::Color)];
            if (index < 0) continue;
            const hdpack::PackFile& pf = pack->files[static_cast<std::size_t>(index)];
            if (pf.format != hdpack::FileFormat::Png && pf.format != hdpack::FileFormat::Ktx2) {
                ++other;
                continue;
            }
            ++(pf.format == hdpack::FileFormat::Ktx2 ? ktx2 : png);
            if (by_hash_[mode].count(hash) != 0) continue;  // (an earlier pack has it: the first wins, as in a load order)
            File f;
            f.path = utf8::PathToUtf8(dir / utf8::PathFromUtf8(pf.path));
            f.flip = !pack->options.flip_png_files;
            f.ktx2 = pf.format == hdpack::FileFormat::Ktx2;
            // (its size from its header when its texture is first found, Sized: a whole pack's thousands of files are not
            // all opened at the start, which kept the screen black a while with a whole pack, 4u-native #56)
            by_hash_[mode][hash] = static_cast<u32>(files_.size());
            files_.push_back(std::move(f));
            ++taken;
        }
        total_files += taken;
        MH4N_LOG(Info, kLog,
                 "texture pack {}: {} textures drawn from it ({} PNG and {} KTX2 colour maps; left out: {} DDS or KTX, {} normal "
                 "maps; {} names ignored, {} files without a hash); {} hash, files {}",
                 dir_text, taken, png, ktx2, other, normal, pack->stats.ignored_names, pack->stats.files_without_hash,
                 mode ? "the new (use_new_hash)" : "the old", pack->options.flip_png_files ? "flipped as Azahar does" : "as they are");
        if (ktx2) {
            MH4N_LOG(Info, kLog, "texture pack {}: {} KTX2 files ({} of them over another file of the same texture), drawn as {}",
                     dir_text, ktx2, pack->stats.ktx2_preferred,
                     bc7 ? "BC7 where their rows run bottom-up (KTXorientation ru: a quarter of the memory), else RGBA8"
                         : "RGBA8 (this device has no BC7: the same memory as a PNG)");
        }
    }
    if (total_files) {
        MH4N_LOG(Info, kLog, "HD textures: {} in all, the packs read in {} ms; each file is read when the game first draws its "
                             "texture, {}, and takes GPU memory as it is (4 bytes a pixel: a 1024x1024 texture 4 MB)", total_files,
                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                 sync_ ? "the frame waiting for it (MH4N_HD_SYNC=1, or a --deterministic run)"
                       : "on threads of its own (the game's own texture meanwhile)");
    }
}

void HdTextures::CheckOrientation(const u8* data, std::size_t size, u32 width, u32 height, texture::Format format,
                                  const std::vector<u8>* rgba) {
    // (a decoded format: the hash's decode is RGBA8 too, its rows bottom-up)
    if (oriented_ || rgba == nullptr || texture::HashBytesPerTexel(format) != 4 || format == texture::Format::RGBA8) return;
    std::vector<u8> bottom_up;
    if (!texture::DecodeForHash(data, size, width, height, format, bottom_up) || bottom_up.size() != rgba->size()) return;
    oriented_ = true;
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    bool upright = true;
    for (u32 y = 0; y < height && upright; ++y) {
        upright = std::memcmp(rgba->data() + y * row, bottom_up.data() + (height - 1 - y) * row, row) == 0;
    }
    MH4N_LOG(Info, kLog, "orientation check ({}x{} format {}): the decoded pixels are {}", width, height, static_cast<u32>(format),
             upright ? "the hash's rows turned over (upright, as Azahar's dumps)" : "NOT the hash's rows turned over");
}

const HdTextures::Replacement* HdTextures::Find(u64 content, const u8* data, std::size_t size, u32 width, u32 height,
                                                texture::Format format) {
    if (by_hash_[0].empty() && by_hash_[1].empty()) return nullptr;
    const u64 key = AnswerKey(content, width, height, format);
    if (auto it = answers_.find(key); it != answers_.end()) {
        return it->second.found && !files_[it->second.r.file].refused ? &it->second.r : nullptr;
    }
    Answer& a = answers_[key];
    ++lookups_;
    const std::optional<texture::CustomHashes> h = texture::ComputeCustomHashes(data, size, width, height, format);
    if (!h) return nullptr;
    const u64 hashes[2] = {h->old_hash, h->new_hash};
    for (int mode = 0; mode < 2 && !a.found; ++mode) {
        if (auto it = by_hash_[mode].find(hashes[mode]); it != by_hash_[mode].end()) {
            File& f = files_[it->second];
            if (!Sized(f, width, height)) break;  // (its header unreadable: the game's own texture)
            a.found = true;
            a.r = Replacement{it->second, f.width, f.height, f.format};
            if (++replaced_ <= 30 || replaced_ % 100 == 0) {
                MH4N_LOG(Info, kLog, "{} {}x{} format {} drawn from {} ({}x{}{}, {:.1f} MB; {} textures from packs so far)",
                         texture::CustomTextureStem(width, height, hashes[mode], format), width, height,
                         static_cast<u32>(format), f.path, f.width, f.height, f.format == HdFormat::Bc7 ? " BC7" : "",
                         HdBytes(f.format, f.width, f.height) / 1048576.0, replaced_);
            }
        }
    }
    return a.found ? &a.r : nullptr;
}

bool HdTextures::Pixels(const Replacement& r, std::vector<u8>& rgba) {
    const File& f = files_[r.file];
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = ReadFile(f.path, f.ktx2, f.format, f.flip, f.file_width, f.file_height, rgba, error, f.half);
    ++sync_reads_;
    sync_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ok) {
        MH4N_LOG(Warning, kLog, "{}: {}", f.path, error);
        return false;
    }
    return true;
}

void HdTextures::Refuse(const Replacement& r) {
    File& f = files_[r.file];
    if (f.refused) return;
    f.refused = true;
    MH4N_LOG(Warning, kLog, "{} ({}x{}): could not be read or uploaded; its texture is the game's own from now on", f.path,
             f.width, f.height);
}

void HdTextures::Dump(u64 content, const u8* data, std::size_t size, u32 width, u32 height, texture::Format format,
                      const std::vector<u8>& rgba) {
    CheckOrientation(data, size, width, height, format, &rgba);
    if (dump_dir_.empty()) return;
    const u64 key = AnswerKey(content, width, height, format);
    if (dumped_.count(key) != 0) return;
    dumped_[key] = true;
    const std::optional<texture::CustomHashes> h = texture::ComputeCustomHashes(data, size, width, height, format);
    if (!h || rgba.size() != static_cast<std::size_t>(width) * height * 4) return;
    const std::string path =
        utf8::PathToUtf8(utf8::PathFromUtf8(dump_dir_) / (texture::CustomTextureStem(width, height, h->old_hash, format) + ".png"));
    if (lodepng::encode(path, rgba, width, height) != 0) {
        MH4N_LOG(Warning, kLog, "MH4N_HD_DUMP: {} not written", path);
    }
}

}  // namespace mh4n::gpu::native

// Written for this package from the design: nothing is copied from Azahar here.
#include "gpu/native/compose.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <unordered_map>

namespace mh4n::gpu::native {
namespace {

rhi::Error Err(std::string message) {
    return rhi::Error{std::move(message)};
}

struct Set0Sig {
    UniformRange vs_pica, vs, fragment;
    const rhi::Buffer* lf = nullptr;
    const rhi::Buffer* general = nullptr;
    const rhi::Buffer* pull = nullptr;
    bool operator==(const Set0Sig& o) const {
        // (the offsets are dynamic: given when the set is bound)
        auto same = [](const UniformRange& a, const UniformRange& b) { return a.buffer == b.buffer && a.size == b.size; };
        return same(vs_pica, o.vs_pica) && same(vs, o.vs) && same(fragment, o.fragment) && lf == o.lf && general == o.general &&
               pull == o.pull;
    }
};

struct Set1Sig {
    std::array<const rhi::Image*, 9> images{};
    std::array<u64, 9> ids{};  // the images' (rhi::Image::Id: never another image's, so a kept set outlives its images safely)
    std::array<rhi::ImageView, 9> views{};
    std::array<const rhi::Sampler*, 3> samplers{};
    bool operator==(const Set1Sig&) const = default;
};
struct Set1Hash {
    size_t operator()(const Set1Sig& s) const {
        u64 h = 0x9E3779B97F4A7C15ull;
        const auto mix = [&h](u64 v) { h = (h ^ v) * 0x100000001B3ull; h ^= h >> 29; };
        for (u64 id : s.ids) mix(id);
        for (rhi::ImageView v : s.views) mix(static_cast<u64>(v));
        for (const rhi::Sampler* p : s.samplers) mix(reinterpret_cast<uintptr_t>(p));
        return static_cast<size_t>(h);
    }
};

// MH4N_KEPT_SETS=0: the texture sets from the per-draw arena again, written binding by binding, kept for one recording
bool KeptSetsOn() {
    static const bool on = [] { const char* v = std::getenv("MH4N_KEPT_SETS"); return !(v != nullptr && *v == '0'); }();
    return on;
}

struct Set2Sig {
    const rhi::Image* image = nullptr;
    bool operator==(const Set2Sig&) const = default;
};

}  // namespace

FixupGeneratorRegistry& FixupGeneratorRegistry::Global() {
    static FixupGeneratorRegistry registry;
    return registry;
}

rhi::GraphicsPipelineDesc BaseDescOf(const PipelineKey& key, const rhi::Backend& backend) {
    rhi::GraphicsPipelineDesc desc;
    desc.vertex_layout = key.vertex.input.ToLayout();
    desc.topology = key.topology;
    if (key.pass == PassKind::Normal) {
        if (key.color_format != rhi::Format::Undefined) {
            desc.color_format = backend.ChooseFormat(key.color_format, rhi::FormatUse::ColorTarget).format;
        }
        if (key.depth_format != rhi::Format::Undefined) {
            desc.depth_format = backend.ChooseFormat(key.depth_format, rhi::FormatUse::DepthTarget).format;
        }
    }
    desc.state = key.state;
    desc.sets = PipelineLayoutSets();
    desc.name = key.pass == PassKind::ShadowWrite ? "pica shadow write" : "pica draw";
    return desc;
}

struct DrawComposer::Impl {
    std::shared_ptr<rhi::Backend> backend;
    rhi::BindingArena arena;
    std::shared_ptr<rhi::Image> dummy_2d, dummy_cube, dummy_shadow;
    // the dummies' clear: in this recording (not submitted yet), and done by a submitted one (Submitted)
    bool dummies_in_recording = false, dummies_cleared = false;
    std::shared_ptr<rhi::Sampler> sampler;

    enum class Open : u8 { None, Target, NoAttach } open = Open::None;
    rhi::Image* open_color = nullptr;
    rhi::Image* open_depth = nullptr;
    rhi::Rect open_area{};
    u32 open_w = 0, open_h = 0;

    std::optional<Set0Sig> sig0;
    std::optional<Set1Sig> sig1;
    std::optional<Set2Sig> sig2;
    rhi::BindingSet* set0 = nullptr;
    rhi::BindingSet* set1 = nullptr;
    rhi::BindingSet* set2 = nullptr;
    // The texture sets written in this recording (its arena sets live until its submission is done): a draw whose
    // textures and samplers are those of one of them binds it again instead of writing nine descriptors (the Mali
    // driver's descriptor writes were a tenth of a Moto g06's renderer thread). Cleared with the recording.
    struct WrittenSet1 {
        Set1Sig sig;
        rhi::BindingSet* set = nullptr;
    };
    std::vector<WrittenSet1> set1_written;
    // The texture sets kept from recording to recording (KeptSetsOn): written once, with one descriptor write, and bound
    // again in any later recording (each image prepared there first), so that after the first frames of a scene no set
    // is written at all. The least recently used go once there are more than kKeptSets; a set let go is written again
    // for another signature once the GPU has passed its last use.
    struct Kept1 {
        std::shared_ptr<rhi::BindingSet> set;
        u64 recording = 0;  // the last recording that bound it
        u64 stamp = 0;      // when it was last bound (the use counter)
    };
    static constexpr size_t kKeptSets = 1024, kKeptEvict = 128;
    std::unordered_map<Set1Sig, Kept1, Set1Hash> kept1;
    std::vector<std::pair<rhi::TimelineValue, std::shared_ptr<rhi::BindingSet>>> kept_free;  // (last use, set)
    u64 recording = 0, stamp = 0;
    PipelineKey last_key{};                     // the last draw's pipeline key and handle (Draw)
    std::optional<PipelineHandle> last_handle;
    size_t set1_next = 0;  // the entry a new set replaces once the list is full

    explicit Impl(std::shared_ptr<rhi::Backend> b) : backend(std::move(b)), arena(backend) {}

    // The kept set of `sig` bound as set 1 (set1, sig1): found and its images prepared for this recording, or written.
    rhi::Result<void> BindKept1(const Set1Sig& sig, ComposerCounters& frame) {
        Kept1* kept = nullptr;
        if (auto it = kept1.find(sig); it != kept1.end()) {
            kept = &it->second;
            // bound again: each image owes this recording what the write would have done for it (it may have been
            // drawn into, or cleared: the dummies at every recording's start)
            for (u32 i = 0; i < sig.images.size(); ++i) {
                const rhi::Image* image = sig.images[i];
                bool seen = image == nullptr;
                for (u32 j = 0; j < i && !seen; ++j) seen = sig.images[j] == image;
                if (!seen) {
                    if (rhi::Result<void> r = rhi::PrepareSampledImage(*image); !r.ok()) return r;
                }
            }
        } else {
            if (kept1.size() >= kKeptSets) EvictKept();
            std::shared_ptr<rhi::BindingSet> set;
            const rhi::TimelineValue completed = backend->CompletedValue();
            for (size_t i = 0; i < kept_free.size(); ++i) {
                if (kept_free[i].first <= completed) {
                    set = std::move(kept_free[i].second);
                    kept_free[i] = std::move(kept_free.back());
                    kept_free.pop_back();
                    break;
                }
            }
            if (set == nullptr) {
                rhi::Result<std::shared_ptr<rhi::BindingSet>> made =
                    rhi::MakeKeptBindingSet(*backend, PipelineLayoutSets()[set::kTextures]);
                if (!made.ok()) return rhi::Error{made.error()};
                set = *made;
            }
            std::array<rhi::BindingSet::ImageWrite, 6> images;
            u32 n = 0;
            for (u32 slot : {binding::kTex0, binding::kTex0Cube, binding::kTex0Shadow, binding::kTex1, binding::kTex2,
                             binding::kTexColor}) {
                images[n++] = rhi::BindingSet::ImageWrite{slot, sig.images[slot], sig.views[slot]};
            }
            const std::array<rhi::BindingSet::SamplerWrite, 3> samplers = {
                rhi::BindingSet::SamplerWrite{binding::kSampler0, sig.samplers[0]},
                rhi::BindingSet::SamplerWrite{binding::kSampler1, sig.samplers[1]},
                rhi::BindingSet::SamplerWrite{binding::kSampler2, sig.samplers[2]}};
            if (rhi::Result<void> r = set->SetImagesAndSamplers(images, samplers); !r.ok()) return r;
            ++frame.set1_writes;
            kept = &kept1.emplace(sig, Kept1{std::move(set), 0, 0}).first->second;
        }
        kept->recording = recording;
        kept->stamp = ++stamp;
        set1 = kept->set.get();
        sig1 = sig;
        return rhi::Result<void>{};
    }

    // The least recently bound kept sets let go (none this recording bound: its submission is still to come); each is
    // written again for another signature once the GPU has passed the last value submitted now (every submission that
    // used it has been made by then).
    void EvictKept() {
        std::vector<std::pair<u64, const Set1Sig*>> old;
        old.reserve(kept1.size());
        for (const auto& [sig, kept] : kept1) {
            if (kept.recording != recording) old.emplace_back(kept.stamp, &sig);
        }
        const size_t n = std::min(kKeptEvict, old.size());
        std::partial_sort(old.begin(), old.begin() + static_cast<std::ptrdiff_t>(n), old.end());
        const rhi::TimelineValue last = backend->LastSubmittedValue();
        for (size_t i = 0; i < n; ++i) {
            auto it = kept1.find(*old[i].second);
            kept_free.emplace_back(last, std::move(it->second.set));
            kept1.erase(it);
        }
    }
    void Forget() {
        open = Open::None;
        open_color = open_depth = nullptr;
        sig0.reset();
        sig1.reset();
        sig2.reset();
        set0 = set1 = set2 = nullptr;
        set1_written.clear();
        set1_next = 0;
        last_handle.reset();
    }
};

DrawComposer::DrawComposer(std::shared_ptr<rhi::Backend> backend, PipelineProvider& provider)
    : impl_(std::make_unique<Impl>(std::move(backend))), provider_(provider), fixup_(&FixupGeneratorRegistry::Global()) {}

DrawComposer::~DrawComposer() = default;

rhi::Result<std::unique_ptr<DrawComposer>> DrawComposer::Create(std::shared_ptr<rhi::Backend> backend,
                                                                PipelineProvider& provider) {
    std::unique_ptr<DrawComposer> composer(new DrawComposer(std::move(backend), provider));
    Impl& impl = *composer->impl_;
    rhi::ImageDesc desc;
    desc.format = rhi::Format::Rgba8Unorm;
    desc.width = desc.height = 1;
    desc.name = "fragment dummy 2d";
    {
        auto image = impl.backend->CreateImage(desc);
        if (!image.ok()) return Err(image.error());
        impl.dummy_2d = *image;
    }
    desc.name = "fragment dummy cube";
    desc.cube = true;
    desc.layers = 6;
    {
        auto image = impl.backend->CreateImage(desc);
        if (!image.ok()) return Err(image.error());
        impl.dummy_cube = *image;
    }
    desc.name = "fragment dummy shadow";
    desc.cube = false;
    desc.layers = 1;
    desc.shadow_source = true;
    {
        auto image = impl.backend->CreateImage(desc);
        if (!image.ok()) return Err(image.error());
        impl.dummy_shadow = *image;
    }
    rhi::SamplerDesc sampler;
    sampler.min_filter = sampler.mag_filter = sampler.mip_filter = rhi::Filter::Nearest;
    auto made = impl.backend->CreateSampler(sampler);
    if (!made.ok()) return Err(made.error());
    impl.sampler = *made;
    return composer;
}

void DrawComposer::BeginRecording(rhi::Recorder& rec) {
    Impl& im = *impl_;
    im.Forget();
    ++im.recording;
    // Nothing writes the dummies after their clear, and an image's layout is the backend's from recording to recording:
    // once a submitted recording has cleared them they stay white (8 clears and 16 barriers a recording, 4 to 6
    // recordings a frame: a few % of the renderer thread on a Mali, and small jobs on its GPU). MH4N_DUMMIES_ONCE=0:
    // every recording clears them, as before.
    static const bool once = [] {
        const char* v = std::getenv("MH4N_DUMMIES_ONCE");
        return v == nullptr || *v != '0';
    }();
    if (once && im.dummies_cleared) {
        return;
    }
    im.dummies_in_recording = true;
    rhi::ClearValue white;
    white.color = {1.0f, 1.0f, 1.0f, 1.0f};
    rec.ClearImage(*im.dummy_2d, white);
    for (u32 layer = 0; layer < 6; ++layer) {
        rec.ClearImage(*im.dummy_cube, white, 0, layer);
    }
    rec.ClearImage(*im.dummy_shadow, white);
}

void DrawComposer::Submitted(bool ok) {
    Impl& im = *impl_;
    if (im.dummies_in_recording && ok) {
        im.dummies_cleared = true;
    }
    im.dummies_in_recording = false;
}

void DrawComposer::BeginTargetPass(rhi::Recorder& rec, const rhi::TargetPassDesc& pass) {
    Impl& im = *impl_;
    if (im.open == Impl::Open::NoAttach) {
        rec.EndPass();
        im.open = Impl::Open::None;
    }
    const rhi::Rect area = pass.area;
    const bool continues = im.open == Impl::Open::Target && im.open_color == pass.color && im.open_depth == pass.depth &&
                           im.open_area.x == area.x && im.open_area.y == area.y && im.open_area.width == area.width &&
                           im.open_area.height == area.height;
    rec.BeginTargetPass(pass);
    if (!continues) {
        ++counters_.pass_begins;
        ++frame_.pass_begins;
    }
    im.open = Impl::Open::Target;
    im.open_color = pass.color;
    im.open_depth = pass.depth;
    im.open_area = area;
}

void DrawComposer::EndPass(rhi::Recorder& rec) {
    Impl& im = *impl_;
    if (im.open != Impl::Open::None) {
        rec.EndPass();
        im.open = Impl::Open::None;
    }
}

std::optional<rhi::Rect> DrawComposer::OpenArea(const rhi::Image* color, const rhi::Image* depth) const {
    const Impl& im = *impl_;
    if (im.open != Impl::Open::Target || im.open_color != color || im.open_depth != depth) {
        return std::nullopt;
    }
    return im.open_area;
}

bool DrawComposer::Continues(const DrawDesc& d) const {
    const Impl& im = *impl_;
    if (d.needs.shadow_write_pass) {
        return d.shadow_target != nullptr && im.open == Impl::Open::NoAttach && im.open_w == d.shadow_target->Width() &&
               im.open_h == d.shadow_target->Height();
    }
    const rhi::Rect& a = d.target.area;
    return im.open == Impl::Open::Target && im.open_color == d.target.color && im.open_depth == d.target.depth &&
           im.open_area.x == a.x && im.open_area.y == a.y && im.open_area.width == a.width && im.open_area.height == a.height;
}

static FixupDecision FixupOf(const DrawDesc& d, const rhi::Capabilities& caps, const FixupGeneratorRegistry* registry) {
    return DecideFixup(d.needs.fixup_gs, d.vertex.kind, caps.geometry_shader, registry != nullptr && registry->Has());
}

PipelineKey DrawComposer::KeyOf(const DrawDesc& d) const {
    PipelineKey key;
    key.pass = d.needs.shadow_write_pass ? PassKind::ShadowWrite : PassKind::Normal;
    key.vertex = d.vertex;
    key.fixup = FixupOf(d, impl_->backend->Caps(), fixup_) == FixupDecision::Applied ? FixupStage::FixedGeometry : FixupStage::None;
    key.fragment.kind = FragmentKind::Specialised;
    if (d.fragment != nullptr) {
        key.fragment.key = *d.fragment;
    }
    key.state = d.state;
    key.topology = d.topology;
    if (key.pass == PassKind::Normal) {
        key.color_format = d.color_format;
        key.depth_format = d.depth_format;
    }
    return d.fallback_only ? key.AsFallback() : key;
}

rhi::Result<DrawResult> DrawComposer::Draw(rhi::Recorder& rec, const DrawDesc& d) {
    Impl& im = *impl_;
    if (d.fragment == nullptr) {
        return Err("DrawComposer::Draw: no fragment key");
    }
    const bool shadow_pass = d.needs.shadow_write_pass;
    if (shadow_pass && d.shadow_target == nullptr) {
        return Err("DrawComposer::Draw: a shadow-write draw needs the shadow source (DrawDesc::shadow_target)");
    }
    if (!shadow_pass && d.target.color == nullptr && d.target.depth == nullptr) {
        return Err("DrawComposer::Draw: a normal draw needs a target");
    }
    DrawResult out;
    ++counters_.draws;
    ++frame_.draws;

    // The fix-up stage and the pipeline.
    const FixupDecision fixup = FixupOf(d, im.backend->Caps(), fixup_);
    out.fixup = fixup == FixupDecision::Applied;
    if (fixup == FixupDecision::Stubbed) {
        out.fixup_stubbed = true;
        ++counters_.stubbed_fixup;
        ++frame_.stubbed_fixup;
    }
    const PipelineKey key = KeyOf(d);
    // The draw before mostly had the same key: its handle again, without the provider's hash and table (not a fallback:
    // that one is asked for again, so the key's own pipeline is taken as soon as it is built).
    rhi::Result<PipelineHandle> handle = im.last_handle && !im.last_handle->fallback && im.last_key == key ? rhi::Result<PipelineHandle>(*im.last_handle)
                                                                              : provider_.Lookup(key);
    if (!handle.ok()) {
        return Err(handle.error());
    }
    if (handle->pending || handle->pipeline == nullptr) {
        // (neither its pipeline nor the fallback is built yet: left out this time, nothing recorded, the provider's
        // workers build them; not kept as the last handle, so the next draw with this key asks again)
        out.skipped = true;
        return out;
    }
    if (!im.last_handle || !(im.last_key == key)) {
        im.last_key = key;
        im.last_handle = *handle;
    }
    out.fallback = handle->fallback || d.fallback_only;
    if (out.fallback) {
        ++counters_.fallback_draws;
        ++frame_.fallback_draws;
    }

    // The pass, for the normal kind (the shadow kind begins after its set is written: a barrier must not land inside it).
    rhi::Image* twin = nullptr;
    if (!shadow_pass) {
        rhi::TargetPassDesc pass = d.target;
        // (the colour loaded, unless the renderer says the draw writes all of the render area; the depth's is the draw's: a
        // clear the renderer owes the target, or a load)
        pass.color_load = d.target.color_load == rhi::LoadOp::DontCare ? rhi::LoadOp::DontCare : rhi::LoadOp::Load;
        BeginTargetPass(rec, pass);
        // A draw with no colour target (depth only) reads and writes no colour: its destination copy is skipped and the
        // dummy image stands in (dropping the draw lost its depth, and its error line per draw, thousands in a hunt on
        // the Galaxy, was a stutter of its own).
        if (d.needs.dst_copy && d.target.color != nullptr) {
            twin = &rec.FeedbackCopy(*d.target.color);
            out.dst_copied = true;
            ++counters_.dst_copies;
            ++frame_.dst_copies;
        }
    } else if (im.open == Impl::Open::Target) {
        rec.EndPass();
        im.open = Impl::Open::None;
    }

    // Set 0: uniforms and tables.
    Set0Sig sig0;
    sig0.vs_pica = d.vs_pica_uniforms;
    sig0.vs = d.vs_uniforms;
    sig0.fragment = UniformRange{d.fragment_uniforms.buffer, d.fragment_uniforms.offset, d.fragment_uniforms.size};
    if (d.tables != nullptr) {
        sig0.lf = &d.tables->Buffer(fragment::TableRing::Lf);
        sig0.general = &d.tables->Buffer(fragment::TableRing::General);
    }
    sig0.pull = d.pull_words;
    if (im.sig0 && *im.sig0 == sig0 && im.set0 != nullptr) {
        out.set0_reused = true;
        ++counters_.sets_reused;
    } else {
        rhi::Result<rhi::BindingSet*> set = im.arena.Acquire(PipelineLayoutSets()[set::kUniforms]);
        if (!set.ok()) return Err(set.error());
        rhi::BindingSet& s = **set;
        auto put = [&](u32 binding, const UniformRange& range) -> rhi::Result<void> {
            if (range.buffer == nullptr) return rhi::Result<void>{};
            return s.SetUniformBuffer(binding, *range.buffer, 0, range.size);
        };
        for (const auto& step : {std::pair{binding::kVsPicaUniforms, sig0.vs_pica}, std::pair{binding::kVsUniforms, sig0.vs},
                                 std::pair{binding::kFragmentBlock, sig0.fragment}}) {
            rhi::Result<void> r = put(step.first, step.second);
            if (!r.ok()) return Err(r.error());
        }
        if (d.tables != nullptr) {
            // (storage buffers read as arrays of the formats' texels: kLutLfFormat, kLutRgFormat, kLutRgbaFormat)
            rhi::Result<void> r = s.SetStorageBuffer(binding::kLutLf, *sig0.lf, 0, sig0.lf->Size());
            if (r.ok()) r = s.SetStorageBuffer(binding::kLutRg, *sig0.general, 0, sig0.general->Size());
            if (r.ok()) r = s.SetStorageBuffer(binding::kLutRgba, *sig0.general, 0, sig0.general->Size());
            if (!r.ok()) return Err(r.error());
        }
        if (sig0.pull != nullptr) {
            rhi::Result<void> r = s.SetStorageBuffer(binding::kPullWords, *sig0.pull, 0, sig0.pull->Size());
            if (!r.ok()) return Err(r.error());
        }
        im.set0 = &s;
        im.sig0 = sig0;
        ++counters_.sets_acquired;
    }

    // Set 1: the textures. Unit 0 goes to the class its type names; the other classes hold the dummies.
    using fragment::regenum::TextureType;
    const TextureType type = d.fragment->texture.texture0_type();
    const bool cube = type == TextureType::TextureCube || type == TextureType::ShadowCube;
    const bool shadow = type == TextureType::Shadow2D;
    Set1Sig sig1;
    auto pick = [&](const TextureBinding& unit, const rhi::Image* dummy) { return unit.image != nullptr ? unit.image : dummy; };
    sig1.images[binding::kTex0] = pick(d.units[0], im.dummy_2d.get());
    sig1.images[binding::kTex0Cube] = im.dummy_cube.get();
    sig1.images[binding::kTex0Shadow] = im.dummy_shadow.get();
    sig1.views[binding::kTex0] = rhi::ImageView::Sampled;
    sig1.views[binding::kTex0Cube] = rhi::ImageView::Sampled;
    sig1.views[binding::kTex0Shadow] = rhi::ImageView::ShadowStorage;
    if (d.units[0].image != nullptr) {
        const u32 slot = cube ? binding::kTex0Cube : (shadow ? binding::kTex0Shadow : binding::kTex0);
        sig1.images[slot] = d.units[0].image;
        sig1.views[slot] = shadow ? rhi::ImageView::ShadowStorage : d.units[0].view;
        if (slot != binding::kTex0) sig1.images[binding::kTex0] = im.dummy_2d.get();
    }
    sig1.images[binding::kTex1] = pick(d.units[1], im.dummy_2d.get());
    sig1.images[binding::kTex2] = pick(d.units[2], im.dummy_2d.get());
    sig1.views[binding::kTex1] = d.units[1].image != nullptr ? d.units[1].view : rhi::ImageView::Sampled;
    sig1.views[binding::kTex2] = d.units[2].image != nullptr ? d.units[2].view : rhi::ImageView::Sampled;
    sig1.images[binding::kTexColor] = twin != nullptr ? twin : im.dummy_2d.get();
    sig1.views[binding::kTexColor] = rhi::ImageView::Sampled;
    for (u32 i = 0; i < 3; ++i) {
        sig1.samplers[i] = d.units[i].sampler != nullptr ? d.units[i].sampler : im.sampler.get();
    }
    for (u32 i = 0; i < sig1.images.size(); ++i) {
        sig1.ids[i] = sig1.images[i] != nullptr ? sig1.images[i]->Id() : 0;
    }
    rhi::BindingSet* written = nullptr;
    if (KeptSetsOn()) {
        if (im.sig1 && *im.sig1 == sig1 && im.set1 != nullptr) {
            out.set1_reused = true;
            ++counters_.sets_reused;
        } else if (rhi::Result<void> r = im.BindKept1(sig1, frame_); !r.ok()) {
            return Err(r.error());
        }
    } else if (!(im.sig1 && *im.sig1 == sig1 && im.set1 != nullptr)) {
        for (const Impl::WrittenSet1& w : im.set1_written) {
            if (w.sig == sig1) {
                written = w.set;
                break;
            }
        }
        if (written != nullptr) {
            // bound again: each image owes this recording what SetSampledImage would have done for it
            for (u32 i = 0; i < sig1.images.size(); ++i) {
                const rhi::Image* image = sig1.images[i];
                bool seen = image == nullptr;
                for (u32 j = 0; j < i && !seen; ++j) seen = sig1.images[j] == image;
                if (!seen && !rhi::PrepareSampledImage(*image).ok()) {
                    written = nullptr;  // (the write below says why)
                    break;
                }
            }
        }
    }
    if (KeptSetsOn()) {
        // (bound above)
    } else if (im.sig1 && *im.sig1 == sig1 && im.set1 != nullptr) {
        out.set1_reused = true;
        ++counters_.sets_reused;
    } else if (written != nullptr) {
        im.set1 = written;
        im.sig1 = sig1;
        out.set1_reused = true;
        ++counters_.sets_reused;
    } else {
        rhi::Result<rhi::BindingSet*> set = im.arena.Acquire(PipelineLayoutSets()[set::kTextures]);
        if (!set.ok()) return Err(set.error());
        rhi::BindingSet& s = **set;
        for (u32 slot : {binding::kTex0, binding::kTex0Cube, binding::kTex0Shadow, binding::kTex1, binding::kTex2,
                         binding::kTexColor}) {
            rhi::Result<void> r = s.SetSampledImage(slot, *sig1.images[slot], sig1.views[slot]);
            if (!r.ok()) return Err(r.error());
        }
        const u32 sampler_slot[3] = {binding::kSampler0, binding::kSampler1, binding::kSampler2};
        for (u32 i = 0; i < 3; ++i) {
            rhi::Result<void> r = s.SetSampler(sampler_slot[i], *sig1.samplers[i]);
            if (!r.ok()) return Err(r.error());
        }
        im.set1 = &s;
        im.sig1 = sig1;
        ++counters_.sets_acquired;
        ++frame_.set1_writes;
        constexpr size_t kWrittenSets = 48;
        if (im.set1_written.size() < kWrittenSets) {
            im.set1_written.push_back({sig1, &s});
        } else {
            im.set1_written[im.set1_next] = {sig1, &s};
            im.set1_next = (im.set1_next + 1) % kWrittenSets;
        }
    }

    // Set 2: the shadow write's storage image.
    Set2Sig sig2;
    sig2.image = shadow_pass ? d.shadow_target : im.dummy_shadow.get();
    if (im.sig2 && *im.sig2 == sig2 && im.set2 != nullptr) {
        out.set2_reused = true;
        ++counters_.sets_reused;
    } else {
        rhi::Result<rhi::BindingSet*> set = im.arena.Acquire(PipelineLayoutSets()[set::kShadow]);
        if (!set.ok()) return Err(set.error());
        rhi::Result<void> r = (*set)->SetStorageImage(binding::kShadowOut, *sig2.image, rhi::ImageView::ShadowStorage);
        if (!r.ok()) return Err(r.error());
        im.set2 = *set;
        im.sig2 = sig2;
        ++counters_.sets_acquired;
    }

    // The shadow-write pass: no attachments, the size of the shadow source.
    if (shadow_pass) {
        const u32 w = d.shadow_target->Width(), h = d.shadow_target->Height();
        if (im.open == Impl::Open::NoAttach && (im.open_w != w || im.open_h != h)) {
            rec.EndPass();
            im.open = Impl::Open::None;
        }
        if (im.open != Impl::Open::NoAttach) {
            rec.BeginPass(rhi::PassDesc{w, h});
            im.open = Impl::Open::NoAttach;
            im.open_w = w;
            im.open_h = h;
            im.open_color = im.open_depth = nullptr;
            ++counters_.pass_begins;
            ++frame_.pass_begins;
        }
    }

    // The draw.
    rec.SetPipeline(*handle->pipeline);
    // the dynamic offsets of set 0, in binding order (kVsPicaUniforms, kVsUniforms, kFragmentBlock)
    const std::array<u32, 3> offsets = {static_cast<u32>(sig0.vs_pica.offset), static_cast<u32>(sig0.vs.offset),
                                        static_cast<u32>(sig0.fragment.offset)};
    rec.Bind(*im.set0, set::kUniforms, offsets);
    rec.Bind(*im.set1, set::kTextures);
    rec.Bind(*im.set2, set::kShadow);
    if (d.viewport) rec.SetViewport(*d.viewport);
    if (d.scissor) rec.SetScissor(*d.scissor);
    if (!d.vertex_buffers.empty()) {
        rec.SetVertexBuffers(0, d.vertex_buffers);
    }
    if (d.index_buffer != nullptr) {
        rec.SetIndexBuffer(*d.index_buffer, d.index_offset, d.index_type);
        rec.DrawIndexed(d.count, 1, d.first, d.base_vertex, d.first_instance);
    } else {
        rec.Draw(d.count, 1, d.first, d.first_instance);
    }
    return out;
}

std::vector<FrameCounter> DrawComposer::TakeFrameCounters() {
    std::vector<FrameCounter> out;
    out.push_back({"stubbed:fixup", frame_.stubbed_fixup});
    out.push_back({"dst_copies", frame_.dst_copies});
    out.push_back({"fallback_draws", frame_.fallback_draws});
    out.push_back({"pass_begins", frame_.pass_begins});
    out.push_back({"set1_writes", frame_.set1_writes});
    frame_ = ComposerCounters{};
    return out;
}

} // namespace mh4n::gpu::native

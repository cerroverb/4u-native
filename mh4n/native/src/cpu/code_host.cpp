// The code host's threads, switching and callbacks (N1-04). The structure follows the Azahar
// backend of the port (one fiber per guest thread, the failure path that yields and never
// resumes); the rest is ours.
#include <atomic>
#include <mutex>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "callbacks.h"
#include "fiber.h"
#include "mh4n/assert.h"
#include "mh4n/cpu.h"
#include "mh4n/log.h"
#include "mh4n/memory.h"
#include "recomp_c.h"  // last: it defines macros

namespace mh4n {

namespace {

constexpr std::size_t kGuestStackSize = 8u << 20;  // as the Azahar backend's
constexpr u32 kUserModeCpsr = 0x10;
// FPSCR 0: round to nearest, no flush to zero: the default of a host thread.
constexpr u32 kHostFpscr = 0;

// Per host thread: the guest thread that runs on it, and the fiber of the host stack (made
// the first time the thread calls Enter).
struct HostThreadState {
    GuestThread* current = nullptr;
    std::unique_ptr<Port::Fiber> host_fiber;
    ~HostThreadState();
};
thread_local HostThreadState t_state;

// The first host thread that enters guest code (the scheduler's: in the app, the only one) reaches its state without
// the thread_local. Built for API 28, a thread_local is emulated TLS: __emutls_get_address and pthread_getspecific at
// every access, and the state is read several times in each system call and switch (2 % of a Moto g06's game
// thread). That thread is known by its thread pointer (TPIDR_EL0, which no other live thread has) until it ends;
// every other thread uses its thread_local as before.
#if defined(__aarch64__)
std::atomic<void*> g_main_tp{nullptr};
HostThreadState* g_main_state = nullptr;  // written by that thread before g_main_tp, read only by that thread
std::mutex g_main_mutex;

HostThreadState& State() {
    if (__builtin_thread_pointer() == g_main_tp.load(std::memory_order_relaxed)) {
        return *g_main_state;
    }
    return t_state;
}

void RegisterMainThread() {
    if (g_main_tp.load(std::memory_order_relaxed) != nullptr) {
        return;
    }
    std::lock_guard lock(g_main_mutex);
    if (g_main_tp.load(std::memory_order_relaxed) == nullptr) {
        g_main_state = &t_state;
        g_main_tp.store(__builtin_thread_pointer(), std::memory_order_release);
    }
}

HostThreadState::~HostThreadState() {
    // (the thread ends: its thread pointer may be another thread's next)
    if (g_main_tp.load(std::memory_order_relaxed) == __builtin_thread_pointer()) {
        std::lock_guard lock(g_main_mutex);
        g_main_tp.store(nullptr, std::memory_order_release);
    }
}
#else
HostThreadState& State() {
    return t_state;
}

void RegisterMainThread() {}

HostThreadState::~HostThreadState() = default;
#endif

// rc_apply_rounding(fpscr) (recomp.h), the host FPU following FPSCR: on arm64 its fesetround (FPCR.RMode, bits 23:22)
// and its FZ (bit 24) in one read of FPCR and a write only when they change. AArch64's FPCR keeps RMode where VFP's
// FPSCR does, in the same encoding (0 to 3: to nearest, up, down, towards zero, the order of rc_apply_fpscr's
// FE_TONEAREST..FE_TOWARDZERO), so the result is the same FPCR. It runs on every way in and out of the host (every
// system call twice), where a library call and two writes were.
void ApplyFpscr(u32 fpscr) {
#if defined(__aarch64__)
    constexpr unsigned long kMask = 0x1C00000ul;  // RMode and FZ
    unsigned long fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    const unsigned long want = (fpcr & ~kMask) | (fpscr & kMask);
    if (want != fpcr) {
        asm volatile("msr fpcr, %0" : : "r"(want));
    }
#else
    rc_apply_rounding(fpscr);
#endif
}

std::string DescribeRegisters(const RCtx& c) {
    RCtx& ctx = const_cast<RCtx&>(c);
    return fmt::format("r0 ={:08X} r1 ={:08X} r2 ={:08X} r3 ={:08X}\n"
                       "r4 ={:08X} r5 ={:08X} r6 ={:08X} r7 ={:08X}\n"
                       "r8 ={:08X} r9 ={:08X} r10={:08X} r11={:08X}\n"
                       "r12={:08X} sp ={:08X} lr ={:08X} pc ={:08X}\n"
                       "cpsr={:08X} fpscr={:08X}",
                       c.r[0], c.r[1], c.r[2], c.r[3], c.r[4], c.r[5], c.r[6], c.r[7], c.r[8],
                       c.r[9], c.r[10], c.r[11], c.r[12], c.r[13], c.r[14], c.r[15],
                       rc_get_cpsr(&ctx), c.fpscr);
}

GuestThread& ThreadOf(RCtx* ctx) {
    return *static_cast<GuestThread*>(ctx->user);
}

}  // namespace

// ---- GuestThread ------------------------------------------------------------------------------

GuestThread::GuestThread(CodeHost& host, u32 id, Entry entry)
    : host_(host), id_(id), entry_(std::move(entry)), ctx_(std::make_unique<RCtx>()) {
    ctx_->mem = host.Memory().Base();
    ctx_->cpsr_misc = kUserModeCpsr;
    ctx_->user = this;
    fiber_ = Port::Fiber::Create([this] { Main(); }, kGuestStackSize);
}

GuestThread::~GuestThread() {
    MH4N_ASSERT(State().current != this, "guest thread {} destroyed while it runs", id_);
}

void GuestThread::ApplyFpuState() {
    ApplyFpscr(in_host_ ? kHostFpscr : ctx_->fpscr);
}

void GuestThread::EnterHost() {
    in_host_ = true;
    ApplyFpscr(kHostFpscr);
}

void GuestThread::LeaveHost() {
    in_host_ = false;
    ApplyFpscr(ctx_->fpscr);
}

void GuestThread::Main() {
    ApplyFpuState();
    if (entry_) {
        entry_(*this);
        Fail("thread entry returned");
    }
    const u32 target = ctx_->r[15];
    host_.Call(*this, target);
    Fail(fmt::format("thread entry {:08X} returned to {:08X}", target, ctx_->r[15]));
}

void GuestThread::Yield() {
    MH4N_ASSERT(State().current == this, "Yield of guest thread {}, which is not running", id_);
    State().current = nullptr;
    ClearFaultContext();
    fiber_->SwitchTo(*State().host_fiber);
    ApplyFpuState();
}

void GuestThread::SwitchTo(GuestThread& next) {
    MH4N_ASSERT(State().current == this, "SwitchTo from guest thread {}, which is not running", id_);
    MH4N_ASSERT(&next != this && !next.failed_, "SwitchTo({}) from {}", next.id_, id_);
    State().current = &next;
    SetFaultContext(next.ctx_.get(), next.id_);
    fiber_->SwitchTo(*next.fiber_);
    ApplyFpuState();
}

void GuestThread::Fail(std::string message) {
    CodeFailure failure{std::move(message), DescribeRegisters(*ctx_), id_};
    MH4N_LOG(Critical, "Cpu", "guest thread {}: {}\n{}", id_, failure.message, failure.registers);
    if (!host_.failure_) {
        host_.failure_ = std::move(failure);
    }
    failed_ = true;
    Yield();
    MH4N_UNREACHABLE();  // a failed thread is never resumed
}

// ---- CodeHost ---------------------------------------------------------------------------------

CodeHost::CodeHost(GuestMemory& memory) : memory_(memory) {}

CodeHost::~CodeHost() = default;

std::unique_ptr<GuestThread> CodeHost::NewThread(u32 id, GuestThread::Entry entry) {
    return std::unique_ptr<GuestThread>(new GuestThread(*this, id, std::move(entry)));
}

void CodeHost::Enter(GuestThread& thread) {
    MH4N_ASSERT(&thread.host_ == this, "guest thread {} belongs to another code host", thread.id_);
    MH4N_ASSERT(State().current == nullptr, "Enter called from inside guest thread {}",
                State().current ? State().current->id_ : 0);
    MH4N_ASSERT(!thread.failed_, "Enter of guest thread {}, which failed", thread.id_);
    if (!State().host_fiber) {
        GuestMemory::PrepareThreadForFaults();
        State().host_fiber = Port::Fiber::FromCurrentThread();
        RegisterMainThread();
    }
    State().current = &thread;
    SetFaultContext(thread.ctx_.get(), thread.id_);
    State().host_fiber->SwitchTo(*thread.fiber_);
    // Back on the host stack: whoever yielded cleared the current thread and the fault context.
    ApplyFpscr(kHostFpscr);
}

void CodeHost::Call(GuestThread& thread, u32 target) {
    if (library_) {
        if (const GuestFunction function = library_->Find(target)) {
            function(thread.ctx_.get());
            return;
        }
    }
    if (missing_) {
        thread.EnterHost();
        missing_(*thread.ctx_, target);
        thread.LeaveHost();
        return;
    }
    thread.Fail(fmt::format("not recompiled: {:08X} (lr {:08X}); add it to "
                            "config/extra_entries.txt and rebuild",
                            target, thread.ctx_->r[14]));
}

RCtx* CodeHost::CurrentContext() {
    return State().current ? State().current->ctx_.get() : nullptr;
}

GuestThread* CodeHost::CurrentThread() {
    return State().current;
}

// ---- the callbacks of RHost -------------------------------------------------------------------

void CodeHostCallbacks::Call(RCtx* ctx, u32 target) {
    GuestThread& thread = ThreadOf(ctx);
    thread.host_.Call(thread, target);
}

void CodeHostCallbacks::Svc(RCtx* ctx, u32 imm, u32 next_pc) {
    GuestThread& thread = ThreadOf(ctx);
    if (!thread.host_.svc_) {
        thread.Fail(fmt::format("svc 0x{:02X} with no SVC handler installed", imm));
    }
    thread.EnterHost();
    thread.host_.svc_(*ctx, imm, next_pc);
    thread.LeaveHost();
    // The exclusive monitor is not held across a system call: the kernel may have switched threads,
    // and a switch clears it (a STREX after one must fail, so that its LDREX loop retries).
    ctx->excl_valid = 0;
}

void CodeHostCallbacks::Budget(RCtx* ctx) {
    GuestThread& thread = ThreadOf(ctx);
    if (!thread.host_.budget_) {
        thread.Fail("preemption requested with no budget handler installed");
    }
    thread.EnterHost();
    thread.host_.budget_(*ctx);
    thread.LeaveHost();
    // A preemption is a context switch, and a context switch clears the exclusive monitor (RC_TICK
    // sits at the start of every basic block, so a thread can be displaced between its LDREX and its
    // STREX, and the other thread may have written the word in between). Clearing it when no switch
    // happened is allowed too: a STREX may fail spuriously and the loops retry.
    ctx->excl_valid = 0;
}

void CodeHostCallbacks::Trap(RCtx* ctx, u32 pc, u32 kind) {
    static const char* const kinds[] = {"trap", "undefined instruction", "unsupported instruction",
                                        "breakpoint", "jump to an invalid address"};
    ThreadOf(ctx).Fail(fmt::format("{} at {:08X}", kinds[kind < 5 ? kind : 0], pc));
}

void CodeHostCallbacks::BadReturn(RCtx* ctx, u32 got, u32 expected) {
    ThreadOf(ctx).Fail(fmt::format("returned to {:08X}, expected {:08X}", got, expected));
}

u32 CodeHostCallbacks::SlowRead(RCtx* ctx, u32 address, u32 size) {
    ThreadOf(ctx).Fail(fmt::format("slow read of {} bytes at {:08X}: a flat-memory library was "
                                   "expected, rebuild it with --native",
                                   size, address));
}

void CodeHostCallbacks::SlowWrite(RCtx* ctx, u32 address, u32 value, u32 size) {
    ThreadOf(ctx).Fail(fmt::format("slow write of {} bytes ({:08X}) at {:08X}: a flat-memory "
                                   "library was expected, rebuild it with --native",
                                   size, value, address));
}

}  // namespace mh4n

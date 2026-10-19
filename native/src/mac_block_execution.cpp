#include "mac_block_execution.hpp"
#include <atomic>
#include <cfenv>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ma::experimental::interval_detail {
namespace {
thread_local bool executing_block = false;
struct Executing {
    bool previous = executing_block;
    Executing() { executing_block = true; }
    ~Executing() { executing_block = previous; }
};
unsigned sse_state() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    return _mm_getcsr();
#else
    return 0;
#endif
}
void set_sse_state(unsigned state) noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(state);
#else
    (void)state;
#endif
}
struct Environment {
    std::fenv_t floating{};
    unsigned sse = sse_state();
    Environment() {
        if (std::fegetenv(&floating) != 0) throw std::runtime_error("Cannot capture block arithmetic environment");
    }
    void apply() const {
        if (std::fesetenv(&floating) != 0) throw std::runtime_error("Cannot restore block arithmetic environment");
        set_sse_state(sse);
    }
    void restore() const noexcept { (void)std::fesetenv(&floating); set_sse_state(sse); }
};
class Executor {
    std::mutex dispatch_, gate_;
    std::condition_variable begin_, finished_;
    std::vector<std::thread> workers_;
    std::vector<std::exception_ptr> errors_;
    std::exception_ptr setup_error_;
    std::atomic<int> raised_{};
    std::atomic<unsigned> sse_raised_{};
    std::size_t count_{}, remaining_{}, epoch_{};
    bool stopping_{};
    BlockFunction function_{};
    const void* context_{};
    Environment environment_;
    void blocks(std::size_t worker) noexcept {
        const Executing executing;
        // Retain the same contiguous partition on every sweep. A shared
        // per-block claim counter moves cache lines between cores, and also
        // discards each worker's locality between pressure iterations.
        const auto participants = workers_.size() + 1, quotient = count_ / participants, remainder = count_ % participants;
        const auto begin = worker * quotient + std::min(worker, remainder);
        const auto end = begin + quotient + static_cast<std::size_t>(worker < remainder);
        for (auto block = begin; block < end; ++block) {
            try { function_(context_, block); }
            catch (...) { errors_[block] = std::current_exception(); }
        }
    }
    void worker(std::size_t index) noexcept {
        std::size_t seen = 0;
        for (;;) {
            {
                std::unique_lock lock(gate_);
                begin_.wait(lock, [&] { return stopping_ || epoch_ != seen; });
                if (stopping_) return;
                seen = epoch_;
            }
            try {
                const Environment previous;
                try {
                    environment_.apply(); blocks(index);
                    raised_.fetch_or(std::fetestexcept(FE_ALL_EXCEPT), std::memory_order_relaxed);
                    sse_raised_.fetch_or(sse_state() & 0x3fU, std::memory_order_relaxed);
                } catch (...) { previous.restore(); throw; }
                previous.restore();
            } catch (...) {
                std::lock_guard lock(gate_);
                if (!setup_error_) setup_error_ = std::current_exception();
            }
            std::lock_guard lock(gate_);
            if (--remaining_ == 0) finished_.notify_one();
        }
    }
    void stop() noexcept {
        { std::lock_guard lock(gate_); stopping_ = true; }
        begin_.notify_all();
        for (auto& thread : workers_) thread.join();
    }
public:
    explicit Executor(std::size_t count) {
        workers_.reserve(count - 1);
        try { for (std::size_t i = 1; i < count; ++i) workers_.emplace_back([this, i] { worker(i); }); }
        catch (...) { stop(); throw; }
    }
    ~Executor() { stop(); }
    void run(std::size_t count, BlockFunction function, const void* context) {
        // A second caller waits here; its own immutable operands remain local.
        // Nested calls run inline before acquiring this lock.
        std::unique_lock dispatch(dispatch_);
        const Environment environment;
        errors_.assign(count, {});
        {
            std::lock_guard lock(gate_);
            if (epoch_ == std::numeric_limits<std::size_t>::max()) throw std::overflow_error("Block execution epoch exhausted");
            environment_ = environment; setup_error_ = {}; function_ = function; context_ = context; count_ = count;
            raised_ = 0; sse_raised_ = 0; remaining_ = workers_.size(); ++epoch_;
        }
        begin_.notify_all(); blocks(0);
        {
            std::unique_lock lock(gate_);
            finished_.wait(lock, [&] { return remaining_ == 0; });
        }
        // Sticky exceptions are the union of the same independent operations,
        // including the main thread. Worker controls never leak into the caller.
        const auto flags = raised_.load(std::memory_order_relaxed);
        if (flags != 0) (void)std::feraiseexcept(flags);
        set_sse_state(sse_state() | sse_raised_.load(std::memory_order_relaxed));
        if (setup_error_) std::rethrow_exception(setup_error_);
        for (const auto& error : errors_) if (error) std::rethrow_exception(error);
    }
};
struct ExecutorOwner { std::mutex mutex; std::shared_ptr<Executor> executor; };
std::atomic<ExecutorOwner*> live_owner{};
ExecutorOwner& executor_owner() {
    // Native modules remain resident. Do not join live threads from Windows
    // DLL_PROCESS_DETACH under the loader lock. Normal native shutdown retires
    // the executor explicitly; an abrupt process exit lets the OS reclaim it.
    static auto* owner = [] {
        auto* created = new ExecutorOwner; live_owner.store(created, std::memory_order_release); return created;
    }();
    return *owner;
}
std::shared_ptr<Executor> acquire_executor() {
    auto& owner = executor_owner(); std::lock_guard lock(owner.mutex);
    if (!owner.executor) owner.executor = std::make_shared<Executor>(block_worker_count());
    return owner.executor;
}
}
std::size_t block_worker_count() {
    static const auto count = [] {
        const auto hardware = std::thread::hardware_concurrency();
        std::size_t result = hardware == 0 ? 1 : std::min(4U, hardware);
        if (const auto* setting = std::getenv("MA_MAC_WORKERS")) {
            const auto* end = setting + std::strlen(setting);
            const auto parsed = std::from_chars(setting, end, result);
            if (parsed.ec != std::errc{} || parsed.ptr != end || result < 1 || result > 64)
                throw std::invalid_argument("MA_MAC_WORKERS must be an integer from 1 through 64");
        }
        return result;
    }();
    return count;
}
void execute_blocks(std::size_t count, BlockFunction function, const void* context, std::size_t parallel_threshold) {
    if (count == 0) return;
    if (!function) throw std::invalid_argument("Missing independent block operation");
    if (executing_block || count < parallel_threshold || block_worker_count() == 1) {
        const Executing executing;
        for (std::size_t block = 0; block < count; ++block) function(context, block);
        return;
    }
    const auto executor = acquire_executor(); executor->run(count, function, context);
}
void retire_block_executor() noexcept {
    std::shared_ptr<Executor> retired;
    auto* owner = live_owner.load(std::memory_order_acquire);
    if (!owner) return;
    // The owner stays resident; only its executor is replaced.
    { std::lock_guard lock(owner->mutex); retired = std::move(owner->executor); }
    // Destroy outside the owner lock; other independent callers can start a
    // new executor while an earlier caller finishes on its retained one.
}
}

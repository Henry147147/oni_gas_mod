// Fixed row-block partitions and a persistent worker pool that claims blocks
// dynamically. Results never depend on the thread count: blocks are fixed by
// the grid shape, each block is processed by exactly one thread, and
// reductions combine per-block partials in block order (MAC_KERNEL.md, "Determinism").
#include "mac_kernel_detail.hpp"
#include <chrono>
#include <map>
#include <memory>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define MA_KERNEL_PAUSE() _mm_pause()
#else
#define MA_KERNEL_PAUSE() std::this_thread::yield()
#endif

namespace ma::experimental::mac_kernel {
Grid::Grid(const MacGrid& mg)
    : W(mg.width), H(mg.height), N(mg.width * mg.height), XW(mg.x_width()), YH(mg.y_height()),
      NX(mg.x_width() * mg.height), NY(mg.width * mg.y_height()), px(mg.periodic_x), py(mg.periodic_y),
      h(mg.spacing_m), depth(mg.depth_m), V(mg.volume_m3()), A(mg.spacing_m * mg.depth_m), wall(mg.walls) {
    require(W > 0 && H > 0 && N <= 1000000 && wall.size() == N && (!px || W > 1) && (!py || H > 1),
        "Invalid production kernel grid");
    require(std::isfinite(V) && V > 0 && std::isfinite(A) && A > 0, "Invalid production kernel cell geometry");
    const auto fluid = [&](std::size_t c) { return wall[c] ? none : static_cast<Index>(c); };
    xl.assign(NX, none); xr.assign(NX, none); yl.assign(NY, none); yr.assign(NY, none);
    for (std::size_t row = 0; row < H; ++row) for (std::size_t col = 0; col < XW; ++col) {
        const auto f = xface(col, row);
        if (col > 0 || px) xl[f] = fluid((col + W - 1) % W + row * W);
        if (col < W) xr[f] = fluid(col + row * W);
    }
    for (std::size_t row = 0; row < YH; ++row) for (std::size_t col = 0; col < W; ++col) {
        const auto f = yface(col, row);
        if (row > 0 || py) yl[f] = fluid(col + ((row + H - 1) % H) * W);
        if (row < H) yr[f] = fluid(col + row * W);
    }
    cell_faces.resize(N);
    for (std::size_t row = 0; row < H; ++row) for (std::size_t col = 0; col < W; ++col)
        cell_faces[col + row * W] = {xface(col, row), xface(col + 1 == XW ? 0 : col + 1, row), yface(col, row), yface(col, row + 1 == YH ? 0 : row + 1)};
    rows_per_block = std::max<std::size_t>(1, (1024 + W / 2) / W);
    blocks = (H + rows_per_block - 1) / rows_per_block;
}

namespace {
// Short pause spin, then yield, then sleep: an oversubscribed host must not
// burn the cores other threads (including our own workers) need. Workers keep
// yielding for up to 2 ms before sleeping: regions inside one interval follow
// each other closely, and a worker woken from sleep in a busy process (the
// game) arrives after the caller has claimed most blocks.
constexpr int pause_spins = 256;
constexpr auto awake_before_sleep = std::chrono::microseconds(2000);
// Block index of a closed ticket: never below any admissible block count.
constexpr std::uint64_t closed = 0xffffffffU;
}
Pool::Pool(unsigned threads) : threads_(std::max(1U, threads)) {
    for (unsigned id = 1; id < threads_; ++id) workers_.emplace_back([this] { worker(); });
}
Pool::~Pool() {
    { std::lock_guard lock(mutex_); stop_.store(true); ticket_.fetch_add(std::uint64_t{1} << 32); }
    start_.notify_all();
    for (auto& w : workers_) w.join();
}
void Pool::claim(std::uint64_t generation) {
    std::uint64_t t = ticket_.load(std::memory_order_acquire);
    for (;;) {
        if ((t >> 32) != generation) return;
        const auto block = static_cast<std::size_t>(t & 0xffffffffU);
        if (block >= blocks_.load(std::memory_order_acquire)) return;
        if (!ticket_.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel, std::memory_order_acquire)) continue;
        try { (*job_)(block); } catch (...) { errors_[block] = std::current_exception(); }
        done_.fetch_add(1, std::memory_order_acq_rel);
        t = ticket_.load(std::memory_order_acquire);
    }
}
void Pool::worker() {
    std::uint64_t seen = 0;
    for (;;) {
        std::uint64_t generation = ticket_.load(std::memory_order_acquire) >> 32;
        const auto idle_since = std::chrono::steady_clock::now();
        for (int spin = 0; generation == seen; ++spin) {
            if (spin < pause_spins) MA_KERNEL_PAUSE();
            else if (spin % 64 != 0 || std::chrono::steady_clock::now() - idle_since < awake_before_sleep) std::this_thread::yield();
            else {
                std::unique_lock lock(mutex_);
                sleeping_.fetch_add(1);
                start_.wait(lock, [&] { return (ticket_.load(std::memory_order_acquire) >> 32) != seen; });
                sleeping_.fetch_sub(1);
            }
            generation = ticket_.load(std::memory_order_acquire) >> 32;
        }
        seen = generation;
        if (stop_.load(std::memory_order_acquire)) return;
        bool environment = true;
        try { require_mac_energy_environment(); } catch (...) { environment = false; }
        if (environment) claim(generation);
        else {
            // This thread cannot execute: record the failure on the lowest
            // block it would claim and let the others finish.
            std::uint64_t t = ticket_.load(std::memory_order_acquire);
            while ((t >> 32) == generation && (t & 0xffffffffU) < blocks_.load(std::memory_order_acquire)) {
                if (ticket_.compare_exchange_weak(t, t + 1)) {
                    errors_[t & 0xffffffffU] = std::make_exception_ptr(FluidFatalError(FluidFatalCode::numerical_precision,
                        "MAC energy arithmetic requires binary64 x86-64 nearest rounding, FTZ/DAZ off and masked SSE exceptions"));
                    done_.fetch_add(1);
                    t = ticket_.load(std::memory_order_acquire);
                }
            }
        }
    }
}
void Pool::run(std::size_t blocks, const std::function<void(std::size_t)>& job) {
    std::lock_guard serial(run_mutex_);
    errors_.assign(blocks, nullptr);
    if (threads_ == 1 || blocks <= 1) {
        for (std::size_t b = 0; b < blocks; ++b) {
            try { job(b); } catch (...) { errors_[b] = std::current_exception(); break; }
        }
    } else {
        if (blocks >= closed) throw std::invalid_argument("Production kernel block count exceeds the pool ticket range");
        // Close the finished generation before blocks_ and job_ change: a
        // worker still in its claim loop that reads the new block count then
        // fails its compare-exchange instead of claiming a block twice.
        const std::uint64_t previous = ticket_.load(std::memory_order_relaxed) >> 32;
        ticket_.store(previous << 32 | closed, std::memory_order_relaxed);
        job_ = &job; done_.store(0, std::memory_order_relaxed);
        blocks_.store(blocks, std::memory_order_release);
        const std::uint64_t generation = previous + 1;
        {
            std::lock_guard lock(mutex_);
            ticket_.store(generation << 32, std::memory_order_release);
        }
        if (sleeping_.load(std::memory_order_acquire) != 0) start_.notify_all();
        claim(generation);
        for (int spin = 0; done_.load(std::memory_order_acquire) != blocks; ++spin) {
            if (spin < pause_spins) MA_KERNEL_PAUSE(); else std::this_thread::yield();
        }
        job_ = nullptr;
    }
    for (auto& error : errors_) if (error) std::rethrow_exception(error);
}
Pool& pool_for(unsigned threads) {
    static std::mutex mutex;
    static std::map<unsigned, std::unique_ptr<Pool>> pools;
    std::lock_guard lock(mutex);
    auto& entry = pools[std::max(1U, threads)];
    if (!entry) entry = std::make_unique<Pool>(std::max(1U, threads));
    return *entry;
}
}

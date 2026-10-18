// Static row-block partitions and a persistent worker pool. Results never
// depend on the thread count: blocks are fixed by the grid shape, each block
// is processed by exactly one thread, and reductions combine per-block
// partials in block order (MAC_KERNEL.md, "Determinism").
#include "mac_kernel_detail.hpp"
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
    rows_per_block = std::max<std::size_t>(1, (1024 + W / 2) / W);
    blocks = (H + rows_per_block - 1) / rows_per_block;
}

namespace { constexpr int spin_limit = 20000; }
Pool::Pool(unsigned threads) : threads_(std::max(1U, threads)) {
    for (unsigned id = 1; id < threads_; ++id) workers_.emplace_back([this, id] { worker(id); });
}
Pool::~Pool() {
    { std::lock_guard lock(mutex_); stop_.store(true); generation_.fetch_add(1); }
    start_.notify_all();
    for (auto& w : workers_) w.join();
}
void Pool::execute(unsigned id) {
    const auto begin = blocks_ * id / threads_, end = blocks_ * (id + 1) / threads_;
    try { require_mac_energy_environment(); }
    catch (...) { for (auto b = begin; b < end; ++b) errors_[b] = std::current_exception(); return; }
    for (auto b = begin; b < end; ++b) {
        try { (*job_)(b); } catch (...) { errors_[b] = std::current_exception(); }
    }
}
void Pool::worker(unsigned id) {
    std::uint64_t seen = 0;
    for (;;) {
        std::uint64_t now = generation_.load(std::memory_order_acquire);
        for (int spin = 0; now == seen; ++spin) {
            if (spin < spin_limit) MA_KERNEL_PAUSE();
            else {
                std::unique_lock lock(mutex_);
                start_.wait(lock, [&] { return generation_.load(std::memory_order_acquire) != seen; });
            }
            now = generation_.load(std::memory_order_acquire);
        }
        seen = now;
        if (stop_.load(std::memory_order_acquire)) return;
        execute(id);
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard lock(mutex_);
            done_.notify_one();
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
        job_ = &job; blocks_ = blocks;
        remaining_.store(threads_ - 1, std::memory_order_relaxed);
        { std::lock_guard lock(mutex_); generation_.fetch_add(1, std::memory_order_acq_rel); }
        start_.notify_all();
        execute(0);
        for (int spin = 0; remaining_.load(std::memory_order_acquire) != 0; ++spin) {
            if (spin < spin_limit) MA_KERNEL_PAUSE();
            else {
                std::unique_lock lock(mutex_);
                done_.wait(lock, [&] { return remaining_.load(std::memory_order_acquire) == 0; });
            }
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

#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ma::experimental {
// The cells of a MacTransportState: a vector-like sequence stored in fixed
// chunks of 1024 cells that copies of the state share until one of them
// writes. A copy shares every chunk; the first mutable access to a cell in a
// shared chunk gives the writer its own copy of that chunk. Values, order and
// every arithmetic use are those of a std::vector; only copying is cheaper.
//
// Concurrency: as for std::vector, distinct cells may be written from
// different threads at once, and readers must not race writers of the same
// cell. Copying, assigning or resizing the store itself is single-threaded.
// A chunk's first write is serialized by a store mutex; readers load each
// chunk pointer atomically, so a concurrent first write to another cell of
// the same chunk never exposes a half-replaced pointer.
template <class T> class MacChunkStore {
public:
    static constexpr std::size_t chunk_shift = 10, chunk_size = std::size_t{1} << chunk_shift, chunk_mask = chunk_size - 1;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;

    template <bool Const> class basic_iterator {
        using Store = std::conditional_t<Const, const MacChunkStore, MacChunkStore>;
        Store* store_ = nullptr; std::size_t index_ = 0;
        friend class MacChunkStore;
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const T&, T&>;
        using pointer = std::conditional_t<Const, const T*, T*>;
        basic_iterator() = default;
        basic_iterator(Store* store, std::size_t index) : store_(store), index_(index) {}
        operator basic_iterator<true>() const { return {store_, index_}; }
        reference operator*() const { return (*store_)[index_]; }
        pointer operator->() const { return &(*store_)[index_]; }
        reference operator[](difference_type n) const { return (*store_)[index_ + static_cast<std::size_t>(n)]; }
        basic_iterator& operator++() { ++index_; return *this; }
        basic_iterator operator++(int) { auto t = *this; ++index_; return t; }
        basic_iterator& operator--() { --index_; return *this; }
        basic_iterator operator--(int) { auto t = *this; --index_; return t; }
        basic_iterator& operator+=(difference_type n) { index_ = static_cast<std::size_t>(static_cast<difference_type>(index_) + n); return *this; }
        basic_iterator& operator-=(difference_type n) { return *this += -n; }
        friend basic_iterator operator+(basic_iterator a, difference_type n) { return a += n; }
        friend basic_iterator operator+(difference_type n, basic_iterator a) { return a += n; }
        friend basic_iterator operator-(basic_iterator a, difference_type n) { return a -= n; }
        friend difference_type operator-(const basic_iterator& a, const basic_iterator& b) {
            return static_cast<difference_type>(a.index_) - static_cast<difference_type>(b.index_);
        }
        friend bool operator==(const basic_iterator& a, const basic_iterator& b) { return a.index_ == b.index_; }
        friend auto operator<=>(const basic_iterator& a, const basic_iterator& b) { return a.index_ <=> b.index_; }
    };
    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    MacChunkStore() = default;
    explicit MacChunkStore(size_type count) { resize(count); }
    MacChunkStore(size_type count, const T& value) { resize(count, value); }
    MacChunkStore(std::initializer_list<T> values) { for (const auto& v : values) push_back(v); }
    template <class It, class = typename std::iterator_traits<It>::iterator_category>
    MacChunkStore(It first, It last) { for (; first != last; ++first) push_back(*first); }
    MacChunkStore(const MacChunkStore& other) : owners_(other.owners_), size_(other.size_) {
        if (!sharing().load(std::memory_order_relaxed)) {
            // Control: every copy owns its own chunks, as a vector would.
            for (auto& chunk : owners_) chunk = copy_of(*chunk);
            rebuild_views();
            for (std::size_t k = 0; k < views_; ++k) mine_[k].store(true, std::memory_order_relaxed);
            return;
        }
        other.share_all(); rebuild_views();
    }
    // Control switch: false makes copies deep (no sharing). Results must not
    // depend on it.
    static std::atomic<bool>& sharing() { static std::atomic<bool> on{true}; return on; }
    MacChunkStore(MacChunkStore&& other) noexcept { swap(other); }
    MacChunkStore& operator=(const MacChunkStore& other) {
        if (this != &other) { MacChunkStore copy(other); swap(copy); }
        return *this;
    }
    MacChunkStore& operator=(MacChunkStore&& other) noexcept {
        if (this != &other) { MacChunkStore empty; swap(empty); swap(other); }
        return *this;
    }
    MacChunkStore& operator=(std::initializer_list<T> values) { MacChunkStore copy(values); swap(copy); return *this; }
    void swap(MacChunkStore& other) noexcept {
        owners_.swap(other.owners_); std::swap(size_, other.size_);
        pointers_.swap(other.pointers_); mine_.swap(other.mine_); std::swap(views_, other.views_);
    }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type capacity() const noexcept { return owners_.size() * chunk_size; }
    void reserve(size_type) {}

    const T& operator[](size_type i) const noexcept {
        return pointers_[i >> chunk_shift].load(std::memory_order_acquire)->values.data()[i & chunk_mask];
    }
    T& operator[](size_type i) {
        const auto k = i >> chunk_shift;
        if (!mine_[k].load(std::memory_order_acquire)) detach(k);
        auto* chunk = pointers_[k].load(std::memory_order_relaxed);
        chunk->stamps[i & chunk_mask] = chunk->serial;
        return chunk->values.data()[i & chunk_mask];
    }
    const T& at(size_type i) const { if (i >= size_) throw std::out_of_range("MacChunkStore::at"); return (*this)[i]; }
    T& at(size_type i) { if (i >= size_) throw std::out_of_range("MacChunkStore::at"); return (*this)[i]; }
    const T& front() const { return (*this)[0]; }
    T& front() { return (*this)[0]; }
    const T& back() const { return (*this)[size_ - 1]; }
    T& back() { return (*this)[size_ - 1]; }

    iterator begin() { return {this, 0}; }
    iterator end() { return {this, size_}; }
    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, size_}; }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    void clear() { MacChunkStore empty; swap(empty); }
    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }
    template <class... Args> T& emplace_back(Args&&... args) {
        if ((size_ & chunk_mask) == 0) append_chunk();
        const auto k = owners_.size() - 1;
        if (!mine_[k].load(std::memory_order_relaxed)) detach(k);
        auto& chunk = owners_[k]->values;
        chunk.emplace_back(std::forward<Args>(args)...);
        owners_[k]->stamps.push_back(owners_[k]->serial);
        ++size_;
        return chunk.back();
    }
    void pop_back() { resize(size_ - 1); }
    void resize(size_type count) { resize_impl(count, nullptr); }
    void resize(size_type count, const T& value) { resize_impl(count, &value); }
    void assign(size_type count, const T& value) { MacChunkStore filled(count, value); swap(filled); }
    template <class It, class = typename std::iterator_traits<It>::iterator_category>
    void assign(It first, It last) { MacChunkStore copy(first, last); swap(copy); }
    template <class It> iterator insert(const_iterator position, It first, It last) {
        std::vector<T> tail(begin() + static_cast<difference_type>(position.index_), end());
        const auto at = position.index_;
        resize(at);
        for (; first != last; ++first) push_back(*first);
        for (auto& v : tail) push_back(std::move(v));
        return {this, at};
    }

    friend bool operator==(const MacChunkStore& a, const MacChunkStore& b) {
        if (a.size_ != b.size_) return false;
        for (std::size_t i = 0; i < a.size_; ++i) if (!(a[i] == b[i])) return false;
        return true;
    }

    // Chunk identity: two stores hold the same values in chunk k when they
    // share it. Nothing else may be inferred from these pointers.
    [[nodiscard]] std::size_t chunk_count() const noexcept { return owners_.size(); }
    [[nodiscard]] const void* chunk_identity(std::size_t k) const noexcept {
        return pointers_[k].load(std::memory_order_acquire);
    }
    // Chunk serial: unique in the process to each chunk's contents since it
    // was sealed. A chunk gets a new serial whenever it is created, copied or
    // claimed for writing, and seal() makes the next write to any chunk claim
    // it again; so after seal(), chunk k holds the values it held then for as
    // long as its serial is unchanged, in this store or any other.
    [[nodiscard]] std::uint64_t chunk_serial(std::size_t k) const noexcept {
        return pointers_[k].load(std::memory_order_acquire)->serial;
    }
    void seal() const noexcept { share_all(); }
    // Cell stamp: the serial of the chunk that last gave cell i out for
    // writing (mutable access). Copies keep stamps, and a chunk gives cells
    // out under one serial only until it is copied or sealed; so two stores
    // whose cell i has the same stamp hold the same value there.
    [[nodiscard]] std::uint64_t cell_stamp(size_type i) const noexcept {
        return pointers_[i >> chunk_shift].load(std::memory_order_acquire)->stamps[i & chunk_mask];
    }

private:
    struct Chunk { std::uint64_t serial = 0; std::vector<T> values; std::vector<std::uint64_t> stamps; };
    static std::uint64_t next_serial() noexcept { static std::atomic<std::uint64_t> n{0}; return n.fetch_add(1, std::memory_order_relaxed) + 1; }
    static std::shared_ptr<Chunk> copy_of(const Chunk& chunk) {
        auto copy = std::make_shared<Chunk>();
        copy->values.reserve(chunk_size);
        copy->values = chunk.values;
        copy->stamps.reserve(chunk_size);
        copy->stamps = chunk.stamps;
        copy->serial = next_serial();
        return copy;
    }
    std::vector<std::shared_ptr<Chunk>> owners_;
    size_type size_ = 0;
    std::unique_ptr<std::atomic<Chunk*>[]> pointers_;
    std::unique_ptr<std::atomic<bool>[]> mine_;
    std::size_t views_ = 0;
    // Serializes first writes (detach) across threads; one per process keeps
    // stores movable. First writes per chunk are rare after the copy.
    static std::mutex& detach_mutex() { static std::mutex m; return m; }
public:
    // Diagnostics: chunks copied by first writes in this process.
    static std::atomic<std::uint64_t>& copied_chunks() { static std::atomic<std::uint64_t> n{0}; return n; }
private:

    void rebuild_views() {
        views_ = owners_.size();
        pointers_ = std::make_unique<std::atomic<Chunk*>[]>(views_);
        mine_ = std::make_unique<std::atomic<bool>[]>(views_);
        for (std::size_t k = 0; k < views_; ++k) {
            pointers_[k].store(owners_[k].get(), std::memory_order_relaxed);
            mine_[k].store(false, std::memory_order_relaxed);
        }
    }
    void share_all() const noexcept {
        for (std::size_t k = 0; k < views_; ++k) mine_[k].store(false, std::memory_order_relaxed);
    }
    void detach(std::size_t k) {
        std::lock_guard lock(detach_mutex());
        if (mine_[k].load(std::memory_order_relaxed)) return;
        if (owners_[k].use_count() != 1) {
            copied_chunks().fetch_add(1, std::memory_order_relaxed);
            owners_[k] = copy_of(*owners_[k]);
            pointers_[k].store(owners_[k].get(), std::memory_order_release);
        } else owners_[k]->serial = next_serial(); // claimed: its values may change
        mine_[k].store(true, std::memory_order_release);
    }
    void append_chunk() {
        auto chunk = std::make_shared<Chunk>();
        chunk->values.reserve(chunk_size);
        chunk->stamps.reserve(chunk_size);
        chunk->serial = next_serial();
        owners_.push_back(std::move(chunk));
        const auto old_pointers = std::move(pointers_);
        const auto old_mine = std::move(mine_);
        const auto old_views = views_;
        views_ = owners_.size();
        pointers_ = std::make_unique<std::atomic<Chunk*>[]>(views_);
        mine_ = std::make_unique<std::atomic<bool>[]>(views_);
        for (std::size_t k = 0; k < views_; ++k) {
            pointers_[k].store(owners_[k].get(), std::memory_order_relaxed);
            mine_[k].store(k < old_views ? old_mine[k].load(std::memory_order_relaxed) : true, std::memory_order_relaxed);
        }
    }
    void resize_impl(size_type count, const T* value) {
        if (count < size_) {
            const auto chunks = (count + chunk_mask) >> chunk_shift;
            owners_.resize(chunks);
            if (chunks && (count & chunk_mask)) {
                const auto k = chunks - 1;
                if (!mine_[k].load(std::memory_order_relaxed)) detach(k);
                owners_[k]->values.resize(count & chunk_mask);
                owners_[k]->stamps.resize(count & chunk_mask);
            }
            size_ = count;
            // Keep the surviving chunks' sharing state.
            std::vector<bool> mine(chunks);
            for (std::size_t k = 0; k < chunks; ++k) mine[k] = mine_[k].load(std::memory_order_relaxed);
            rebuild_views();
            for (std::size_t k = 0; k < chunks; ++k) mine_[k].store(mine[k], std::memory_order_relaxed);
            return;
        }
        while (size_ < count) {
            if (value) emplace_back(*value); else emplace_back();
        }
    }
};
} // namespace ma::experimental

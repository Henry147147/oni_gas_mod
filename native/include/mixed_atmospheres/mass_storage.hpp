#pragma once
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace ma {
// Untouched, positive-zero arrays share immutable
// storage. Mutable access materializes an independent vector. Owned vectors
// always copy deeply, even when they happen to contain only zeros, so mutable
// references retained before a copy keep ordinary value semantics.
class MassStorage {
    using Vector = std::vector<double>;
    Vector owned_;
    std::shared_ptr<const Vector> zero_;
    static bool positive_zero(double value) noexcept { return std::bit_cast<std::uint64_t>(value) == 0; }
    static std::shared_ptr<const Vector> zeros(std::size_t count) {
        if (count == 0) return {};
        thread_local std::shared_ptr<const Vector> recent;
        if (recent && recent->size() == count) return recent;
        static std::mutex mutex;
        static std::map<std::size_t, std::shared_ptr<const Vector>> cache;
        std::lock_guard lock(mutex);
        auto& entry = cache[count];
        if (!entry) entry = std::make_shared<const Vector>(count, 0.0);
        recent = entry; return entry;
    }
    [[nodiscard]] const Vector& read() const noexcept { return zero_ ? *zero_ : owned_; }
    Vector& write() {
        if (zero_) { owned_ = *zero_; zero_.reset(); }
        return owned_;
    }
public:
    using value_type = double;
    using size_type = Vector::size_type;
    using difference_type = Vector::difference_type;
    using reference = double&;
    using const_reference = const double&;
    using pointer = double*;
    using const_pointer = const double*;
    using iterator = Vector::iterator;
    using const_iterator = Vector::const_iterator;
    using reverse_iterator = Vector::reverse_iterator;
    using const_reverse_iterator = Vector::const_reverse_iterator;
    MassStorage() = default;
    explicit MassStorage(size_type count, double value = 0) { assign(count, value); }
    MassStorage(std::initializer_list<double> values) : owned_(values) {}
    MassStorage(const Vector& values) : owned_(values) {}
    MassStorage(Vector&& values) noexcept : owned_(std::move(values)) {}
    MassStorage(const MassStorage&) = default;
    MassStorage(MassStorage&&) noexcept = default;
    MassStorage& operator=(const MassStorage&) = default;
    MassStorage& operator=(MassStorage&&) noexcept = default;
    MassStorage& operator=(const Vector& values) { owned_ = values; zero_.reset(); return *this; }
    MassStorage& operator=(Vector&& values) noexcept { owned_ = std::move(values); zero_.reset(); return *this; }
    MassStorage& operator=(std::initializer_list<double> values) { owned_ = values; zero_.reset(); return *this; }
    operator const Vector&() const noexcept { return read(); }
    [[nodiscard]] bool implicit_zero() const noexcept { return static_cast<bool>(zero_); }
    [[nodiscard]] size_type size() const noexcept { return read().size(); }
    [[nodiscard]] bool empty() const noexcept { return read().empty(); }
    [[nodiscard]] size_type capacity() const noexcept { return read().capacity(); }
    [[nodiscard]] size_type max_size() const noexcept { return read().max_size(); }
    reference operator[](size_type index) { return write()[index]; }
    const_reference operator[](size_type index) const noexcept { return read()[index]; }
    reference at(size_type index) { return write().at(index); }
    const_reference at(size_type index) const { return read().at(index); }
    reference front() { return write().front(); }
    const_reference front() const { return read().front(); }
    reference back() { return write().back(); }
    const_reference back() const { return read().back(); }
    pointer data() { return write().data(); }
    const_pointer data() const noexcept { return read().data(); }
    iterator begin() { return write().begin(); }
    iterator end() { return write().end(); }
    const_iterator begin() const noexcept { return read().begin(); }
    const_iterator end() const noexcept { return read().end(); }
    const_iterator cbegin() const noexcept { return read().cbegin(); }
    const_iterator cend() const noexcept { return read().cend(); }
    reverse_iterator rbegin() { return write().rbegin(); }
    reverse_iterator rend() { return write().rend(); }
    const_reverse_iterator rbegin() const noexcept { return read().rbegin(); }
    const_reverse_iterator rend() const noexcept { return read().rend(); }
    void clear() noexcept { owned_.clear(); zero_.reset(); }
    void reserve(size_type count) { write().reserve(count); }
    void shrink_to_fit() { write().shrink_to_fit(); }
    void resize(size_type count, double value = 0) {
        if (zero_ && positive_zero(value)) zero_ = zeros(count);
        else write().resize(count, value);
    }
    void assign(size_type count, double value) {
        if (positive_zero(value)) { Vector{}.swap(owned_); zero_ = zeros(count); }
        else { owned_.assign(count, value); zero_.reset(); }
    }
    template<class It> void assign(It first, It last) { owned_.assign(first, last); zero_.reset(); }
    void assign(std::initializer_list<double> values) { owned_.assign(values); zero_.reset(); }
    void push_back(double value) { write().push_back(value); }
    template<class... Args> reference emplace_back(Args&&... args) { return write().emplace_back(std::forward<Args>(args)...); }
    void pop_back() { write().pop_back(); }
    iterator insert(const_iterator pos, double value) { const auto offset = pos - cbegin(); return write().insert(owned_.cbegin() + offset, value); }
    template<class It> iterator insert(const_iterator pos, It first, It last) {
        const auto offset = pos - cbegin(); auto& values = write(); return values.insert(values.cbegin() + offset, first, last);
    }
    iterator erase(const_iterator pos) { const auto offset = pos - cbegin(); auto& values = write(); return values.erase(values.cbegin() + offset); }
    iterator erase(const_iterator first, const_iterator last) {
        const auto a = first - cbegin(), b = last - cbegin(); auto& values = write(); return values.erase(values.cbegin() + a, values.cbegin() + b);
    }
    void swap(MassStorage& other) noexcept { owned_.swap(other.owned_); zero_.swap(other.zero_); }
    void swap(Vector& other) { write().swap(other); }
    // Fresh checkpoint cells cannot have retained mutable references.
    void compact_checkpoint_zeros() {
        if (zero_) return;
        for (const auto value : owned_) if (!positive_zero(value)) return;
        const auto count = owned_.size(); assign(count, 0.0);
    }
    friend bool operator==(const MassStorage& a, const MassStorage& b) { return a.read() == b.read(); }
    friend bool operator==(const MassStorage& a, const Vector& b) { return a.read() == b; }
    friend bool operator==(const Vector& a, const MassStorage& b) { return a == b.read(); }
};
}

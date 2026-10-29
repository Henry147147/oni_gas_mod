#pragma once
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ma {
// Owned vectors copy deeply, including after a mutable reference was retained.
// Explicit snapshots share independent const vectors; mutation first creates
// owned bytes. Shared storage never retains a world or containing object.
class MassStorage {
    using Vector = std::vector<double>;
    struct ZeroShared { std::shared_ptr<const Vector> values; };
    struct FrozenShared { std::shared_ptr<const Vector> values; };
    using Storage = std::variant<Vector, ZeroShared, FrozenShared>;
    Storage storage_;

    static_assert(std::is_nothrow_move_constructible_v<Vector>);
    static_assert(std::is_nothrow_default_constructible_v<Vector>);
    static_assert(std::is_nothrow_copy_constructible_v<ZeroShared>);
    static_assert(std::is_nothrow_copy_constructible_v<FrozenShared>);
    static_assert(std::is_nothrow_swappable_v<Storage>);
    static_assert(std::is_nothrow_move_constructible_v<Storage>);
    static_assert(std::is_nothrow_move_assignable_v<Storage>);

    static bool positive_zero(double value) noexcept {
        return std::bit_cast<std::uint64_t>(value) == 0;
    }
    static std::shared_ptr<const Vector> zeros(std::size_t count) {
        if (count == 0) return {};
        thread_local std::shared_ptr<const Vector> recent;
        if (recent && recent->size() == count) return recent;
        static std::mutex mutex;
        static std::map<std::size_t, std::shared_ptr<const Vector>> cache;
        std::lock_guard lock(mutex);
        auto& entry = cache[count];
        if (!entry) entry = std::make_shared<const Vector>(count, 0.0);
        recent = entry;
        return entry;
    }
    [[nodiscard]] const Vector& read() const noexcept {
        switch (storage_.index()) {
        case 0: return std::get<Vector>(storage_);
        case 1: return *std::get<ZeroShared>(storage_).values;
        default: return *std::get<FrozenShared>(storage_).values;
        }
    }
    Vector& write() {
        if (auto* owned = std::get_if<Vector>(&storage_)) return *owned;
        Vector candidate(read());
        // Allocation precedes the no-throw alternative replacement.
        storage_.emplace<Vector>(std::move(candidate));
        return std::get<Vector>(storage_);
    }
    void assign_vector(const Vector& values) {
        if (auto* owned = std::get_if<Vector>(&storage_)) *owned = values;
        else {
            Vector candidate(values);
            storage_.emplace<Vector>(std::move(candidate));
        }
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
    MassStorage(std::initializer_list<double> values) : storage_(std::in_place_type<Vector>, values) {}
    MassStorage(const Vector& values) : storage_(std::in_place_type<Vector>, values) {}
    MassStorage(Vector&& values) noexcept : storage_(std::in_place_type<Vector>, std::move(values)) {}
    MassStorage(const MassStorage&) = default;
    MassStorage(MassStorage&& other) noexcept : storage_(std::move(other.storage_)) {
        // A moved-from shared alternative must never retain a null tag.
        other.storage_.emplace<Vector>();
    }
    MassStorage& operator=(const MassStorage& other) {
        if (this == &other) return *this;
        if (const auto* owned = std::get_if<Vector>(&other.storage_)) assign_vector(*owned);
        else if (const auto* zero = std::get_if<ZeroShared>(&other.storage_)) storage_.emplace<ZeroShared>(*zero);
        else storage_.emplace<FrozenShared>(std::get<FrozenShared>(other.storage_));
        return *this;
    }
    MassStorage& operator=(MassStorage&& other) noexcept {
        if (this != &other) {
            storage_ = std::move(other.storage_);
            other.storage_.emplace<Vector>();
        }
        return *this;
    }
    MassStorage& operator=(const Vector& values) { assign_vector(values); return *this; }
    MassStorage& operator=(Vector&& values) noexcept {
        if (auto* owned = std::get_if<Vector>(&storage_)) *owned = std::move(values);
        else storage_.emplace<Vector>(std::move(values));
        return *this;
    }
    MassStorage& operator=(std::initializer_list<double> values) { assign(values); return *this; }
    operator const Vector&() const noexcept { return read(); }
    [[nodiscard]] bool implicit_zero() const noexcept { return storage_.index() == 1; }
    // Existing mutable references to an owned source cannot affect this copy.
    [[nodiscard]] MassStorage immutable_snapshot_copy() const {
        MassStorage result;
        if (const auto* owned = std::get_if<Vector>(&storage_)) {
            if (!owned->empty()) {
                result.storage_.emplace<FrozenShared>(FrozenShared{std::make_shared<const Vector>(*owned)});
            }
        } else if (const auto* zero = std::get_if<ZeroShared>(&storage_)) result.storage_.emplace<ZeroShared>(*zero);
        else result.storage_.emplace<FrozenShared>(std::get<FrozenShared>(storage_));
        return result;
    }
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
    void clear() noexcept {
        if (auto* owned = std::get_if<Vector>(&storage_)) owned->clear();
        else storage_.emplace<Vector>();
    }
    void reserve(size_type count) { write().reserve(count); }
    void shrink_to_fit() { write().shrink_to_fit(); }
    void resize(size_type count, double value = 0) {
        if (implicit_zero() && positive_zero(value)) assign(count, value);
        else write().resize(count, value);
    }
    void assign(size_type count, double value) {
        if (positive_zero(value)) {
            auto zero = zeros(count);
            if (zero) storage_.emplace<ZeroShared>(ZeroShared{std::move(zero)});
            else storage_.emplace<Vector>();
        } else if (auto* owned = std::get_if<Vector>(&storage_)) owned->assign(count, value);
        else {
            Vector candidate(count, value);
            storage_.emplace<Vector>(std::move(candidate));
        }
    }
    template<class It> void assign(It first, It last) {
        if (auto* owned = std::get_if<Vector>(&storage_)) owned->assign(first, last);
        else {
            Vector candidate(first, last);
            storage_.emplace<Vector>(std::move(candidate));
        }
    }
    void assign(std::initializer_list<double> values) { assign(values.begin(), values.end()); }
    void push_back(double value) { write().push_back(value); }
    template<class... Args> reference emplace_back(Args&&... args) { return write().emplace_back(std::forward<Args>(args)...); }
    void pop_back() { write().pop_back(); }
    iterator insert(const_iterator pos, double value) {
        const auto offset = pos - cbegin();
        auto& owned = write();
        return owned.insert(owned.cbegin() + offset, value);
    }
    template<class It> iterator insert(const_iterator pos, It first, It last) {
        const auto offset = pos - cbegin();
        auto& owned = write();
        return owned.insert(owned.cbegin() + offset, first, last);
    }
    iterator erase(const_iterator pos) {
        const auto offset = pos - cbegin();
        auto& owned = write();
        return owned.erase(owned.cbegin() + offset);
    }
    iterator erase(const_iterator first, const_iterator last) {
        const auto a = first - cbegin(), b = last - cbegin();
        auto& owned = write();
        return owned.erase(owned.cbegin() + a, owned.cbegin() + b);
    }
    void swap(MassStorage& other) noexcept { storage_.swap(other.storage_); }
    void swap(Vector& other) { write().swap(other); }
    void compact_checkpoint_zeros() {
        if (implicit_zero()) return;
        const auto& values = read();
        for (const auto value : values) if (!positive_zero(value)) return;
        const auto count = values.size();
        assign(count, 0.0);
    }
    friend bool operator==(const MassStorage& a, const MassStorage& b) { return a.read() == b.read(); }
    friend bool operator==(const MassStorage& a, const Vector& b) { return a.read() == b; }
    friend bool operator==(const Vector& a, const MassStorage& b) { return a == b.read(); }
};
}

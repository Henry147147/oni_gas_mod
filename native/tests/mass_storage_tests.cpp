#include "mixed_atmospheres/mass_storage.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>

namespace allocation {
thread_local long fail_after = -1;
thread_local std::size_t calls = 0;
}
void* operator new(std::size_t size) {
    ++allocation::calls;
    if (allocation::fail_after >= 0 && allocation::fail_after-- == 0) throw std::bad_alloc();
    if (auto* data = std::malloc(size ? size : 1)) return data;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* data) noexcept { std::free(data); }
void operator delete[](void* data) noexcept { std::free(data); }
void operator delete(void* data, std::size_t) noexcept { std::free(data); }
void operator delete[](void* data, std::size_t) noexcept { std::free(data); }

using ma::MassStorage;
using Vector = std::vector<double>;
std::size_t checks = 0;
void check(bool good, const char* reason) {
    ++checks;
    if (!good) throw std::runtime_error(reason);
}
const MassStorage& read(const MassStorage& values) { return values; }
std::vector<std::uint64_t> bits(const MassStorage& values) {
    std::vector<std::uint64_t> result;
    for (double value : values) result.push_back(std::bit_cast<std::uint64_t>(value));
    return result;
}
std::vector<std::uint64_t> bits(const Vector& values) { return bits(MassStorage(values)); }
Vector fixture() {
    return {0., -0., 2.25, -9.5, std::numeric_limits<double>::infinity(),
            std::bit_cast<double>(std::uint64_t{0x7ff8000012345678})};
}

void retained_reference_and_reuse() {
    MassStorage source(fixture());
    const auto before = bits(source);
    auto& reference = source[2];
    auto* pointer = source.data();
    auto iterator = source.begin() + 3;
    auto frozen = source.immutable_snapshot_copy();
    auto again = frozen.immutable_snapshot_copy();
    reference = 42.;
    pointer[0] = -0.;
    *iterator = 18.;
    check(bits(frozen) == before, "retained writable reference mutated frozen snapshot");
    check(bits(again) == before, "second frozen snapshot changed");
    check(read(frozen).data() == read(again).data(), "frozen snapshot was cloned again");
    check(read(frozen).data() != read(source).data(), "owned storage was aliased");
    const auto allocated = allocation::calls;
    auto reused = again.immutable_snapshot_copy();
    check(allocation::calls == allocated, "reusing frozen storage allocated");
    check(read(reused).data() == read(frozen).data(), "reused frozen bytes differ");
    auto ordinary = source;
    const auto ordinary_bits = bits(ordinary);
    source[2] = 99.;
    check(bits(ordinary) == ordinary_bits, "ordinary owned copy stopped being deep");
    MassStorage assigned;
    assigned = source;
    auto& held = source[3];
    const auto assigned_bits = bits(assigned);
    held = 71.;
    check(bits(assigned) == assigned_bits, "ordinary owned assignment aliases references");
    auto copied_frozen = frozen;
    frozen[2] = 13.;
    check(bits(copied_frozen) == before && bits(again) == before, "thaw changed a frozen peer");
    check(read(frozen).data() != read(again).data(), "mutable write did not materialize storage");
}

using Mutation = std::pair<std::function<void(MassStorage&)>, std::function<void(Vector&)>>;
void mutation_families() {
    const std::vector<Mutation> cases{
        {[](auto& v) { v[2] = 8.; }, [](auto& v) { v[2] = 8.; }},
        {[](auto& v) { v.at(2) = 8.; }, [](auto& v) { v.at(2) = 8.; }},
        {[](auto& v) { v.front() = 8.; }, [](auto& v) { v.front() = 8.; }},
        {[](auto& v) { v.back() = 8.; }, [](auto& v) { v.back() = 8.; }},
        {[](auto& v) { v.data()[1] = 8.; }, [](auto& v) { v.data()[1] = 8.; }},
        {[](auto& v) { *(v.begin()+2) = 8.; }, [](auto& v) { *(v.begin()+2) = 8.; }},
        {[](auto& v) { *v.rbegin() = 8.; }, [](auto& v) { *v.rbegin() = 8.; }},
        {[](auto& v) { v.reserve(24); }, [](auto& v) { v.reserve(24); }},
        {[](auto& v) { v.shrink_to_fit(); }, [](auto& v) { v.shrink_to_fit(); }},
        {[](auto& v) { v.resize(9, .5); }, [](auto& v) { v.resize(9, .5); }},
        {[](auto& v) { v.resize(2); }, [](auto& v) { v.resize(2); }},
        {[](auto& v) { v.resize(0); }, [](auto& v) { v.resize(0); }},
        {[](auto& v) { v.push_back(8.); }, [](auto& v) { v.push_back(8.); }},
        {[](auto& v) { v.emplace_back(8.) = 9.; }, [](auto& v) { v.emplace_back(8.) = 9.; }},
        {[](auto& v) { v.pop_back(); }, [](auto& v) { v.pop_back(); }},
        {[](auto& v) { v.insert(v.cbegin()+2, 8.); }, [](auto& v) { v.insert(v.cbegin()+2, 8.); }},
        {[](auto& v) { const Vector a{8.,9.}; v.insert(v.cbegin()+2,a.begin(),a.end()); },
         [](auto& v) { const Vector a{8.,9.}; v.insert(v.cbegin()+2,a.begin(),a.end()); }},
        {[](auto& v) { v.erase(v.cbegin()+2); }, [](auto& v) { v.erase(v.cbegin()+2); }},
        {[](auto& v) { v.erase(v.cbegin()+1,v.cbegin()+3); }, [](auto& v) { v.erase(v.cbegin()+1,v.cbegin()+3); }},
        {[](auto& v) { v.assign(4, 0.); }, [](auto& v) { v.assign(4, 0.); }},
        {[](auto& v) { v.assign(4, -0.); }, [](auto& v) { v.assign(4, -0.); }},
        {[](auto& v) { const Vector a{8.,-0.,9.}; v.assign(a.begin(),a.end()); },
         [](auto& v) { const Vector a{8.,-0.,9.}; v.assign(a.begin(),a.end()); }},
        {[](auto& v) { v.assign({8.,-0.,9.}); }, [](auto& v) { v.assign({8.,-0.,9.}); }},
        {[](auto& v) { v = {8.,-0.,9.}; }, [](auto& v) { v = {8.,-0.,9.}; }},
        {[](auto& v) { const Vector a{8.,-0.,9.}; v = a; }, [](auto& v) { const Vector a{8.,-0.,9.}; v = a; }},
        {[](auto& v) { Vector a{8.,-0.,9.}; v = std::move(a); }, [](auto& v) { Vector a{8.,-0.,9.}; v = std::move(a); }},
        {[](auto& v) { v.clear(); }, [](auto& v) { v.clear(); }},
        {[](auto& v) { Vector a{8.,-0.}; v.swap(a); }, [](auto& v) { Vector a{8.,-0.}; v.swap(a); }},
        {[](auto& v) { MassStorage a{8.,-0.}; v.swap(a); }, [](auto& v) { Vector a{8.,-0.}; v.swap(a); }},
    };
    for (const auto& [mutate, oracle] : cases) {
        MassStorage source(fixture());
        const auto before = bits(source);
        auto frozen = source.immutable_snapshot_copy();
        auto peer = frozen.immutable_snapshot_copy();
        auto expected = fixture();
        mutate(frozen);
        oracle(expected);
        check(bits(frozen) == bits(expected), "frozen mutation differs from vector oracle");
        check(bits(peer) == before && bits(source) == before, "mutation changed another copy");
        bool reached_success = false;
        for (long fail = 0; fail < 16; ++fail) {
            auto candidate = source.immutable_snapshot_copy();
            auto held_peer = candidate;
            bool refused = false;
            allocation::fail_after = fail;
            try { mutate(candidate); }
            catch (const std::bad_alloc&) { refused = true; }
            allocation::fail_after = -1;
            check(bits(source) == before && bits(held_peer) == before,
                  "failed mutation changed source or frozen peer");
            if (refused) {
                check(bits(candidate) == before, "failed mutation changed visible values");
            } else {
                check(bits(candidate) == bits(expected), "retry after allocation refusal differs");
                reached_success = true;
                break;
            }
        }
        check(reached_success, "mutation did not succeed after allocation-refusal sweep");
    }
}

void zeros_compaction_swap_moves() {
    MassStorage zero(79, 0.);
    auto zero_copy = zero.immutable_snapshot_copy();
    check(zero_copy.implicit_zero() && zero_copy.size() == 79, "implicit zero lost its storage kind");
    zero_copy[0] = 1.;
    check(read(zero)[0] == 0 && read(zero_copy)[0] == 1., "implicit-zero thaw aliases source");
    MassStorage nonzero(fixture());
    auto frozen = nonzero.immutable_snapshot_copy();
    const auto before = bits(frozen);
    frozen.compact_checkpoint_zeros();
    check(bits(frozen) == before && !frozen.implicit_zero(), "compaction discarded frozen nonzeros");
    MassStorage signed_zero{0., -0., 0.};
    auto signed_frozen = signed_zero.immutable_snapshot_copy();
    signed_frozen.compact_checkpoint_zeros();
    check(bits(signed_frozen) == bits(signed_zero) && !signed_frozen.implicit_zero(), "compaction lost signed zero");
    MassStorage owned_zero(Vector(9, 0.));
    auto frozen_zero = owned_zero.immutable_snapshot_copy();
    frozen_zero.compact_checkpoint_zeros();
    check(frozen_zero.implicit_zero() && frozen_zero.size() == 9, "positive-zero compaction has wrong size");
    auto moved = std::move(frozen);
    check(bits(moved) == before, "moving frozen storage changed bits");
    frozen.assign(2, 7.);
    check(frozen.size() == 2 && read(frozen)[0] == 7., "moved-from storage is unusable");
    moved = moved;
    check(bits(moved) == before, "self assignment changed frozen data");
    MassStorage empty;
    auto empty_copy = empty.immutable_snapshot_copy();
    check(empty_copy.empty(), "empty snapshot changed size");
    empty_copy.swap(moved);
    check(bits(empty_copy) == before && moved.empty(), "swapping frozen and empty failed");
}

void allocation_refusal() {
    MassStorage source(fixture());
    const auto before = bits(source);
    const auto pointer = read(source).data();
    unsigned refusals = 0;
    for (long fail = 0; fail < 8; ++fail) {
        bool refused = false;
        MassStorage result;
        allocation::fail_after = fail;
        try { result = source.immutable_snapshot_copy(); }
        catch (const std::bad_alloc&) { refused = true; }
        allocation::fail_after = -1;
        check(bits(source) == before && read(source).data() == pointer, "failed freeze changed source");
        if (!refused) {
            check(bits(result) == before, "successful freeze changed bits");
            break;
        }
        ++refusals;
    }
    check(refusals > 0, "freeze allocation refusals were not exercised");
    auto frozen = source.immutable_snapshot_copy();
    auto peer = frozen;
    allocation::fail_after = 0;
    bool refused = false;
    try { frozen[2] = 99.; }
    catch (const std::bad_alloc&) { refused = true; }
    allocation::fail_after = -1;
    check(refused && bits(frozen) == before && bits(peer) == before, "failed thaw changed immutable data");
    check(read(frozen).data() == read(peer).data(), "failed thaw replaced original storage");
    frozen[2] = 99.;
    check(bits(peer) == before && read(frozen)[2] == 99., "thaw retry broke isolation");
}

void concurrent_independent_copies() {
    MassStorage owner(fixture());
    auto frozen = owner.immutable_snapshot_copy();
    const auto before = bits(frozen);
    std::atomic<bool> valid{true};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 8; ++worker) workers.emplace_back([&, worker] {
        for (unsigned i = 0; i < 512; ++i) {
            auto local = frozen.immutable_snapshot_copy();
            if (bits(local) != before) valid.store(false);
            local[2] = static_cast<double>(worker + i);
            if (bits(frozen) != before) valid.store(false);
        }
    });
    for (auto& worker : workers) worker.join();
    check(valid.load() && bits(frozen) == before, "independent concurrent copies share writes");
}

void no_parent_retention() {
    struct Parent { MassStorage values{fixture()}; };
    auto parent = std::make_shared<Parent>();
    std::weak_ptr<Parent> weak = parent;
    const auto before = bits(parent->values);
    auto frozen = parent->values.immutable_snapshot_copy();
    parent.reset();
    check(weak.expired() && bits(frozen) == before, "immutable storage retained its containing parent");
    auto second_parent = std::make_shared<Parent>();
    second_parent->values = frozen;
    std::weak_ptr<Parent> second_weak = second_parent;
    auto second = second_parent->values.immutable_snapshot_copy();
    second_parent.reset();
    check(second_weak.expired() && bits(second) == before, "reused frozen vector retained a parent chain");
}

MassStorage mode(unsigned kind) {
    if (kind == 1) return MassStorage(6, 0.);
    MassStorage owned(fixture());
    return kind == 2 ? owned.immutable_snapshot_copy() : std::move(owned);
}
const void* volatile constructor_storage_escape = nullptr;
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noipa))
#elif defined(__clang__)
__attribute__((noinline, optnone))
#endif
MassStorage observable_constructor_copy(const MassStorage& source) {
    return MassStorage(source);
}
void alternative_transitions() {
    for (unsigned from = 0; from < 3; ++from) {
        auto source = mode(from);
        const auto before = bits(source);
        auto moved = std::move(source);
        check(source.size() <= source.capacity(), "moved-from alternative is not readable");
        check(bits(moved) == before, "moving an alternative changed values");
        source = std::move(moved);
        check(moved.size() <= moved.capacity(), "move-assigned source is not readable");
        check(bits(source) == before, "move assignment changed values");
        source = source;
        source.swap(source);
        check(bits(source) == before, "self copy/swap changed alternative");
        source = std::move(source);
        check(bits(source) == before && source.size() <= source.capacity(), "self move left invalid alternative");
        const Vector& view = source;
        source = view;
        check(bits(source) == before, "assigning its own read view lost values");
        auto copy_source = mode(from);
        const auto mode_probe = copy_source.immutable_snapshot_copy();
        const bool source_shared = copy_source.implicit_zero() || read(mode_probe).data() == read(copy_source).data();
        check(source_shared == (from != 0), "constructor fixture has wrong source mode");
        std::optional<MassStorage> constructed;
        allocation::fail_after = 0;
        bool failed = false;
        try { constructed.emplace(observable_constructor_copy(copy_source)); }
        catch (const std::bad_alloc&) { failed = true; }
        allocation::fail_after = -1;
        if (constructed) {
            constructor_storage_escape = read(*constructed).data();
            check(bits(*constructed) == before, "observed constructed copy changed values");
            check((read(*constructed).data() == read(copy_source).data()) == source_shared,
                  "observed constructor copy lost deep/shared storage semantics");
        }
        check(bits(copy_source) == before, "failed copy construction changed source");
        check(failed == (from == 0), "copy construction did not preserve deep/shared modes");
        for (unsigned to = 0; to < 3; ++to) {
            auto original = mode(from);
            auto target = mode(to);
            const auto target_before = bits(target);
            allocation::fail_after = 0;
            bool refused = false;
            try { target = original; }
            catch (const std::bad_alloc&) { refused = true; }
            allocation::fail_after = -1;
            check(bits(original) == before, "assignment changed source alternative");
            check(bits(target) == (refused ? target_before : before), "failed type-changing assignment lost values");
            check(target.size() <= target.capacity(), "assignment left invalid alternative");
            target = original;
            check(bits(target) == before, "type-changing retry differs");
            auto left = mode(from), right = mode(to);
            const auto left_before = bits(left), right_before = bits(right);
            const bool left_zero = left.implicit_zero(), right_zero = right.implicit_zero();
            left.swap(right);
            check(bits(left) == right_before && bits(right) == left_before, "cross-alternative swap changed values");
            check(left.implicit_zero() == right_zero && right.implicit_zero() == left_zero, "cross-alternative swap lost mode");
        }
    }
    MassStorage owned(fixture());
    owned.reserve(64);
    const auto capacity = owned.capacity();
    owned.clear();
    check(owned.empty() && owned.capacity() == capacity, "owned clear lost vector capacity semantics");
}

void uncached_zero_assignment_refusal() {
    MassStorage target(fixture());
    target.reserve(64);
    const auto before = bits(target);
    const auto* pointer = read(target).data();
    const auto capacity = target.capacity();
    allocation::fail_after = 0;
    bool refused = false;
    try { target.assign(1009, 0.); }
    catch (const std::bad_alloc&) { refused = true; }
    allocation::fail_after = -1;
    check(refused, "uncached positive-zero allocation refusal was not exercised");
    check(bits(target) == before, "positive-zero refusal discarded prior values");
    check(read(target).data() == pointer && target.capacity() == capacity, "positive-zero refusal replaced owned storage");
}

int main() {
    try {
        retained_reference_and_reuse();
        mutation_families();
        zeros_compaction_swap_moves();
        allocation_refusal();
        concurrent_independent_copies();
        no_parent_retention();
        alternative_transitions();
        uncached_zero_assignment_refusal();
        std::cout << "PASS frozen mass storage " << checks << " controls\n";
        return 0;
    } catch (const std::exception& error) {
        allocation::fail_after = -1;
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

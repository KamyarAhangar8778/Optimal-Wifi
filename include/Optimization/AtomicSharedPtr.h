#pragma once

#include <atomic>
#include <utility>

namespace uniuno {

/**
 * @brief Custom lightweight smart pointer designed to completely replace
 * std::shared_ptr and avoid the massive Flash/RAM bloat of libstdc++.
 *
 * NOTE on storage: State (value + refcount) is heap-allocated and its address
 * is shared by every copy/move of the pointer. This is required for correct
 * shared-ownership semantics — a per-instance inline buffer cannot outlive the
 * owning instance when a copy outlives the original (use-after-free). See
 * plans/atomicsharedptr-optimization.md change 4 for the rejected alternative.
 */
template <typename T>
class AtomicSharedPtr {
private:
    struct State {
        T value;
        std::atomic<int> m_ref_count;
        template<typename... Args>
        State(Args&&... args) : value(std::forward<Args>(args)...), m_ref_count(1) {}
    };
    State* state;
public:
    AtomicSharedPtr() : state(nullptr) {}
    AtomicSharedPtr(T initial) : state(new State(std::move(initial))) {}

    template<typename... Args>
    static AtomicSharedPtr make(Args&&... args) {
        AtomicSharedPtr ptr;
        ptr.state = new State(std::forward<Args>(args)...);
        return ptr;
    }

    AtomicSharedPtr(const AtomicSharedPtr& other) : state(other.state) {
        if (state) {
            state->m_ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    AtomicSharedPtr& operator=(const AtomicSharedPtr& other) {
        if (this != &other) {
            reset();
            state = other.state;
            if (state) {
                state->m_ref_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    AtomicSharedPtr(AtomicSharedPtr&& other) noexcept : state(other.state) {
        other.state = nullptr;
    }

    AtomicSharedPtr& operator=(AtomicSharedPtr&& other) noexcept {
        if (this != &other) {
            reset();
            state = other.state;
            other.state = nullptr;
        }
        return *this;
    }

    ~AtomicSharedPtr() {
        reset();
    }

    void reset() {
        if (state) {
            if (state->m_ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete state;
            }
            state = nullptr;
        }
    }

    explicit operator bool() const { return state != nullptr; }
    T& operator*() { return state->value; }
    const T& operator*() const { return state->value; }
    T* operator->() { return &state->value; }
    // const overload mirrors std::shared_ptr::operator->: returns T* (not
    // const T*) so existing const-context callers keep working unchanged.
    T* operator->() const { return &state->value; }

    // Pointer-identity comparison (matches std::shared_ptr::operator==):
    // two pointers compare equal iff they own the same underlying State.
    bool operator==(const AtomicSharedPtr& other) const { return state == other.state; }
    bool operator!=(const AtomicSharedPtr& other) const { return state != other.state; }
};

} // namespace uniuno

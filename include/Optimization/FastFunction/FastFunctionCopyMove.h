#pragma once

#include "FastFunctionDecl.h"
#include <cstring>

namespace uniuno {

// ---------------------------------------------------------------------------
// Copy constructor
// ---------------------------------------------------------------------------

template<typename R, typename... Args, size_t Capacity>
FastFunction<R(Args...), Capacity>::FastFunction(const FastFunction& other)
  : invoker_(other.invoker_)
  , vtable_(other.vtable_) {
  if (LIKELY(other.invoker_)) {
    // Mirror the source storage size before copying
    storage_.reserve(other.storage_.size());
    storage_.force_set_size(other.storage_.size());

    if (vtable_) {
      vtable_->copy(storage_.data(), const_cast<void*>(static_cast<const void*>(other.storage_.data())));
    } else {
      // Trivially copyable — use fast bulk memcpy
      std::memcpy(storage_.data(), other.storage_.data(),
                  other.storage_.size() * sizeof(std::max_align_t));
    }
  }
}

// ---------------------------------------------------------------------------
// Move constructor
//
// Optimization: storage_ holds std::max_align_t (a trivially-copyable POD), so
// SmallVector::operator=(SmallVector&&) transfers the bytes via memcpy and only
// zeroes the source size — it does NOT run element destructors. The functor's
// bytes are therefore already at their destination after the move-assign below.
// Re-invoking vtable_->move() would (a) re-construct the functor on top of the
// already-copied bytes (a redundant move-ctor) and (b) for non-trivial functors
// risk aliasing, since the source bytes were bit-copied. We simply let the
// storage move stand and clear the source's dispatch pointers.
// ---------------------------------------------------------------------------

template<typename R, typename... Args, size_t Capacity>
FastFunction<R(Args...), Capacity>::FastFunction(FastFunction&& other) noexcept
  : invoker_(other.invoker_)
  , vtable_(other.vtable_) {
  storage_ = std::move(other.storage_);
  other.invoker_ = nullptr;
  other.vtable_ = nullptr;
}

// ---------------------------------------------------------------------------
// Copy assignment
// ---------------------------------------------------------------------------

template<typename R, typename... Args, size_t Capacity>
FastFunction<R(Args...), Capacity>&
FastFunction<R(Args...), Capacity>::operator=(const FastFunction& other) {
  if (LIKELY(this != &other)) {
    clear();
    invoker_ = other.invoker_;
    vtable_ = other.vtable_;
    if (LIKELY(other.invoker_)) {
      storage_.reserve(other.storage_.size());
      storage_.force_set_size(other.storage_.size());

      if (vtable_) {
        vtable_->copy(storage_.data(), const_cast<void*>(static_cast<const void*>(other.storage_.data())));
      } else {
        std::memcpy(storage_.data(), other.storage_.data(),
                    other.storage_.size() * sizeof(std::max_align_t));
      }
    }
  }
  return *this;
}

// ---------------------------------------------------------------------------
// Move assignment
// ---------------------------------------------------------------------------

template<typename R, typename... Args, size_t Capacity>
FastFunction<R(Args...), Capacity>&
FastFunction<R(Args...), Capacity>::operator=(FastFunction&& other) noexcept {
  if (LIKELY(this != &other)) {
    clear();
    invoker_  = other.invoker_;
    vtable_  = other.vtable_;
    storage_  = std::move(other.storage_);
    other.invoker_ = nullptr;
    other.vtable_ = nullptr;
  }
  return *this;
}

} // namespace uniuno

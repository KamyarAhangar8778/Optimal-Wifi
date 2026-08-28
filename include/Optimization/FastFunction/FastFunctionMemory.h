#pragma once

#include "FastFunctionDecl.h"

namespace uniuno {

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

template<typename R, typename... Args, size_t Capacity>
FastFunction<R(Args...), Capacity>::~FastFunction() {
  clear();
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

template<typename R, typename... Args, size_t Capacity>
void FastFunction<R(Args...), Capacity>::clear() {
  if (UNLIKELY(invoker_ == nullptr)) return;

  if (vtable_) {
    vtable_->destroy(storage_.data());
    vtable_ = nullptr;
  }
  invoker_ = nullptr;
  // storage_ holds std::max_align_t (trivially-destructible): storage_.clear()
  // would only reset size_ (a function call for no real work). The next
  // constructor re-marks the used slots via force_set_size(), and move-assign
  // copies the source size before clearing, so skipping it here is safe and
  // drops one call on the clear path.
}

} // namespace uniuno

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// ADDITIONAL_COMPILE_FLAGS: -D_LIBCPP_ENABLE_CXX26_REMOVED_SHARED_PTR_ATOMICS
// ADDITIONAL_COMPILE_FLAGS: -D_LIBCPP_DISABLE_DEPRECATION_WARNINGS
// UNSUPPORTED: no-threads

// <memory>

// shared_ptr

// template <class T>
// bool
// atomic_compare_exchange_strong_explicit(shared_ptr<T>* p, shared_ptr<T>* v,
//                                         shared_ptr<T> w, memory_order success,
//                                         memory_order failure);                 // Deprecated in C++20, removed in C++26

// UNSUPPORTED: c++03

#include <memory>

#include <atomic>
#include <cassert>

#include "test_macros.h"

int main(int, char**)
{
    {
        std::shared_ptr<int> p(new int(4));
        std::shared_ptr<int> v(new int(3));
        std::shared_ptr<int> w(new int(2));
        bool b = std::atomic_compare_exchange_strong_explicit(&p, &v, w,
                                                              std::memory_order_seq_cst,
                                                              std::memory_order_seq_cst);
        assert(b == false);
        assert(*p == 4);
        assert(*v == 4);
        assert(*w == 2);
    }
    {
        std::shared_ptr<int> p(new int(4));
        std::shared_ptr<int> v = p;
        std::shared_ptr<int> w(new int(2));
        bool b = std::atomic_compare_exchange_strong_explicit(&p, &v, w,
                                                              std::memory_order_seq_cst,
                                                              std::memory_order_seq_cst);
        assert(b == true);
        assert(*p == 2);
        assert(*v == 4);
        assert(*w == 2);
    }

  return 0;
}

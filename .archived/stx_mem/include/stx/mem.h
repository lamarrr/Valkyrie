#pragma once

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <new>
#include <string_view>

#include "stx/rc.h"
#include "stx/span.h"

namespace stx {

/// memory resource management
namespace mem {

/// uses polymorphic default-delete manager
template <typename T>
using Rc = stx::Rc<T*, stx::Manager>;

/// thread-safe
template <typename T>
struct DefaultDeleteManager {
  void ref(T*) {}

  /// this manager is stateless and doesn't observe moves or copies along with
  /// the handle (as in the case of Manager), so we can't use that to
  /// prevent if-switches. we still have to check the address as Unique and Rc
  /// assumes the default-value is also a valid handle, it is left to the
  /// managers to determine what is valid and what is not.
  ///
  void unref(T* ptr) { delete ptr; }
};

/// thread-safe
template <typename T>
struct DefaultArrayDeleteManager {
  void ref(stx::Span<T> handle) {}

  void unref(stx::Span<T> handle) {
    T* raw_ptr_handle = handle.data();
    delete[] raw_ptr_handle;
  }
};

/// uses default delete
template <typename T>
using Unique = stx::Unique<T*, DefaultDeleteManager<T>>;

/// uses array default delete
template <typename T>
using UniqueArray = stx::Unique<stx::Span<T>, DefaultArrayDeleteManager<T>>;

namespace pmr {

/// thread-safe
template <typename Object>
struct IntrusiveRefCountHandle final : public stx::ManagerHandle {
  using object_type = Object;
  Object object;
  alignas(alignof(std::max_align_t) * 2) std::atomic<uint64_t> ref_count;

  template <typename... Args>
  IntrusiveRefCountHandle(uint64_t initial_ref_count, Args&&... args)
      : object{std::forward<Args>(args)...}, ref_count{initial_ref_count} {}

  IntrusiveRefCountHandle(IntrusiveRefCountHandle const&) = delete;
  IntrusiveRefCountHandle(IntrusiveRefCountHandle&&) = delete;

  IntrusiveRefCountHandle& operator=(IntrusiveRefCountHandle const&) = delete;
  IntrusiveRefCountHandle& operator=(IntrusiveRefCountHandle&&) = delete;

  virtual void ref() override final {
    ref_count.fetch_add(1, std::memory_order_relaxed);
  }

  virtual void unref() override final {
    /// NOTE: the last user of the object might have made modifications to the
    /// object's address just before unref is called, this means we need to
    /// ensure correct ordering of the operations/instructions relative to the
    /// unref call (instruction re-ordering).
    ///
    /// we don't need to ensure correct ordering of instructions after this
    /// atomic operation since it is non-observable anyway, since the handle
    /// will be deleted afterwards
    ///
    if (ref_count.fetch_sub(1, std::memory_order_acquire) == 1) {
      delete this;
    }
  }
};

}  // namespace pmr

/// adopt the object
///
/// reference count for this associated object must be >=1 (if any).
template <typename T>
Rc<T> unsafe_make_rc(T& object, stx::Manager&& manager) {
  return Rc<T>{&object, std::move(manager)};
}

template <typename T, typename... Args>
Rc<T> make_rc_inplace(Args&&... args) {
  auto* manager_handle =
      new IntrusiveRefCountHandle<T>{0, std::forward<Args>(args)...};
  stx::Manager manager{*manager_handle};

  // the polymorphic manager manages itself,
  // unref can be called on a polymorphic manager with a different pointer since
  // it doesn't need the handle, it can delete itself independently
  manager.ref(manager_handle);

  stx::Rc<IntrusiveRefCountHandle<T>*, stx::Manager> manager_rc =
      unsafe_make_rc(*manager_handle, std::move(manager));

  return transmute(static_cast<T*>(&manager_handle->object),
                   std::move(manager_rc));
}

/// uses polymorphic default-delete manager
template <typename T>
Rc<T> make_rc(T&& value) {
  return make_rc_inplace<T>(std::move(value));
}

// make_rc_array

/// adopt an object memory handle that is guaranteed to be valid for the
/// lifetime of this Rc struct and any Rc structs constructed or assigned from
/// it. typically used for static storage lifetimes.
///
/// it is advised that this should not be used for scope-local storage as it
/// would be difficult to guarantee that a called function does not retain a
/// copy or move an Rc constructed using this method. However, static storage
/// objects live for the whole duration of the program so this is safe.
///
template <typename T>
Rc<T> make_rc_for_static(T& object) {
  stx::Manager manager{stx::static_storage_manager_handle};
  T* handle = &object;
  manager.ref(handle);
  return unsafe_make_rc(object, std::move(manager));
}

// requires that c_str be non-null.
inline stx::Rc<std::string_view, stx::Manager> make_static_string_rc(
    char const* c_str) {
  return transmute(std::string_view{c_str}, make_rc_for_static(c_str[0]));
}

// make static array rc

template <typename T>
Unique<T> unsafe_make_unique(T& object, DefaultDeleteManager<T>&& manager) {
  return Unique<T>{&object, std::move(manager)};
}

template <typename T>
UniqueArray<T> unsafe_make_unique_array(
    stx::Span<T> handle, DefaultArrayDeleteManager<T>&& manager) {
  return UniqueArray<T>{std::move(handle), std::move(manager)};
}

template <typename T, typename... Args>
Unique<T> make_unique_inplace(Args&&... args) {
  T* handle = new T{std::forward<Args>(args)...};
  DefaultDeleteManager<T> manager{};
  manager.ref(handle);
  return unsafe_make_unique(*handle, std::move(manager));
}

template <typename T>
Unique<T> make_unique(T&& value) {
  return make_unique_inplace<T>(std::move(value));
}

template <typename T>
UniqueArray<T> make_unique_array(size_t number) {
  DefaultArrayDeleteManager<T> manager{};
  T* ptr = new T[number];
  stx::Span<T> handle{ptr, number};
  manager.ref(handle);

  return unsafe_make_unique_array(std::move(handle), std::move(manager));
}

}  // namespace mem
}  // namespace stx

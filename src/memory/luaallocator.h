/*
** Standard C++ Allocator for Lua Memory Management
** See Copyright Notice in lua.h
*/

#ifndef moonallocator_h
#define moonallocator_h

#include <cstddef>
#include <new>
#include <limits>

#include "lmem.h"
#include "lstate.h"

/*
** MoonAllocator - Standard C++ allocator that uses Lua's memory management
**
** This allocator integrates with Lua's GC accounting and respects memory limits.
** It can be used with standard containers like std::vector, std::deque, etc.
**
** Usage example:
**   std::vector<int, MoonAllocator<int>> vec(MoonAllocator<int>(L));
**   vec.push_back(42);
**
** Key features:
** - Respects Lua's memory limits and GC accounting
** - Triggers emergency GC on allocation failure
** - Zero overhead compared to manual moonM_* calls
** - Fully standard-conforming (C++17 allocator requirements)
*/
template<typename T>
class MoonAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;

    // Default constructor (requires moon_State)
    explicit MoonAllocator(moon_State* L) noexcept : L_(L) {
        moon_assert(L != nullptr);
    }

    // Copy constructor
    MoonAllocator(const MoonAllocator& other) noexcept = default;

    // Copy constructor for different types (rebinding)
    template<typename U>
    MoonAllocator(const MoonAllocator<U>& other) noexcept : L_(other.getState()) {}

    // Allocate n objects of type T
    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) return nullptr;

        // Check for overflow
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            moonM_toobig(L_);
        }

        size_t size = n * sizeof(T);
        void* p = moonM_malloc_(L_, size, 0);

        // moonM_malloc_ throws MoonException on failure, but standard allocator
        // should throw std::bad_alloc. However, since we're in Lua context,
        // the exception will be caught and converted to Lua error anyway.
        // For safety, we check nullptr (shouldn't happen).
        if (p == nullptr) {
            throw std::bad_alloc();
        }

        return static_cast<T*>(p);
    }

    // Deallocate n objects at p
    void deallocate(T* p, std::size_t n) noexcept {
        if (p == nullptr) return;
        size_t size = n * sizeof(T);
        moonM_free_(L_, p, size);
    }

    // Get the moon_State (for rebinding)
    moon_State* getState() const noexcept { return L_; }

    // Equality comparison - allocators are equal if they use the same moon_State
    template<typename U>
    bool operator==(const MoonAllocator<U>& other) const noexcept {
        return L_ == other.getState();
    }

    template<typename U>
    bool operator!=(const MoonAllocator<U>& other) const noexcept {
        return !(*this == other);
    }

private:
    moon_State* L_;

    // Allow access to L_ for rebinding
    template<typename U> friend class MoonAllocator;
};

#endif

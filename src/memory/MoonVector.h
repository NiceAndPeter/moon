/*
** Type alias for std::vector with MoonAllocator
** See Copyright Notice in lua.h
*/

#ifndef moonvector_h
#define moonvector_h

#include <vector>
#include "moonallocator.h"

/*
** MoonVector<T> - Convenient type alias for std::vector with MoonAllocator
**
** This provides a std::vector that integrates with Lua's memory management.
** All allocations are tracked by Lua's GC and respect memory limits.
**
** Usage example:
**
**   // In a function that has access to moon_State* L
**   MoonVector<int> numbers(L);
**   numbers.push_back(42);
**   numbers.push_back(84);
**
**   // The vector automatically uses Lua's allocator
**   // Memory is freed when the vector goes out of scope
**
** Benefits over manual moonM_* calls:
** - Automatic memory management (RAII)
** - Exception safety
** - Standard container interface
** - Works with STL algorithms
** - Type-safe
**
** When to use:
** - Temporary arrays during compilation/parsing
** - Internal data structures (not in GC objects)
** - Helper functions needing dynamic arrays
** - New code development
**
** When NOT to use:
** - GC-managed objects (use manual arrays)
** - Hot-path VM code (benchmark first)
** - Public API structures (C compatibility)
** - Fixed-size stack arrays (use native arrays)
*/
template<typename T>
class MoonVector {
public:
    using VectorType = std::vector<T, MoonAllocator<T>>;
    using iterator = typename VectorType::iterator;
    using const_iterator = typename VectorType::const_iterator;
    using size_type = typename VectorType::size_type;
    using value_type = T;

    // Construct with moon_State
    explicit MoonVector(moon_State* L) : vec_(MoonAllocator<T>(L)) {}

    // Forward standard vector operations
    void push_back(const T& value) { vec_.push_back(value); }
    void push_back(T&& value) { vec_.push_back(std::move(value)); }

    template<typename... Args>
    void emplace_back(Args&&... args) { vec_.emplace_back(std::forward<Args>(args)...); }

    void pop_back() { vec_.pop_back(); }
    void clear() noexcept { vec_.clear(); }
    void reserve(size_type n) { vec_.reserve(n); }
    void resize(size_type n) { vec_.resize(n); }
    void resize(size_type n, const T& value) { vec_.resize(n, value); }

    T& operator[](size_type pos) { return vec_[pos]; }
    const T& operator[](size_type pos) const { return vec_[pos]; }

    T& at(size_type pos) { return vec_.at(pos); }
    const T& at(size_type pos) const { return vec_.at(pos); }

    T& front() { return vec_.front(); }
    const T& front() const { return vec_.front(); }
    T& back() { return vec_.back(); }
    const T& back() const { return vec_.back(); }

    T* data() noexcept { return vec_.data(); }
    const T* data() const noexcept { return vec_.data(); }

    bool empty() const noexcept { return vec_.empty(); }
    size_type size() const noexcept { return vec_.size(); }
    size_type capacity() const noexcept { return vec_.capacity(); }

    iterator begin() noexcept { return vec_.begin(); }
    const_iterator begin() const noexcept { return vec_.begin(); }
    const_iterator cbegin() const noexcept { return vec_.cbegin(); }

    iterator end() noexcept { return vec_.end(); }
    const_iterator end() const noexcept { return vec_.end(); }
    const_iterator cend() const noexcept { return vec_.cend(); }

    // Access to underlying vector (for advanced usage)
    VectorType& getVector() noexcept { return vec_; }
    const VectorType& getVector() const noexcept { return vec_; }

private:
    VectorType vec_;
};

#endif

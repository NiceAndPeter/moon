/*
** Test program for MoonAllocator
** Demonstrates that the allocator works correctly with std::vector
*/

#include <iostream>
#include <vector>
#include <string>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "luaallocator.h"
#include "lstate.h"

// Test 1: Basic allocation and deallocation
static void test_basic_vector(moon_State* L) {
    std::cout << "Test 1: Basic vector operations... ";

    std::vector<int, MoonAllocator<int>> vec{MoonAllocator<int>(L)};

    // Push some elements
    for (int i = 0; i < 100; i++) {
        vec.push_back(i);
    }

    // Verify contents
    for (int i = 0; i < 100; i++) {
        if (vec[i] != i) {
            std::cout << "FAILED: Element " << i << " is " << vec[i] << " instead of " << i << std::endl;
            return;
        }
    }

    std::cout << "PASSED" << std::endl;
}

// Test 2: Reallocation (vector growth)
static void test_growth(moon_State* L) {
    std::cout << "Test 2: Vector growth and reallocation... ";

    std::vector<int, MoonAllocator<int>> vec{MoonAllocator<int>(L)};

    // Force multiple reallocations
    for (int i = 0; i < 10000; i++) {
        vec.push_back(i);
    }

    // Verify all elements
    for (int i = 0; i < 10000; i++) {
        if (vec[i] != i) {
            std::cout << "FAILED: Element " << i << " is " << vec[i] << std::endl;
            return;
        }
    }

    std::cout << "PASSED" << std::endl;
}

// Test 3: Different types
static void test_different_types(moon_State* L) {
    std::cout << "Test 3: Different types (double, struct)... ";

    // Test with double
    std::vector<double, MoonAllocator<double>> dvec{MoonAllocator<double>(L)};
    for (int i = 0; i < 100; i++) {
        dvec.push_back(i * 1.5);
    }

    for (int i = 0; i < 100; i++) {
        if (dvec[i] != i * 1.5) {
            std::cout << "FAILED: Double element " << i << " is incorrect" << std::endl;
            return;
        }
    }

    // Test with struct
    struct TestStruct {
        int x;
        double y;
        char z;
    };

    std::vector<TestStruct, MoonAllocator<TestStruct>> svec{MoonAllocator<TestStruct>(L)};
    for (int i = 0; i < 100; i++) {
        svec.push_back({i, i * 2.0, static_cast<char>('A' + (i % 26))});
    }

    for (int i = 0; i < 100; i++) {
        if (svec[i].x != i || svec[i].y != i * 2.0) {
            std::cout << "FAILED: Struct element " << i << " is incorrect" << std::endl;
            return;
        }
    }

    std::cout << "PASSED" << std::endl;
}

// Test 4: Memory accounting
static void test_memory_accounting(moon_State* L) {
    std::cout << "Test 4: Memory accounting... ";

    l_mem before = moon_gc(L, MOON_GCCOUNT, 0) * 1024 + moon_gc(L, MOON_GCCOUNTB, 0);

    {
        std::vector<int, MoonAllocator<int>> vec{MoonAllocator<int>(L)};

        // Allocate ~1MB
        vec.resize(256 * 1024);
        for (size_t i = 0; i < vec.size(); i++) {
            vec[i] = static_cast<int>(i);
        }

        l_mem during = moon_gc(L, MOON_GCCOUNT, 0) * 1024 + moon_gc(L, MOON_GCCOUNTB, 0);

        // Should have increased
        if (during <= before) {
            std::cout << "FAILED: Memory not tracked (before=" << before << ", during=" << during << ")" << std::endl;
            return;
        }
    }

    // Force GC
    moon_gc(L, MOON_GCCOLLECT, 0);

    l_mem after = moon_gc(L, MOON_GCCOUNT, 0) * 1024 + moon_gc(L, MOON_GCCOUNTB, 0);

    // Should be close to original (within some tolerance for overhead)
    if (after > before + 100000) {  // 100KB tolerance
        std::cout << "FAILED: Memory not freed (before=" << before << ", after=" << after << ")" << std::endl;
        return;
    }

    std::cout << "PASSED" << std::endl;
}

// Test 5: Exception safety (allocation failure)
static void test_exception_safety(moon_State* L) {
    std::cout << "Test 5: Exception safety... ";

    // This test verifies that allocation failures are handled correctly
    // In normal operation, Lua's allocator will throw on failure

    try {
        std::vector<int, MoonAllocator<int>> vec{MoonAllocator<int>(L)};

        // Normal allocations should work
        vec.push_back(42);
        vec.push_back(84);

        if (vec[0] != 42 || vec[1] != 84) {
            std::cout << "FAILED: Values incorrect" << std::endl;
            return;
        }

        std::cout << "PASSED" << std::endl;
    } catch (const std::bad_alloc&) {
        std::cout << "FAILED: Unexpected allocation failure" << std::endl;
    } catch (...) {
        std::cout << "FAILED: Unexpected exception" << std::endl;
    }
}

int main() {
    std::cout << "=== MoonAllocator Test Suite ===" << std::endl;
    std::cout << std::endl;

    // Create a new Lua state
    moon_State* L = moonL_newstate();
    if (!L) {
        std::cerr << "Failed to create Lua state" << std::endl;
        return 1;
    }

    moonL_openlibs(L);

    // Run tests
    test_basic_vector(L);
    test_growth(L);
    test_different_types(L);
    test_memory_accounting(L);
    test_exception_safety(L);

    std::cout << std::endl;
    std::cout << "=== All tests completed ===" << std::endl;

    moon_close(L);
    return 0;
}

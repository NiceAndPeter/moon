# CMakeLists.txt for Lua C++ project
# Modern CMake build system for C++23 Lua implementation
cmake_minimum_required(VERSION 3.20)

project(Lua
    VERSION 5.5.0
    DESCRIPTION "Lua programming language - C++ implementation"
    LANGUAGES C CXX
)

# C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(LUA_BUILD_TESTS "Build with test mode enabled" ON)
option(LUA_ENABLE_ASSERTIONS "Enable runtime assertions (for debugging)" ON)
option(LUA_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LUA_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LUA_ENABLE_COVERAGE "Enable code coverage reporting (gcov/lcov)" OFF)
option(LUA_ENABLE_LTO "Enable Link Time Optimization" OFF)
option(LUA_BUILD_SHARED "Build shared library in addition to static" OFF)

# Platform detection
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Compiler warning flags (common to GCC and Clang)
set(WARNING_FLAGS
    -Werror
    -Wfatal-errors
    -Wextra
    -Wshadow
    -Wundef
    -Wwrite-strings
    -Wredundant-decls
    -Wdisabled-optimization
    -Wdouble-promotion
    -Wmissing-declarations
    -Wconversion
    -Wstrict-overflow=2
    # -Wsign-conversion (enabled by -Wconversion) flags ~36 pre-existing benign
    # signed->unsigned conversions (int loop/index vars used as std::array/span
    # subscripts or size_t args). Clang's -Wsign-conversion is stricter than GCC's
    # here. These are harmless (non-negative values); keep as warnings, not fatal.
    -Wno-error=sign-conversion
)

# GCC-specific warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND WARNING_FLAGS
        -Wlogical-op
        -Wno-aggressive-loop-optimizations
        -Wno-invalid-offsetof
        # GCC's -Wmaybe-uninitialized (enabled by -Wextra) cannot see through the
        # GC's custom placement-new allocator, which sets GCObject mark bits before
        # constructors run. It produces false positives on fresh objects (e.g.
        # luaC_objbarrier in LClosure::initUpvals). Keep it as a warning, not fatal.
        -Wno-error=maybe-uninitialized
        # GCC 15's -Wformat-truncation loses track of fixed-size stack buffers
        # through inlining (e.g. l_sprintf into LUA_N2SBUFFSZ buffers in
        # luaO_tostringbuff) and reports a bogus "null destination pointer". Keep
        # it as a warning, not fatal.
        -Wno-error=format-truncation
    )
endif()

# Clang-specific warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND WARNING_FLAGS
        -Wno-invalid-offsetof
    )
endif()

# Optimization and platform flags
set(OPTIMIZE_FLAGS
    -O3
    -fno-stack-protector
    -fno-common
)

# Test mode definitions
if(LUA_BUILD_TESTS)
    add_compile_definitions(
        LUA_USER_H="ltests.h"
    )
    # If assertions are disabled, tell ltests.h not to enable them
    if(NOT LUA_ENABLE_ASSERTIONS)
        add_compile_definitions(LUA_TESTS_NO_ASSERT)
    endif()
endif()

# Assertion control (independent of test mode, for non-test builds)
if(LUA_ENABLE_ASSERTIONS AND NOT LUA_BUILD_TESTS)
    add_compile_definitions(LUAI_ASSERT)
endif()

# Platform definitions
if(LINUX)
    add_compile_definitions(LUA_USE_LINUX)
endif()

# Sanitizer options
if(LUA_ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

if(LUA_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Coverage options
if(LUA_ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    # Disable optimizations for better coverage accuracy
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING "Coverage works best with Debug or RelWithDebInfo build type")
    endif()
endif()

# Link Time Optimization
if(LUA_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    # LTO can be aggressive about strict aliasing - Lua uses type punning extensively
    # so we need to disable strict aliasing optimization to ensure correctness
    add_compile_options(-fno-strict-aliasing)
    add_link_options(-fno-strict-aliasing)
    # Use fat LTO objects to avoid some LTO bugs
    add_compile_options(-ffat-lto-objects)
endif()

# Source files organized by subdirectory
set(LUA_CORE_SOURCES
    src/core/lapi.cpp
    src/core/ldo.cpp
    src/core/lstack.cpp
    src/core/lstate.cpp
    src/core/ltm.cpp
    src/core/ldebug.cpp
)

set(LUA_COMPILER_SOURCES
    src/compiler/lcode.cpp
    src/compiler/llex.cpp
    src/compiler/parser.cpp
    src/compiler/funcstate.cpp
    src/compiler/parselabels.cpp
    src/compiler/parseutils.cpp
    src/compiler/lopcodes.cpp
)

set(LUA_VM_SOURCES
    src/vm/lvm.cpp
    src/vm/lvm_comparison.cpp
    src/vm/lvm_conversion.cpp
    src/vm/lvm_loops.cpp
    src/vm/lvirtualmachine.cpp
)

set(LUA_OBJECT_SOURCES
    src/objects/lobject.cpp
    src/objects/ltable.cpp
    src/objects/lstring.cpp
    src/objects/lfunc.cpp
    src/objects/lctype.cpp
)

set(LUA_MEMORY_SOURCES
    src/memory/lgc.cpp
    src/memory/lmem.cpp
    src/memory/gc/gc_core.cpp
    src/memory/gc/gc_marking.cpp
    src/memory/gc/gc_sweeping.cpp
    src/memory/gc/gc_finalizer.cpp
    src/memory/gc/gc_weak.cpp
    src/memory/gc/gc_collector.cpp
)

set(LUA_SERIALIZATION_SOURCES
    src/serialization/lundump.cpp
    src/serialization/ldump.cpp
    src/serialization/lzio.cpp
)

set(LUA_AUX_SOURCES
    src/auxiliary/lauxlib.cpp
    src/auxiliary/linit.cpp
)

set(LUA_LIB_SOURCES
    src/libraries/lbaselib.cpp
    src/libraries/ldblib.cpp
    src/libraries/liolib.cpp
    src/libraries/lmathlib.cpp
    src/libraries/loslib.cpp
    src/libraries/ltablib.cpp
    src/libraries/lstrlib.cpp
    src/libraries/lutf8lib.cpp
    src/libraries/loadlib.cpp
    src/libraries/lcorolib.cpp
)

# Test source (only in test mode)
if(LUA_BUILD_TESTS)
    set(LUA_TEST_SOURCES src/testing/ltests.cpp)
else()
    set(LUA_TEST_SOURCES "")
endif()

# Public headers
set(LUA_PUBLIC_HEADERS
    include/lua.h
    include/luaconf.h
    include/lualib.h
)

# All library sources
set(LUA_ALL_SOURCES
    ${LUA_CORE_SOURCES}
    ${LUA_COMPILER_SOURCES}
    ${LUA_VM_SOURCES}
    ${LUA_OBJECT_SOURCES}
    ${LUA_MEMORY_SOURCES}
    ${LUA_SERIALIZATION_SOURCES}
    ${LUA_AUX_SOURCES}
    ${LUA_LIB_SOURCES}
    ${LUA_TEST_SOURCES}
)

# Static library target
add_library(liblua_static STATIC
    ${LUA_ALL_SOURCES}
)

# Set output name to liblua.a (remove double lib prefix)
set_target_properties(liblua_static PROPERTIES
    OUTPUT_NAME lua
    PUBLIC_HEADER "${LUA_PUBLIC_HEADERS}"
)

target_compile_options(liblua_static PRIVATE
    -Wall
    ${WARNING_FLAGS}
    ${OPTIMIZE_FLAGS}
)

target_link_libraries(liblua_static PUBLIC
    m      # Math library
    dl     # Dynamic linking library (for loadlib)
)

# Export symbols for dynamic loading
target_link_options(liblua_static PUBLIC
    -Wl,-E
)

target_include_directories(liblua_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/objects
        ${CMAKE_CURRENT_SOURCE_DIR}/src/memory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/memory/gc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization
        ${CMAKE_CURRENT_SOURCE_DIR}/src/libraries
        ${CMAKE_CURRENT_SOURCE_DIR}/src/testing
)

# Optional shared library
if(LUA_BUILD_SHARED)
    add_library(liblua_shared SHARED
        ${LUA_ALL_SOURCES}
    )

    set_target_properties(liblua_shared PROPERTIES
        OUTPUT_NAME lua
        VERSION ${PROJECT_VERSION}
        SOVERSION 5.5
        PUBLIC_HEADER "${LUA_PUBLIC_HEADERS}"
    )

    target_compile_options(liblua_shared PRIVATE
        -Wall
        ${WARNING_FLAGS}
        ${OPTIMIZE_FLAGS}
    )

    target_link_libraries(liblua_shared PUBLIC m dl)
    target_link_options(liblua_shared PUBLIC -Wl,-E)

    target_include_directories(liblua_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler
            ${CMAKE_CURRENT_SOURCE_DIR}/src/vm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/objects
            ${CMAKE_CURRENT_SOURCE_DIR}/src/memory
            ${CMAKE_CURRENT_SOURCE_DIR}/src/memory/gc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization
            ${CMAKE_CURRENT_SOURCE_DIR}/src/libraries
            ${CMAKE_CURRENT_SOURCE_DIR}/src/testing
    )
endif()

# Lua interpreter executable
add_executable(lua
    src/interpreter/lua.cpp
)

target_link_libraries(lua PRIVATE
    liblua_static
)

target_compile_options(lua PRIVATE
    -Wall
    ${WARNING_FLAGS}
    ${OPTIMIZE_FLAGS}
)

target_include_directories(lua PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/testing
    ${CMAKE_CURRENT_SOURCE_DIR}/src/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/memory/gc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/objects
    ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization
)

# LuaAllocator test executable (optional, for testing the allocator)
# Note: Only build with GCC due to C++23 libstdc++ compatibility issues with Clang 15
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_executable(test_luaallocator
        test_luaallocator.cpp
    )

    target_link_libraries(test_luaallocator PRIVATE
        liblua_static
    )

    target_compile_options(test_luaallocator PRIVATE
        -Wall
        ${WARNING_FLAGS}
        ${OPTIMIZE_FLAGS}
    )

    target_include_directories(test_luaallocator PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/testing
        ${CMAKE_CURRENT_SOURCE_DIR}/src/memory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/memory/gc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/objects
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization
    )
endif()

# Install targets
include(GNUInstallDirs)

install(TARGETS liblua_static lua
    EXPORT LuaTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(LUA_BUILD_SHARED)
    install(TARGETS liblua_shared
        EXPORT LuaTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

# Export targets for use by other CMake projects
install(EXPORT LuaTargets
    FILE LuaTargets.cmake
    NAMESPACE Lua::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

# Create package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

# Testing with CTest
if(LUA_BUILD_TESTS)
    enable_testing()

    # Build test library modules
    set(TEST_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/testes/libs)
    set(TEST_LIB_OUTPUT_DIR ${TEST_LIB_DIR})

    # Common compile options for test libraries
    set(TEST_LIB_COMPILE_OPTIONS -Wall -O2 -fPIC -ULUA_USER_H)

    # lib1.so
    add_library(lib1 MODULE ${TEST_LIB_DIR}/lib1.c)
    set_target_properties(lib1 PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIB_OUTPUT_DIR}
    )
    target_include_directories(lib1 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary
    )
    target_compile_options(lib1 PRIVATE ${TEST_LIB_COMPILE_OPTIONS})
    set_property(TARGET lib1 PROPERTY COMPILE_DEFINITIONS "")

    # lib11.so
    add_library(lib11 MODULE ${TEST_LIB_DIR}/lib11.c)
    set_target_properties(lib11 PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIB_OUTPUT_DIR}
    )
    target_include_directories(lib11 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary
    )
    target_compile_options(lib11 PRIVATE ${TEST_LIB_COMPILE_OPTIONS})
    set_property(TARGET lib11 PROPERTY COMPILE_DEFINITIONS "")

    # lib2.so
    add_library(lib2 MODULE ${TEST_LIB_DIR}/lib2.c)
    set_target_properties(lib2 PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIB_OUTPUT_DIR}
    )
    target_include_directories(lib2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary
    )
    target_compile_options(lib2 PRIVATE ${TEST_LIB_COMPILE_OPTIONS})
    set_property(TARGET lib2 PROPERTY COMPILE_DEFINITIONS "")

    # lib21.so
    add_library(lib21 MODULE ${TEST_LIB_DIR}/lib21.c)
    set_target_properties(lib21 PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIB_OUTPUT_DIR}
    )
    target_include_directories(lib21 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary
    )
    target_compile_options(lib21 PRIVATE ${TEST_LIB_COMPILE_OPTIONS})
    set_property(TARGET lib21 PROPERTY COMPILE_DEFINITIONS "")

    # lib2-v2.so (built from lib22.c)
    add_library(lib2-v2 MODULE ${TEST_LIB_DIR}/lib22.c)
    set_target_properties(lib2-v2 PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIB_OUTPUT_DIR}
    )
    target_include_directories(lib2-v2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/auxiliary
    )
    target_compile_options(lib2-v2 PRIVATE ${TEST_LIB_COMPILE_OPTIONS})
    set_property(TARGET lib2-v2 PROPERTY COMPILE_DEFINITIONS "")

    # Create a custom target to build all test libraries
    add_custom_target(test_libs ALL
        DEPENDS lib1 lib11 lib2 lib21 lib2-v2
    )

    add_test(
        NAME LuaTestSuite
        COMMAND lua all.lua
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/testes
    )

    set_tests_properties(LuaTestSuite PROPERTIES
        TIMEOUT 120
        PASS_REGULAR_EXPRESSION "final OK"
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Lua ${PROJECT_VERSION} Configuration Summary:")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Test Mode: ${LUA_BUILD_TESTS}")
message(STATUS "  Assertions: ${LUA_ENABLE_ASSERTIONS}")
message(STATUS "  Build Shared Library: ${LUA_BUILD_SHARED}")
message(STATUS "  AddressSanitizer: ${LUA_ENABLE_ASAN}")
message(STATUS "  UBSanitizer: ${LUA_ENABLE_UBSAN}")
message(STATUS "  LTO: ${LUA_ENABLE_LTO}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")

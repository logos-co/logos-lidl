cmake_minimum_required(VERSION 3.14)
project(LogosLidl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# logos-lidl — the language-neutral LIDL frontend: lexer, parser, AST,
# serializer, validator. Pure standard C++, zero dependencies. The .lidl
# text artifact is the cross-language interchange every Logos SDK backend
# (C++, Rust, ...) generates bindings from.

set(LIDL_FRONTEND_SOURCES
    src/lexer.cpp
    src/parser.cpp
    src/serializer.cpp
    src/validator.cpp
)
set(LIDL_C_SOURCES
    src/json.cpp
    src/lidl_c.cpp
)

add_library(logos_lidl STATIC ${LIDL_FRONTEND_SOURCES})

target_include_directories(logos_lidl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS logos_lidl
    EXPORT logos-lidlTargets
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)
install(DIRECTORY include/lidl DESTINATION include)
install(EXPORT logos-lidlTargets
    FILE logos-lidlTargets.cmake
    NAMESPACE logos-lidl::
    DESTINATION lib/cmake/logos-lidl
)

# logos_lidl_c — the C ABI / JSON bridge over the core frontend. This is the
# layer non-C++ SDKs (Rust, ...) link against: it exposes parse/serialize/
# validate across the C boundary by exchanging the AST as JSON. nlohmann/json
# is confined here so the core lib above stays dependency-free. Installed as a
# plain archive (no CMake export) — consumers link it by path.
find_package(nlohmann_json REQUIRED)
add_library(logos_lidl_c STATIC ${LIDL_C_SOURCES})
target_link_libraries(logos_lidl_c PUBLIC logos_lidl PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(logos_lidl_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
install(TARGETS logos_lidl_c ARCHIVE DESTINATION lib)

# The same C ABI as a LOADABLE library, for SDKs that bind lidl_* at runtime
# through dlopen/FFI instead of linking C++ — the js-sdk's koffi codegen is the
# first. Mirrors logos-protocol's logos_protocol_shared.
#
# It compiles the frontend sources DIRECTLY rather than linking the two static
# archives: a STATIC archive is not built position-independent on every
# platform, so pulling one into a shared object fails to link on Linux.
# nlohmann is header-only, so the result carries no runtime dependency beyond
# the C++ standard library — which is what makes it dlopen-able from a runtime
# that knows nothing about this build.
#
# Default visibility is hidden so the ONLY exported symbols are the four
# LIDL_C_EXPORT-annotated entry points in include/lidl/lidl_c.h; without this
# every internal C++ symbol would leak into the dynamic table.
#
# Deliberately NOT in the export set: in-tree consumers link the static
# archives, so find_package(logos-lidl) is unaffected by this target existing.
add_library(logos_lidl_c_shared SHARED ${LIDL_FRONTEND_SOURCES} ${LIDL_C_SOURCES})
target_link_libraries(logos_lidl_c_shared PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(logos_lidl_c_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
set_target_properties(logos_lidl_c_shared PROPERTIES
    OUTPUT_NAME logos_lidl_c
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
install(TARGETS logos_lidl_c_shared
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/logos-lidlConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/logos-lidlConfig.cmake"
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/logos-lidlTargets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/logos-lidlConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/logos-lidlConfigVersion.cmake"
    DESTINATION lib/cmake/logos-lidl
)

option(LOGOS_LIDL_BUILD_TESTS "Build the logos-lidl test suite" ON)
if(LOGOS_LIDL_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)
    add_executable(lidl_tests tests/test_lidl.cpp)
    # nlohmann is linked into the tests (not just into logos_lidl_c) so the
    # C-ABI tests can assert on the JSON wire form structurally instead of by
    # substring — the derived optionality keys are checked field by field.
    target_link_libraries(lidl_tests PRIVATE logos_lidl logos_lidl_c
        nlohmann_json::nlohmann_json GTest::gtest GTest::gtest_main)
    gtest_discover_tests(lidl_tests)
endif()

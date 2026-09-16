cmake_minimum_required(VERSION 3.14)
project(LogosLidl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# logos-lidl — the language-neutral LIDL frontend: lexer, parser, AST,
# serializer, validator. Pure standard C++, zero dependencies. The .lidl
# text artifact is the cross-language interchange every Logos SDK backend
# (C++, Rust, ...) generates bindings from.

add_library(logos_lidl STATIC
    src/identity.cpp
    src/lexer.cpp
    src/parser.cpp
    src/serializer.cpp
    src/validator.cpp
)

target_include_directories(logos_lidl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS logos_lidl
    EXPORT logos-lidlTargets
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)
# cli.hpp stays out: its library (logos_lidl_cli) is not installed.
install(DIRECTORY include/lidl DESTINATION include PATTERN "cli.hpp" EXCLUDE)
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
add_library(logos_lidl_c STATIC
    src/json.cpp
    src/lidl_c.cpp
)
target_link_libraries(logos_lidl_c PUBLIC logos_lidl PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(logos_lidl_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
install(TARGETS logos_lidl_c ARCHIVE DESTINATION lib)

# Consumers link both archives into shared objects (e.g. Qt plugins).
set_target_properties(logos_lidl logos_lidl_c PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

option(LOGOS_LIDL_BUILD_CLI "Build the lidl command-line tool" ON)
set(LOGOS_LIDL_GIT_REV "unknown" CACHE STRING "Revision reported by lidl --version")
set(LOGOS_LIDL_VERSION_DEFINES
    LOGOS_LIDL_VERSION="${PROJECT_VERSION}"
    LOGOS_LIDL_GIT_REV="${LOGOS_LIDL_GIT_REV}")
if(LOGOS_LIDL_BUILD_CLI)
    # A library so lidl_tests can run the CLI in-process on string streams.
    add_library(logos_lidl_cli STATIC src/cli.cpp)
    target_link_libraries(logos_lidl_cli PUBLIC logos_lidl logos_lidl_c
        PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(logos_lidl_cli PRIVATE ${LOGOS_LIDL_VERSION_DEFINES})

    add_executable(lidl tools/lidl_main.cpp)
    target_link_libraries(lidl PRIVATE logos_lidl_cli logos_lidl logos_lidl_c)
    install(TARGETS lidl RUNTIME DESTINATION bin)
endif()

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
    if(LOGOS_LIDL_BUILD_CLI)
        target_sources(lidl_tests PRIVATE tests/test_cli.cpp)
        target_link_libraries(lidl_tests PRIVATE logos_lidl_cli)
        target_compile_definitions(lidl_tests PRIVATE ${LOGOS_LIDL_VERSION_DEFINES})
    endif()
    gtest_discover_tests(lidl_tests)
endif()

cmake_minimum_required(VERSION 3.14)
project(LogosLidl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# logos-lidl — the language-neutral LIDL frontend: lexer, parser, AST,
# serializer, validator. Pure standard C++, zero dependencies. The .lidl
# text artifact is the cross-language interchange every Logos SDK backend
# (C++, Rust, ...) generates bindings from.

add_library(logos_lidl STATIC
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
    target_link_libraries(lidl_tests PRIVATE logos_lidl logos_lidl_c GTest::gtest GTest::gtest_main)
    gtest_discover_tests(lidl_tests)
endif()

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
    target_link_libraries(lidl_tests PRIVATE logos_lidl GTest::gtest GTest::gtest_main)
    gtest_discover_tests(lidl_tests)
endif()

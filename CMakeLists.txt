# Minimum required version of CMake.
# 3.15 is a safe baseline for modern features like CONFIGURE_DEPENDS.
cmake_minimum_required(VERSION 3.15)

# Declare the project and the language(s) used.
project(libera-core LANGUAGES CXX)

# ---- Compiler settings ----
# Use C++17 and disable compiler-specific extensions (for portability).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Source files for the library ----
# This command scans the `src/` folder for all .cpp files.
# GLOB_RECURSE means it will look in subdirectories too.
# CONFIGURE_DEPENDS tells CMake to re-run if new files are added.
file(GLOB_RECURSE LIBERA_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Define the actual library target "libera-core".
# It will compile all .cpp files found above into a static library by default.
add_library(libera-core ${LIBERA_CORE_SOURCES})

# Tell consumers (and the tests) where to find the public headers.
# - BUILD_INTERFACE: during this project’s build, use our local "include" dir.
# - INSTALL_INTERFACE: if we ever install this lib, use "include".
target_include_directories(libera-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # Add vendored Asio include directory
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libs/asio/include>
)

# Add define so NetConfig.hpp knows we’re using standalone Asio
target_compile_definitions(libera-core PUBLIC ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(libera-core PUBLIC ws2_32)
endif()

# ---- Testing setup ----
enable_testing()

# Find all test source files (*.cpp) under the "tests" folder.
file(GLOB_RECURSE LIBERA_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

# For each test source file, create an executable and register it with CTest.
foreach(test_source ${LIBERA_TEST_SOURCES})
    # Extract the filename without extension (e.g. smoke_controller.cpp → smoke_controller).
    get_filename_component(test_name ${test_source} NAME_WE)

    # Build a test executable.
    add_executable(${test_name} ${test_source})

    # Link it against our library so it can use Controller, etc.
    target_link_libraries(${test_name} PRIVATE libera-core)

    # Register the test with CTest.
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---- Example / main executable ----
add_executable(libera-main apps/main.cpp)
target_link_libraries(libera-main PRIVATE libera-core)
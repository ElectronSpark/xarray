cmake_minimum_required(VERSION 3.10)
project(xarray LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(XA_ENABLE_LOCK "Enable internal locking (XA_CONFIG_LOCK)" OFF)
option(XA_ENABLE_RCU  "Enable RCU read-side protection (XA_CONFIG_RCU)" OFF)
option(XA_BUILD_TESTS "Build unit tests (requires cmocka)" ON)
option(ENABLE_ASAN    "Enable AddressSanitizer" ON)

# Collect compile definitions based on options
set(XA_COMPILE_DEFS "")
if(XA_ENABLE_LOCK)
    list(APPEND XA_COMPILE_DEFS XA_CONFIG_LOCK)
endif()
if(XA_ENABLE_RCU)
    list(APPEND XA_COMPILE_DEFS XA_CONFIG_RCU)
endif()

# Library
add_library(xarray src/xarray.c)
target_include_directories(xarray PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(xarray PUBLIC ${XA_COMPILE_DEFS})
target_compile_options(xarray PRIVATE -Wall -Wextra -Wpedantic)

# Link pthread when locking is enabled (default lock uses pthread_mutex)
if(XA_ENABLE_LOCK)
    find_package(Threads REQUIRED)
    target_link_libraries(xarray PUBLIC Threads::Threads)
endif()

# Tests
if(XA_BUILD_TESTS)
    enable_testing()

    find_library(CMOCKA_LIB cmocka)
    find_path(CMOCKA_INCLUDE cmocka.h)

    if(CMOCKA_LIB AND CMOCKA_INCLUDE)
        set(ASAN_COMPILE_FLAGS "")
        set(ASAN_LINK_FLAGS "")
        if(ENABLE_ASAN)
            set(ASAN_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer)
            set(ASAN_LINK_FLAGS -fsanitize=address)
        endif()

        add_executable(test_xarray test/test_xarray.c)
        target_link_libraries(test_xarray PRIVATE xarray ${CMOCKA_LIB})
        target_include_directories(test_xarray PRIVATE ${CMOCKA_INCLUDE})
        target_compile_options(test_xarray PRIVATE
            -Wall -Wextra ${ASAN_COMPILE_FLAGS}
        )
        target_link_options(test_xarray PRIVATE ${ASAN_LINK_FLAGS})
        target_compile_definitions(test_xarray PRIVATE ${XA_COMPILE_DEFS})

        add_test(NAME xarray_tests COMMAND test_xarray)
    else()
        message(WARNING "cmocka not found — tests will not be built. "
                        "Install libcmocka-dev or set CMOCKA_LIB/CMOCKA_INCLUDE.")
    endif()
endif()

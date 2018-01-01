cmake_minimum_required (VERSION 2.6)

project (lock-benchmark)
set (lock_benchmark_VERSION_MAJOR 1)
set (lock_benchmark_VERSION_MINOR 0)

file(GLOB SOURCES src/*.cc)

include_directories("${PROJECT_SOURCE_DIR}/src")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3")
add_definitions(-Wall -Wextra -pedantic -std=c++14)

# declare our executable
add_executable(lock-benchmark ${SOURCES})

# pthreads
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)
target_link_libraries(lock-benchmark ${CMAKE_THREAD_LIBS_INIT})

# check for spinlocks
include(CheckSymbolExists)
check_symbol_exists(OSSpinLockLock "libkern/OSAtomic.h" HAVE_DARWIN_SPINLOCKS)
check_symbol_exists(pthread_spin_lock "pthread.h" HAVE_PTHREAD_SPINLOCKS)
check_symbol_exists(os_unfair_lock_lock "os/lock.h" HAVE_UNFAIR_LOCKS)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_SOURCE_DIR}/src/config.h"
)

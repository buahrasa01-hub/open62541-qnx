# CMake toolchain file: open62541 for QNX 8.0 (x86_64)
#
# Cross-compile open62541 with the QNX SDP 8.0 toolchain (qcc, GCC 12.2.0).
#
# Usage:
#   source ~/qnx/qnx800/qnxsdp-env.sh   # sets QNX_HOST / QNX_TARGET / PATH
#   cmake -S <open62541-source> -B <build-dir> \
#       -DCMAKE_TOOLCHAIN_FILE=<this-repo>/cmake/toolchain-qnx-x86_64.cmake \
#       -DUA_ARCHITECTURE=posix -DUA_ENABLE_ENCRYPTION=OFF \
#       -DCMAKE_BUILD_TYPE=Release
#
# QNX specifics handled here and in patches/:
#   - CMAKE_SYSTEM_NAME=QNX, so CMake loads its QNX platform module and the
#     upstream CMakeLists QNX exclusions (patches/0003) apply.
#   - qcc with -Vgcc_ntox86_64 selects the x86_64 QNX target.
#   - QNX has no separate libpthread / librt (both are integrated into
#     libc); patches/0003 drops the -lpthread / -lrt link flags.
#   - On QNX the eventloop uses the select() fallback (no epoll) and the
#     system <sys/queue.h> (patches/0001, 0002).

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# SDP location: prefer the environment from qnxsdp-env.sh; fall back to the
# default install path under $HOME.
if(NOT DEFINED ENV{QNX_HOST} OR NOT DEFINED ENV{QNX_TARGET})
    set(_QNX_SDP_DEFAULT "$ENV{HOME}/qnx/qnx800")
    if(EXISTS "${_QNX_SDP_DEFAULT}/qnxsdp-env.sh")
        set(ENV{QNX_HOST} "${_QNX_SDP_DEFAULT}/host/linux/x86_64")
        set(ENV{QNX_TARGET} "${_QNX_SDP_DEFAULT}/target/qnx")
    endif()
    unset(_QNX_SDP_DEFAULT)
endif()

# Compilers: full path when the SDP is known, plain name otherwise (PATH
# from qnxsdp-env.sh must then contain qcc).
if(DEFINED ENV{QNX_HOST} AND EXISTS "$ENV{QNX_HOST}/usr/bin/qcc")
    set(CMAKE_C_COMPILER "$ENV{QNX_HOST}/usr/bin/qcc")
    set(CMAKE_CXX_COMPILER "$ENV{QNX_HOST}/usr/bin/QCC")
else()
    set(CMAKE_C_COMPILER qcc)
    set(CMAKE_CXX_COMPILER QCC)
endif()
set(CMAKE_C_COMPILER_ARG1 -Vgcc_ntox86_64)
set(CMAKE_CXX_COMPILER_ARG1 -Vgcc_ntox86_64)

# Find libraries and headers in the QNX target sysroot only; never pick up
# host programs while cross-compiling.
if(DEFINED ENV{QNX_TARGET})
    set(CMAKE_FIND_ROOT_PATH "$ENV{QNX_TARGET}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

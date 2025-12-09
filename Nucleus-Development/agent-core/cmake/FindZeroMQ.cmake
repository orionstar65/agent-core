# FindZeroMQ.cmake
# Finds ZeroMQ library and sets up variables
#
# Variables set:
#   ZMQ_FOUND - True if ZeroMQ is found
#   ZMQ_INCLUDE_DIRS - ZeroMQ include directories
#   ZMQ_LIBRARIES - ZeroMQ libraries

# Try pkg-config first
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZMQ QUIET libzmq)
endif()

# Fallback to manual find if pkg-config didn't work
if(NOT ZMQ_FOUND)
    find_path(ZMQ_INCLUDE_DIR zmq.h)
    find_library(ZMQ_LIBRARY zmq)
    if(ZMQ_INCLUDE_DIR AND ZMQ_LIBRARY)
        # Windows builds use MSVC only - filter out MSYS2/MinGW paths if accidentally found
        if(MSVC)
            string(TOUPPER "${ZMQ_INCLUDE_DIR}" zmq_include_upper)
            if(zmq_include_upper MATCHES ".*MSYS.*" OR zmq_include_upper MATCHES ".*MINGW.*")
                message(FATAL_ERROR "ZeroMQ found in MSYS2/MinGW path: ${ZMQ_INCLUDE_DIR}. Windows builds must use MSVC with vcpkg. Install ZeroMQ via vcpkg instead.")
                set(ZMQ_INCLUDE_DIR "")
                set(ZMQ_LIBRARY "")
            endif()
        endif()
        
        if(ZMQ_INCLUDE_DIR AND ZMQ_LIBRARY)
            set(ZMQ_FOUND TRUE)
            set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
            set(ZMQ_LIBRARIES ${ZMQ_LIBRARY})
        endif()
    endif()
endif()

# Provide feedback
if(NOT ZMQ_FOUND)
    message(WARNING "ZeroMQ not found. Please install libzmq-dev (Linux) or ZeroMQ (Windows)")
    message(WARNING "Continuing without ZeroMQ support")
else()
    message(STATUS "ZeroMQ found: ${ZMQ_LIBRARIES}")
endif()


cmake_minimum_required(VERSION 3.24)
project(Server)

set(CMAKE_CXX_STANDARD 17)

add_executable(Server Server.cpp size.h)

IF(MINGW)
    target_link_libraries(Server ws2_32)
ENDIF()

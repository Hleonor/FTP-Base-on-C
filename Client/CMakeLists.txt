cmake_minimum_required(VERSION 3.24)
project(Client)

set(CMAKE_CXX_STANDARD 17)

add_executable(Client sizes.h Client.cpp)

IF (MINGW)
    target_link_libraries(Client ws2_32)
ENDIF ()

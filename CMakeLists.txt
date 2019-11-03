cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(hello_world CXX)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(PAHO_CPP_LIB paho-mqttpp3)
set(MPSEE_C_LIB mpsse)

add_executable(app main.cpp)

target_link_libraries(app ${PAHO_CPP_LIB})
target_link_libraries(app ${MPSEE_C_LIB})
target_link_libraries(app Threads::Threads)
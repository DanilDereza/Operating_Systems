cmake_minimum_required(VERSION 3.10)

project(ProcessManagerExample)

if(WIN32)
    set(BUILD_SHARED_LIBS ON)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
elseif(UNIX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_library(process_manager SHARED src/process_manager.cpp)

set_target_properties(process_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(UNIX)
    target_link_libraries(process_manager pthread)
endif()

add_executable(test src/test.cpp)
add_executable(process_child src/process_child.cpp)

target_link_libraries(test process_manager)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
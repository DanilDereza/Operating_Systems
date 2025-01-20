cmake_minimum_required(VERSION 3.10)
project(my_app)

set(CMAKE_CXX_STANDARD 17)

set(SOURCE_DIR src)
set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/child1.cpp
    ${SOURCE_DIR}/child2.cpp
)

add_executable(main ${SOURCE_DIR}/main.cpp)
add_executable(child1 ${SOURCE_DIR}/child1.cpp)
add_executable(child2 ${SOURCE_DIR}/child2.cpp)

if(UNIX)
   target_link_libraries(main pthread rt)
   target_link_libraries(child1 pthread rt)
   target_link_libraries(child2 pthread rt)
endif()
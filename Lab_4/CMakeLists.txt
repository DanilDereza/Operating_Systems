cmake_minimum_required(VERSION 3.10)
project(SerialLogger)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(${PROJECT_NAME} PRIVATE -D_WIN32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

add_executable(temperature_simulator
    src/temperature_simulator.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(temperature_simulator PRIVATE -D_WIN32)
else()
    target_link_libraries(temperature_simulator PRIVATE pthread)
endif()

file(WRITE ${CMAKE_BINARY_DIR}/last_record.txt "")
file(WRITE ${CMAKE_BINARY_DIR}/log.txt "")
file(WRITE ${CMAKE_BINARY_DIR}/log_hour.txt "")
file(WRITE ${CMAKE_BINARY_DIR}/log_day.txt "")

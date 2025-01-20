cmake_minimum_required(VERSION 3.10)
project(TemperatureMonitor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SQLITE3_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SQLITE3_LIBRARY "${CMAKE_SOURCE_DIR}/lib/libsqlite3.a")
    set(SQLITE3_DLL "${CMAKE_SOURCE_DIR}/lib/sqlite3.dll")
endif()

include_directories(${SQLITE3_INCLUDE_DIR})

set(SOURCE_FILES
    src/main.cpp
)

add_executable(main ${SOURCE_FILES})

add_executable(temperature_simulator
    src/temperature_simulator.cpp
)

file(GLOB STATIC_FILES "${CMAKE_SOURCE_DIR}/*.html" "${CMAKE_SOURCE_DIR}/*.css" "${CMAKE_SOURCE_DIR}/*.js")
foreach(FILE ${STATIC_FILES})
    get_filename_component(FILENAME ${FILE} NAME)
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${FILE} ${CMAKE_BINARY_DIR}/${FILENAME}
        COMMENT "Copying ${FILENAME} to build directory"
    )
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(main PRIVATE ${SQLITE3_LIBRARY} ws2_32)
    install(FILES "${SQLITE3_DLL}" DESTINATION "${CMAKE_BINARY_DIR}") 
else()
    target_link_libraries(main PRIVATE pthread)
    target_link_libraries(main PRIVATE sqlite3)
    target_link_libraries(temperature_simulator PRIVATE pthread)
endif()

file(WRITE ${CMAKE_BINARY_DIR}/last_record.txt "")
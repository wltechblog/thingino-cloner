# Test executables
# All tests link against libcloner and can access its internal headers

enable_testing()

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/libcloner/src
    ${CMAKE_SOURCE_DIR}/libcloner/src/ddr
    ${CMAKE_SOURCE_DIR}/libcloner/src/firmware
    ${CMAKE_SOURCE_DIR}/libcloner/src/flash
)

macro(add_cloner_test name)
    add_executable(${name} ${name}.c)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${name} cloner)
    add_test(NAME ${name} COMMAND ${name}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endmacro()

# DDR tests (dynamic generation, no reference files needed)
add_cloner_test(test_ddr_integration)
add_cloner_test(test_config_database)
add_cloner_test(test_ddr_reference)

# Flash database test
add_cloner_test(test_flash_db)

# Network protocol test
add_cloner_test(test_protocol)

# File operations test
add_cloner_test(test_file_ops)

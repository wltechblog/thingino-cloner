cmake_minimum_required(VERSION 3.10)
project(thingino-cloner VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# Include directories
include_directories(${LIBUSB_INCLUDE_DIRS})
include_directories(include)
include_directories(src/ddr)
include_directories(src/firmware)

# Compiler flags
add_compile_options(-Wall -Wextra -Werror)
add_compile_options(${LIBUSB_CFLAGS_OTHER})

# Source files
set(SOURCES
    src/main.c
    src/usb/manager.c
    src/usb/device.c
    src/usb/protocol.c
    src/firmware/loader.c
    src/firmware/reader.c
    src/firmware/handshake.c
    src/firmware/flash_descriptor.c
    src/ddr/parser.c
    src/ddr/ddr_utils.c
    src/ddr/ddr_controller.c
    src/ddr/ddr_param_builder.c
    src/ddr/ddr_phy.c
    src/ddr/ddr_phy_ddr2.c
    src/ddr/ddr_ctrl_txx.c
    src/ddr/ddr_generator.c
    src/ddr/ddr_binary_builder.c
    src/ddr/ddr_config_database.c
    src/utils.c
    src/bootstrap.c
)

# Firmware database files (auto-generated)
file(GLOB FIRMWARE_SOURCES "src/firmware/firmware_*.c")
list(APPEND SOURCES ${FIRMWARE_SOURCES})

# Create executable
add_executable(thingino-cloner ${SOURCES})

# Link libraries (add zlib for CRC32 in ddr_binary_builder)
target_link_libraries(thingino-cloner ${LIBUSB_LIBRARIES} z)

# Test executable for DDR generator
add_executable(test_ddr_generator
    src/test_ddr_generator.c
    src/ddr/ddr_utils.c
    src/ddr/ddr_controller.c
    src/ddr/ddr_param_builder.c
    src/ddr/ddr_phy.c
    src/ddr/ddr_phy_ddr2.c
    src/ddr/ddr_ctrl_txx.c
    src/ddr/ddr_generator.c
)

# Test executable for multi-chip DDR generation
add_executable(test_ddr_multiplechip
    src/test_ddr_multiplechip.c
    src/ddr/ddr_utils.c
    src/ddr/ddr_controller.c
    src/ddr/ddr_param_builder.c
    src/ddr/ddr_phy.c
    src/ddr/ddr_phy_ddr2.c
    src/ddr/ddr_ctrl_txx.c
    src/ddr/ddr_generator.c
)

# Test executable for reference binary analysis
add_executable(test_ddr_reference_analysis
    src/test_ddr_reference_analysis.c
)

# Test executable for DDR mapping analysis
add_executable(test_ddr_mapping_analysis
    src/test_ddr_mapping_analysis.c
    src/ddr/ddr_utils.c
    src/ddr/ddr_controller.c
    src/ddr/ddr_param_builder.c
    src/ddr/ddr_phy.c
    src/ddr/ddr_phy_ddr2.c
    src/ddr/ddr_ctrl_txx.c
    src/ddr/ddr_generator.c
)

# Test executable for TXX mapping analysis
add_executable(test_txx_mapping_analysis
    src/test_txx_mapping_analysis.c
)

# Test executable for DDRC analysis
add_executable(test_ddrc_analysis
    src/test_ddrc_analysis.c
)

# Test executable for new binary builder
add_executable(test_binary_builder
    src/test_binary_builder.c
    src/ddr/ddr_binary_builder.c
    src/ddr/ddr_config_database.c
)
target_link_libraries(test_binary_builder z)

# Test executable for DDR integration
add_executable(test_ddr_integration
    src/test_ddr_integration.c
    src/ddr/ddr_binary_builder.c
    src/ddr/ddr_config_database.c
)
target_link_libraries(test_ddr_integration z)

# Test executable for configuration database
add_executable(test_config_database
    src/test_config_database.c
    src/ddr/ddr_binary_builder.c
    src/ddr/ddr_config_database.c
)
target_link_libraries(test_config_database z)

# Test firmware database
add_executable(test_firmware_database
    src/test_firmware_database.c
    ${FIRMWARE_SOURCES}
)

# Installation
install(TARGETS thingino-cloner DESTINATION bin)

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    target_compile_definitions(thingino-cloner PRIVATE _WIN32)
elseif(APPLE)
    # macOS-specific settings
    target_compile_definitions(thingino-cloner PRIVATE __APPLE__)
else()
    # Linux-specific settings
    target_compile_definitions(thingino-cloner PRIVATE __linux__)
endif()
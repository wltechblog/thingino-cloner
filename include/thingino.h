#ifndef THINGINO_H
#define THINGINO_H

#include <stdint.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// GLOBAL DEBUG FLAG
// ============================================================================

extern bool g_debug_enabled;

// Debug logging macro - only prints if debug is enabled
#define DEBUG_PRINT(fmt, ...) \
    do { \
        if (g_debug_enabled) { \
            printf("[DEBUG] " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Forward declare firmware_binary_t (defined in firmware/firmware_database.h)
typedef struct {
    const char *processor;
    const uint8_t *spl_data;
    size_t spl_size;
    const uint8_t *uboot_data;
    size_t uboot_size;
} firmware_binary_t;

// ============================================================================
// CONSTANTS
// ============================================================================

// USB Vendor IDs and Product IDs for Ingenic devices
#define VENDOR_ID_INGENIC      0x601A  // Primary vendor ID for most Ingenic devices
#define VENDOR_ID_INGENIC_ALT  0xA108  // Alternative vendor ID for some models
#define PRODUCT_ID_BOOTROM      0x4770  // T20/T21
#define PRODUCT_ID_BOOTROM2    0xC309  // T30/T31/T40 series bootrom
#define PRODUCT_ID_BOOTROM3     0x601A  // Alternative bootrom ID
#define PRODUCT_ID_FIRMWARE     0x8887  // Common firmware ID
#define PRODUCT_ID_FIRMWARE2    0x601E  // Alternative firmware ID

// Command codes - Bootrom stage (0x00-0x05)
#define VR_GET_CPU_INFO        0x00
#define VR_SET_DATA_ADDR       0x01
#define VR_SET_DATA_LEN        0x02
#define VR_FLUSH_CACHE         0x03
#define VR_PROG_STAGE1        0x04
#define VR_PROG_STAGE2        0x05

// Firmware stage commands (0x10-0x26)
#define VR_FW_READ            0x10
#define VR_FW_HANDSHAKE       0x11
#define VR_FW_WRITE1          0x13
#define VR_FW_WRITE2          0x14
#define VR_FW_READ_STATUS1    0x16
#define VR_FW_READ_STATUS2    0x19
#define VR_FW_READ_STATUS3    0x25
#define VR_FW_READ_STATUS4    0x26

// Traditional firmware operations
#define VR_WRITE              0x12
#define VR_READ               0x13

// NAND operations (available in bootloader)
#define VR_NAND_OPS           0x07
#define NAND_OPERATION_READ   0x05  // NAND read subcommand
#define NAND_OPERATION_WRITE  0x06  // NAND write subcommand

// USB Configuration constants
#define DEFAULT_BUFFER_SIZE    (1024 * 1024)  // 1MB default buffer
#define REQUEST_TYPE_VENDOR    0xC0           // USB vendor request type for device-to-host
#define REQUEST_TYPE_OUT       0x40           // USB vendor request type for host-to-device

// Bootstrap constants
#define BOOTLOADER_ADDRESS_SDRAM   0x80000000
#define BOOTSTRAP_TIMEOUT_SECONDS   30
#define BOOTSTRAP_POLL_INTERVAL_MS  500
#define CRC32_POLYNOMIAL           0xEDB88320
#define CRC32_INITIAL              0xFFFFFFFF

// Endpoints
#define ENDPOINT_IN   0x81  // Bulk IN
#define ENDPOINT_OUT  0x01  // Bulk OUT
#define ENDPOINT_INT_IN   0x80  // Interrupt IN (EP 0x00 with IN direction)
#define ENDPOINT_INT_OUT  0x00  // Interrupt OUT (EP 0x00 with OUT direction)

// Error codes
#define ACK_SUCCESS    0x00
#define ACK_ERROR      0x01

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

// Processor variants
typedef enum {
    VARIANT_T20,
    VARIANT_T21,
    VARIANT_T23,
    VARIANT_T30,
    VARIANT_T31,
    VARIANT_T31X,
    VARIANT_T31ZX,
    VARIANT_T40,
    VARIANT_T41,
    VARIANT_X1000,
    VARIANT_X1600,
    VARIANT_X1700,
    VARIANT_X2000,
    VARIANT_X2100,
    VARIANT_X2600
} processor_variant_t;

// Device stages
typedef enum {
    STAGE_BOOTROM,
    STAGE_FIRMWARE
} device_stage_t;

// Error codes
typedef enum {
    THINGINO_SUCCESS = 0,
    THINGINO_ERROR_INIT_FAILED = -1,
    THINGINO_ERROR_DEVICE_NOT_FOUND = -2,
    THINGINO_ERROR_OPEN_FAILED = -3,
    THINGINO_ERROR_TRANSFER_FAILED = -4,
    THINGINO_ERROR_TIMEOUT = -5,
    THINGINO_ERROR_INVALID_PARAMETER = -6,
    THINGINO_ERROR_MEMORY = -7,
    THINGINO_ERROR_FILE_IO = -8,
    THINGINO_ERROR_PROTOCOL = -9,
    THINGINO_ERROR_TRANSFER_TIMEOUT = -10
} thingino_error_t;

// Device information structure
typedef struct {
    uint8_t bus;
    uint8_t address;
    uint16_t vendor;
    uint16_t product;
    device_stage_t stage;
    processor_variant_t variant;
} device_info_t;

// CPU information structure
typedef struct {
    uint8_t magic[8];     // "BOOT47XX" or similar
    uint8_t unknown[8];    // Additional info
    char clean_magic[9];   // Clean ASCII string for variant detection
    device_stage_t stage;
} cpu_info_t;

// Write command structure
typedef struct {
    uint32_t partition;
    uint32_t offset;
    uint32_t length;
    uint32_t crc32;
} write_command_t;

// Read command structure
typedef struct {
    uint32_t partition;
    uint32_t offset;
    uint32_t length;
} read_command_t;

// Flash memory bank structure
typedef struct {
    uint32_t offset;
    uint32_t size;
    char label[16];
    bool enabled;
} flash_bank_t;

// Firmware read configuration
typedef struct {
    uint32_t total_size;
    int bank_count;
    flash_bank_t* banks;
    uint32_t block_size;
} firmware_read_config_t;

// Firmware files structure
typedef struct {
    uint8_t* config;
    size_t config_size;
    uint8_t* spl;
    size_t spl_size;
    uint8_t* uboot;
    size_t uboot_size;
} firmware_files_t;

// Bootstrap configuration
typedef struct {
    uint32_t sdram_address;
    int timeout;
    bool verbose;
    bool skip_ddr;
    const char* config_file;  // Custom DDR config file path (NULL = use default)
    const char* spl_file;     // Custom SPL file path (NULL = use default)
    const char* uboot_file;   // Custom U-Boot file path (NULL = use default)
} bootstrap_config_t;

// Bootstrap progress
typedef struct {
    char stage[32];
    int current;
    int total;
    char description[128];
} bootstrap_progress_t;

// USB device structure
typedef struct {
    libusb_device_handle* handle;
    libusb_context* context;
    libusb_device* device;
    device_info_t info;
    bool closed;
} usb_device_t;

// USB manager structure
typedef struct {
    libusb_context* context;
    bool initialized;
} usb_manager_t;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Manager functions
thingino_error_t usb_manager_init(usb_manager_t* manager);
thingino_error_t usb_manager_find_devices(usb_manager_t* manager, device_info_t** devices, int* count);
thingino_error_t usb_manager_find_devices_fast(usb_manager_t* manager, device_info_t** devices, int* count);
thingino_error_t usb_manager_open_device(usb_manager_t* manager, const device_info_t* info, usb_device_t** device);
void usb_manager_cleanup(usb_manager_t* manager);

// Device functions
thingino_error_t usb_device_init(usb_device_t* device, uint8_t bus, uint8_t address);
thingino_error_t usb_device_close(usb_device_t* device);
	thingino_error_t usb_device_reopen(usb_device_t* device);

thingino_error_t usb_device_reset(usb_device_t* device);
thingino_error_t usb_device_claim_interface(usb_device_t* device);
thingino_error_t usb_device_release_interface(usb_device_t* device);
thingino_error_t usb_device_get_cpu_info(usb_device_t* device, cpu_info_t* info);

// Transfer functions
thingino_error_t usb_device_control_transfer(usb_device_t* device, uint8_t request_type,
    uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length, int* transferred);
thingino_error_t usb_device_bulk_transfer(usb_device_t* device, uint8_t endpoint,
    uint8_t* data, int length, int* transferred, int timeout);
thingino_error_t usb_device_interrupt_transfer(usb_device_t* device, uint8_t endpoint,
    uint8_t* data, int length, int* transferred, int timeout);
thingino_error_t usb_device_vendor_request(usb_device_t* device, uint8_t request_type,
    uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length, uint8_t* response, int* response_length);

// Protocol functions
thingino_error_t protocol_set_data_address(usb_device_t* device, uint32_t addr);
thingino_error_t protocol_set_data_length(usb_device_t* device, uint32_t length);
thingino_error_t protocol_flush_cache(usb_device_t* device);
thingino_error_t protocol_read_status(usb_device_t* device, uint8_t* status_buffer,
                                     int buffer_size, int* status_len);
thingino_error_t protocol_prog_stage1(usb_device_t* device, uint32_t addr);
thingino_error_t protocol_prog_stage2(usb_device_t* device, uint32_t addr);
thingino_error_t protocol_get_ack(usb_device_t* device, int32_t* status);
thingino_error_t protocol_init(usb_device_t* device);
thingino_error_t protocol_nand_read(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data, int* transferred);

// Firmware functions
thingino_error_t firmware_load(processor_variant_t variant, firmware_files_t* firmware);
void firmware_cleanup(firmware_files_t* firmware);
thingino_error_t firmware_load_t20(firmware_files_t* firmware);
thingino_error_t firmware_load_t31x(firmware_files_t* firmware);
thingino_error_t load_file(const char* filename, uint8_t** data, size_t* size);
thingino_error_t firmware_load_from_files(processor_variant_t variant, const char* config_file, const char* spl_file, const char* uboot_file, firmware_files_t* firmware);
thingino_error_t firmware_validate(const firmware_files_t* firmware);

// DDR functions
thingino_error_t ddr_parse_config(const char* config_text, uint8_t** binary, size_t* size);
thingino_error_t ddr_parse_config_bytes(const char* config_text, uint8_t** binary, size_t* size);
thingino_error_t ddr_validate_binary(const uint8_t* data, size_t size);
thingino_error_t load_extracted_binary(void);
thingino_error_t create_minimal_ddr_binary(void);
thingino_error_t ddr_parse_text_to_binary(const char* config_text, uint8_t** binary, size_t* size);
void ddr_cleanup(void);
void ddr_print_info(const uint8_t* data, size_t size);

// Bootstrap functions
thingino_error_t bootstrap_device(usb_device_t* device, const bootstrap_config_t* config);
thingino_error_t bootstrap_ensure_bootstrapped(usb_device_t* device, const bootstrap_config_t* config);
thingino_error_t bootstrap_load_data_to_memory(usb_device_t* device, const uint8_t* data, size_t size, uint32_t address);
thingino_error_t bootstrap_program_stage2(usb_device_t* device, const uint8_t* data, size_t size);
thingino_error_t bootstrap_transfer_data(usb_device_t* device, const uint8_t* data, size_t size);

// Additional protocol functions
thingino_error_t protocol_fw_read(usb_device_t* device, int data_len, uint8_t** data, int* actual_len);
thingino_error_t protocol_fw_handshake(usb_device_t* device);
thingino_error_t protocol_fw_write_chunk1(usb_device_t* device, const uint8_t* data);
thingino_error_t protocol_fw_write_chunk2(usb_device_t* device, const uint8_t* data);
thingino_error_t protocol_traditional_read(usb_device_t* device, int data_len, uint8_t** data, int* actual_len);
thingino_error_t protocol_fw_read_operation(usb_device_t* device, uint32_t offset, uint32_t length, uint8_t** data, int* actual_len);
thingino_error_t protocol_fw_read_status(usb_device_t* device, int status_cmd, uint32_t* status);
thingino_error_t protocol_vendor_style_read(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data, int* actual_len);

// Proper bootloader protocol functions (using code execution pattern)
thingino_error_t protocol_load_and_execute_code(usb_device_t* device, uint32_t ram_address,
                                                 const uint8_t* code, uint32_t code_size);
thingino_error_t protocol_proper_firmware_read(usb_device_t* device, uint32_t flash_offset,
                                               uint32_t read_size, uint8_t** out_data, int* out_len);
thingino_error_t protocol_proper_firmware_write(usb_device_t* device, uint32_t flash_offset,
                                                const uint8_t* data, uint32_t data_size);

// Firmware read functions
thingino_error_t firmware_read_detect_size(usb_device_t* device, uint32_t* size);
thingino_error_t firmware_read_init(usb_device_t* device, firmware_read_config_t* config);
thingino_error_t firmware_read_bank(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data);
thingino_error_t firmware_read_full(usb_device_t* device, uint8_t** data, uint32_t* size);
thingino_error_t firmware_read_cleanup(firmware_read_config_t* config);

// Firmware handshake protocol functions (40-byte chunk transfers)
thingino_error_t firmware_handshake_read_chunk(usb_device_t* device, uint32_t chunk_index,
                                               uint32_t chunk_offset, uint32_t chunk_size,
                                               uint8_t** out_data, int* out_len);
thingino_error_t firmware_handshake_write_chunk(usb_device_t* device, uint32_t chunk_index,
                                                uint32_t chunk_offset, const uint8_t* data,
                                                uint32_t data_size);
thingino_error_t firmware_handshake_write_chunk_a1(usb_device_t* device, uint32_t chunk_index,
                                                   uint32_t chunk_offset, const uint8_t* data,
                                                   uint32_t data_size);
thingino_error_t firmware_handshake_init(usb_device_t* device);

// Firmware writer functions
thingino_error_t write_firmware_to_device(usb_device_t* device,
                                         const char* firmware_file,
                                         const firmware_binary_t* fw_binary,
                                         bool force_erase,
                                         bool is_a1_board);
thingino_error_t send_bulk_data(usb_device_t* device, uint8_t endpoint,
                                const uint8_t* data, uint32_t size);

// Utility functions (additional)
processor_variant_t detect_variant_from_magic(const char* magic);

// Bootstrap functions
thingino_error_t bootstrap_device(usb_device_t* device, const bootstrap_config_t* config);
thingino_error_t bootstrap_ensure_bootstrapped(usb_device_t* device, const bootstrap_config_t* config);

// Utility functions
uint32_t calculate_crc32(const uint8_t* data, size_t length);
const char* processor_variant_to_string(processor_variant_t variant);
const char* device_stage_to_string(device_stage_t stage);
const char* thingino_error_to_string(thingino_error_t error);

#endif // THINGINO_H
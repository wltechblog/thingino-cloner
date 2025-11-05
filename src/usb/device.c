#include "thingino.h"

#ifndef _WIN32
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#endif

thingino_error_t usb_device_get_cpu_info(usb_device_t* device, cpu_info_t* info) {
    if (!device || !info || device->closed) {
        DEBUG_PRINT("GetCPUInfo: Invalid parameters or device closed\n");
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("GetCPUInfo: Starting CPU info request (VID:0x%04X, PID:0x%04X)\n",
        device->info.vendor, device->info.product);
    
    uint8_t data[8] = {0};
    int transferred;
    DEBUG_PRINT("GetCPUInfo: Sending vendor request VR_GET_CPU_INFO (0x%02X)\n", VR_GET_CPU_INFO);
    
    // Direct control transfer without claiming interface first (like Go version)
    int result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR,
        VR_GET_CPU_INFO, 0, 0, data, 8, 5000);
    
    if (result < 0) {
        DEBUG_PRINT("GetCPUInfo: Direct control transfer failed: %d (%s), trying with interface claim\n", 
            result, libusb_error_name(result));
        
        // Fall back to interface claiming approach
        thingino_error_t claim_result = usb_device_claim_interface(device);
        if (claim_result != THINGINO_SUCCESS) {
            DEBUG_PRINT("GetCPUInfo: Failed to claim interface: %s\n", thingino_error_to_string(claim_result));
            return claim_result;
        }
        
        result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
            VR_GET_CPU_INFO, 0, 0, NULL, 0, data, &transferred);
        
        // Release interface after communication
        usb_device_release_interface(device);
        
        if (result != THINGINO_SUCCESS) {
            DEBUG_PRINT("GetCPUInfo: Vendor request failed: %s\n", thingino_error_to_string(result));
            return result;
        }
    } else {
        transferred = result;
        DEBUG_PRINT("GetCPUInfo: Direct control transfer succeeded: %d bytes\n", transferred);
    }
    
    if (transferred < 8) {
        DEBUG_PRINT("GetCPUInfo: Invalid response length: %d (expected 8)\n", transferred);
        return THINGINO_ERROR_PROTOCOL;
    }
    
    DEBUG_PRINT("GetCPUInfo: Got %d bytes of response data\n", transferred);
    
    // Copy magic bytes
    memcpy(info->magic, data, 8);
    
    // Clean CPU string - extract only valid ASCII characters
    char cpu_str[9] = {0};
    int write_pos = 0;
    for (int read_pos = 0; read_pos < 8; read_pos++) {
        uint8_t byte = data[read_pos];
        if (byte >= 0x20 && byte <= 0x7E) {  // Printable ASCII including space
            if (byte != ' ') {  // Skip spaces
                cpu_str[write_pos++] = byte;
            }
        }
    }
    cpu_str[write_pos] = '\0';
    
    // Also create a clean version for variant detection (preserve spaces for pattern matching)
    char clean_cpu_str[9] = {0};
    write_pos = 0;
    for (int read_pos = 0; read_pos < 8; read_pos++) {
        uint8_t byte = data[read_pos];
        if (byte >= 0x20 && byte <= 0x7E) {  // Printable ASCII including space
            clean_cpu_str[write_pos++] = byte;
        }
    }
    clean_cpu_str[write_pos] = '\0';
    
    DEBUG_PRINT("GetCPUInfo: CPU magic bytes: ");
    for (int i = 0; i < 8; i++) {
        DEBUG_PRINT("0x%02X ", data[i]);
    }
    DEBUG_PRINT("-> string = '%s' -> clean = '%s'\n", cpu_str, clean_cpu_str);
    
    // Store clean string in cpu_info for variant detection
    strcpy(info->clean_magic, clean_cpu_str);
    
    // Determine boot stage based on CPU info
    // Check for "BOOT" prefix which indicates firmware stage
    if (strncmp(cpu_str, "Boot", 4) == 0 || strncmp(cpu_str, "BOOT", 4) == 0) {
        info->stage = STAGE_FIRMWARE;
        if (device->info.stage == STAGE_BOOTROM) {
            DEBUG_PRINT("GetCPUInfo: Device is in firmware stage\n");
        }
    } else {
        info->stage = STAGE_BOOTROM;
        if (device->info.stage == STAGE_BOOTROM) {
            DEBUG_PRINT("GetCPUInfo: Device is in bootrom stage\n");
        }
    }
    
    return THINGINO_SUCCESS;
}

// Initialize USB device
thingino_error_t usb_device_init(usb_device_t* device, uint8_t bus, uint8_t address) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Find the device by bus and address
    libusb_device** devices;
    ssize_t count = libusb_get_device_list(NULL, &devices);
    if (count < 0) {
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    
    libusb_device* found_device = NULL;
    for (ssize_t i = 0; i < count; i++) {
        uint8_t dev_bus = libusb_get_bus_number(devices[i]);
        uint8_t dev_address = libusb_get_device_address(devices[i]);
        
        if (dev_bus == bus && dev_address == address) {
            found_device = devices[i];
            break;
        }
    }
    
    if (!found_device) {
        libusb_free_device_list(devices, 1);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    
    // Open the device
    int result = libusb_open(found_device, &device->handle);
    if (result != LIBUSB_SUCCESS) {
        libusb_free_device_list(devices, 1);
        return THINGINO_ERROR_OPEN_FAILED;
    }
    
    // Get device descriptor
    struct libusb_device_descriptor desc;
    result = libusb_get_device_descriptor(found_device, &desc);
    if (result != LIBUSB_SUCCESS) {
        libusb_close(device->handle);
        libusb_free_device_list(devices, 1);
        return THINGINO_ERROR_OPEN_FAILED;
    }
    
    // Initialize device structure
    device->device = found_device;
    // Preserve context if already set by manager, otherwise set to NULL
    // (context is set before usb_device_init is called by the manager)
    // DEBUG_PRINT("usb_device_init: context before init = %p\n", device->context);
    device->closed = false;
    device->info.bus = bus;
    device->info.address = address;
    device->info.vendor = desc.idVendor;
    device->info.product = desc.idProduct;
    device->info.stage = STAGE_BOOTROM;  // Default to bootrom
    // Don't set default variant - preserve whatever was set by manager
    DEBUG_PRINT("usb_device_init: preserving variant %d, context=%p\n", device->info.variant, device->context);
    
    libusb_free_device_list(devices, 1);
    
    DEBUG_PRINT("Device initialized: VID:0x%04X, PID:0x%04X, Bus:%d, Addr:%d\n",
        device->info.vendor, device->info.product, bus, address);
    
    return THINGINO_SUCCESS;
}

// Close USB device
thingino_error_t usb_device_close(usb_device_t* device) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    if (!device->closed && device->handle) {
        libusb_close(device->handle);
        device->handle = NULL;
    }
    
    device->closed = true;
    return THINGINO_SUCCESS;
}

// Reset USB device
thingino_error_t usb_device_reset(usb_device_t* device) {
    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    int result = libusb_reset_device(device->handle);
    if (result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Reset device failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    
    return THINGINO_SUCCESS;
}

// Claim USB interface
thingino_error_t usb_device_claim_interface(usb_device_t* device) {
    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    int result = libusb_claim_interface(device->handle, 0);
    if (result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Claim interface failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    
    return THINGINO_SUCCESS;
}

// Release USB interface
thingino_error_t usb_device_release_interface(usb_device_t* device) {
    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    int result = libusb_release_interface(device->handle, 0);
    if (result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Release interface failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    
    return THINGINO_SUCCESS;
}

// Control transfer
thingino_error_t usb_device_control_transfer(usb_device_t* device, uint8_t request_type,
    uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length, int* transferred) {
    
    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    int result = libusb_control_transfer(device->handle, request_type, request, value, index, data, length, 5000);
    
    if (result < 0) {
        DEBUG_PRINT("Control transfer failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    
    if (transferred) {
        *transferred = result;
    }
    
    return THINGINO_SUCCESS;
}

// Helper to get current time in milliseconds
#ifndef _WIN32
#endif

// Async transfer removed - protocol requires direct, synchronous transfers per trace file

// Direct ioctl removed - protocol requires synchronous libusb transfers per trace file

// Bulk transfer with timeout parameter
// According to the trace file, protocol requires successful transfer
// Fail immediately if transfer doesn't succeed
thingino_error_t usb_device_bulk_transfer(usb_device_t* device, uint8_t endpoint,
    uint8_t* data, int length, int* transferred, int timeout) {

    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Determine direction from endpoint (bit 7: 0=OUT, 1=IN)
    const char* direction = (endpoint & 0x80) ? "read" : "write";

    DEBUG_PRINT("Bulk transfer: %s %d bytes, timeout=%dms, endpoint=0x%02X\n",
        direction, length, timeout, endpoint);

    // Use libusb for bulk transfer
    int result = libusb_bulk_transfer(device->handle, endpoint, data, length, transferred, timeout);

    if (result == LIBUSB_SUCCESS) {
        DEBUG_PRINT("Bulk transfer success: %d bytes transferred\n", *transferred);
        return THINGINO_SUCCESS;
    }
    
    printf("[ERROR] Bulk transfer failed: %s (endpoint=0x%02X, length=%d, timeout=%dms)\n",
        libusb_error_name(result), endpoint, length, timeout);
    return THINGINO_ERROR_TRANSFER_FAILED;
}

// Interrupt transfer with timeout parameter
// Used for INT endpoint communication (e.g., EP 0x00 handshaking)
thingino_error_t usb_device_interrupt_transfer(usb_device_t* device, uint8_t endpoint,
    uint8_t* data, int length, int* transferred, int timeout) {

    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Determine direction from endpoint (bit 7: 0=OUT, 1=IN)
    const char* direction = (endpoint & 0x80) ? "read" : "write";

    DEBUG_PRINT("Interrupt transfer: %s %d bytes, timeout=%dms, endpoint=0x%02X\n",
        direction, length, timeout, endpoint);

    // Use libusb interrupt transfer
    int result = libusb_interrupt_transfer(device->handle, endpoint, data, length, 
                                          transferred, timeout);
    
    if (result == LIBUSB_SUCCESS) {
        DEBUG_PRINT("Interrupt transfer success (%s): %d bytes transferred\n",
            direction, *transferred);
        return THINGINO_SUCCESS;
    }
    
    if (result == LIBUSB_ERROR_TIMEOUT) {
        DEBUG_PRINT("Interrupt transfer timeout (%s): endpoint=0x%02X, length=%d, timeout=%dms\n",
            direction, endpoint, length, timeout);
        return THINGINO_ERROR_TIMEOUT;
    }
    
    DEBUG_PRINT("Interrupt transfer failed (%s): %s (endpoint=0x%02X, length=%d, timeout=%dms)\n",
        direction, libusb_error_name(result), endpoint, length, timeout);
    return THINGINO_ERROR_TRANSFER_FAILED;
}

// Vendor request with retry logic for device re-enumeration
thingino_error_t usb_device_vendor_request(usb_device_t* device, uint8_t request_type,
    uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length, uint8_t* response, int* response_length) {
    
    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Retry logic for device re-enumeration issues
    int max_retries = 5;
    int retry_count = 0;
    int retry_delays[] = {500000, 1000000, 2000000, 3000000, 5000000};  // microseconds: 0.5s, 1s, 2s, 3s, 5s
    
    while (retry_count < max_retries) {
        int result = libusb_control_transfer(device->handle, request_type, request, value, index, 
            response ? response : data, response ? 8 : length, 5000);
        
        if (result >= 0) {
            // Success
            if (response_length) {
                *response_length = result;
            }
            return THINGINO_SUCCESS;
        }
        
        // Check if this is a timeout or pipe error (device disconnected)
        if (result == LIBUSB_ERROR_TIMEOUT || result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_NO_DEVICE) {
            retry_count++;
            if (retry_count < max_retries) {
                DEBUG_PRINT("Vendor request failed with %s, retrying in %d ms (attempt %d/%d)...\n", 
                    libusb_error_name(result), retry_delays[retry_count-1]/1000, retry_count, max_retries);
#ifdef _WIN32
                Sleep(retry_delays[retry_count-1] / 1000);
#else
                usleep(retry_delays[retry_count-1]);
#endif
            } else {
                DEBUG_PRINT("Vendor request failed after %d retries: %s\n", max_retries, libusb_error_name(result));
                return THINGINO_ERROR_TRANSFER_FAILED;
            }
        } else {
            // Non-recoverable error
            DEBUG_PRINT("Vendor request failed: %s\n", libusb_error_name(result));
            return THINGINO_ERROR_TRANSFER_FAILED;
        }
    }
    
    return THINGINO_ERROR_TRANSFER_FAILED;
}
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

    // Cross-check the actual handle's bus/address
    libusb_device* __dev = libusb_get_device(device->handle);
    if (__dev) {
        int __bus = libusb_get_bus_number(__dev);
        int __addr = libusb_get_device_address(__dev);
        DEBUG_PRINT("GetCPUInfo: Using handle at bus=%d addr=%d (manager bus=%d addr=%d)\n",
            __bus, __addr, device->info.bus, device->info.address);
    }

    uint8_t data[16] = {0};
    int transferred = 0;
    DEBUG_PRINT("GetCPUInfo: Sending vendor request VR_GET_CPU_INFO (0x%02X)\n", VR_GET_CPU_INFO);

    // Try fast, non-blocking attempts first to avoid long retry loops
    // Request up to 16 bytes; many ROMs return 8, some return 16
    int result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR,
        VR_GET_CPU_INFO, 0, 0, data, 16, 1200);

    if (result < 0) {
        DEBUG_PRINT("GetCPUInfo: Device-recipient (0xC0) failed: %d (%s), trying interface recipient (0xC1) without claim\n",
            result, libusb_error_name(result));

        // Immediate recipient=interface attempt without claiming
        // Request up to 16 bytes here as well
        result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR_IF,
            VR_GET_CPU_INFO, 0, 0, data, 16, 1200);
    }

    bool claimed = false;
    if (result < 0) {
        DEBUG_PRINT("GetCPUInfo: Interface-recipient without claim failed: %d (%s), trying after claiming interface 0\n",
            result, libusb_error_name(result));

        // Claim interface 0 and retry quickly (still no long internal retries)
        thingino_error_t claim_result = usb_device_claim_interface(device);
        if (claim_result == THINGINO_SUCCESS) {
            claimed = true;
            // Prefer interface-recipient when interface is claimed
            int r_if = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR_IF,
                VR_GET_CPU_INFO, 0, 0, data, 16, 1500);
            if (r_if >= 0) {
                result = r_if;
            } else {
                DEBUG_PRINT("GetCPUInfo: Claimed + interface-recipient failed: %d (%s), trying device-recipient once more\n",
                    r_if, libusb_error_name(r_if));
                int r_dev = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR,
                    VR_GET_CPU_INFO, 0, 0, data, 16, 1500);
                result = r_dev;
            }
        } else {
            DEBUG_PRINT("GetCPUInfo: Failed to claim interface: %s\n", thingino_error_to_string(claim_result));
        }
    }

    if (claimed) {
        usb_device_release_interface(device);
    }

    if (result < 0) {
        DEBUG_PRINT("GetCPUInfo: All quick attempts failed: %d (%s)\n", result, libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    transferred = result;
    DEBUG_PRINT("GetCPUInfo: Control transfer succeeded: %d bytes\n", transferred);

    if (transferred < 8) {
        DEBUG_PRINT("GetCPUInfo: Invalid response length: %d (expected 8)\n", transferred);
        return THINGINO_ERROR_PROTOCOL;
    }

    DEBUG_PRINT("GetCPUInfo: Got %d bytes of response data\n", transferred);

    // Copy magic bytes (first 8) and optional extra bytes if present
    memcpy(info->magic, data, 8);
    if (transferred >= 16) {
        memcpy(info->unknown, data + 8, 8);
    } else {
        memset(info->unknown, 0, 8);
    }

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
    if (transferred >= 16) {
        DEBUG_PRINT(" | extra: ");
        for (int i = 8; i < 16; i++) {
            DEBUG_PRINT("0x%02X ", data[i]);
        }
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

// Quick, low-timeout CPU info query for fast polling (post-SPL on T41)
thingino_error_t usb_device_get_cpu_info_quick(usb_device_t* device, cpu_info_t* info) {
    if (!device || !info || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    uint8_t data[16] = {0};
    // Minimal single control transfer with short timeout; try device then interface recipient
    int result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR,
        VR_GET_CPU_INFO, 0, 0, data, 16, 50); // ~50ms timeout; request up to 16 bytes

    if (result < 0) {
        // Try interface-recipient quickly without claiming
        result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR_IF,
            VR_GET_CPU_INFO, 0, 0, data, 16, 50);
        if (result < 0) {
            return THINGINO_ERROR_TRANSFER_FAILED;
        }
    }
    if (result < 8) {
        return THINGINO_ERROR_PROTOCOL;
    }

    // Copy magic and optional extra bytes
    memcpy(info->magic, data, 8);
    if (result >= 16) {
        memcpy(info->unknown, data + 8, 8);
    } else {
        memset(info->unknown, 0, 8);
    }

    // Build cpu_str without spaces for stage detection
    char cpu_str[9] = {0};
    int write_pos = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t byte = data[i];
        if (byte >= 0x20 && byte <= 0x7E) {
            if (byte != ' ') cpu_str[write_pos++] = byte;
        }
    }
    cpu_str[write_pos] = '\0';

    // Also store a clean version with spaces preserved (for variant detection if needed)
    char clean_cpu_str[9] = {0};
    write_pos = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t byte = data[i];
        if (byte >= 0x20 && byte <= 0x7E) {
            clean_cpu_str[write_pos++] = byte;
        }
    }
    clean_cpu_str[write_pos] = '\0';
    strcpy(info->clean_magic, clean_cpu_str);

    // Determine stage
    if (strncmp(cpu_str, "Boot", 4) == 0 || strncmp(cpu_str, "BOOT", 4) == 0) {
        info->stage = STAGE_FIRMWARE;
    } else {
        info->stage = STAGE_BOOTROM;
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
    // Use the device's libusb context instead of the default one
    ssize_t count = libusb_get_device_list(device->context, &devices);
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

    // Detach any active kernel driver on interface 0 (common on Linux)
    int active = libusb_kernel_driver_active(device->handle, 0);
    if (active == 1) {
        int detach_res = libusb_detach_kernel_driver(device->handle, 0);
        if (detach_res != LIBUSB_SUCCESS) {
            DEBUG_PRINT("Detach kernel driver failed: %s\n", libusb_error_name(detach_res));
            // Continue anyway; control transfers generally don't require claimed interface
        } else {
            DEBUG_PRINT("Detached kernel driver from interface 0\n");
        }
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



// Set interface alternate setting (post-SPL handshakes sometimes require alt=1)
thingino_error_t usb_device_set_interface_alt_setting(usb_device_t* device, int interface_number, int alt_setting) {
    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    DEBUG_PRINT("Set alt setting: if=%d alt=%d\n", interface_number, alt_setting);
    int r = libusb_set_interface_alt_setting(device->handle, interface_number, alt_setting);
    if (r != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Set alt setting failed: %s\n", libusb_error_name(r));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    return THINGINO_SUCCESS;
}

// Get the first interface number from the active configuration
thingino_error_t usb_device_get_first_interface_number(usb_device_t* device, int* interface_number) {
    if (!device || !device->device || !interface_number) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    struct libusb_config_descriptor* config = NULL;
    int r = libusb_get_active_config_descriptor(device->device, &config);
    if (r != LIBUSB_SUCCESS || !config) {
        DEBUG_PRINT("get_active_config_descriptor failed: %s\n", libusb_error_name(r));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    if (config->bNumInterfaces == 0) {
        libusb_free_config_descriptor(config);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    const struct libusb_interface* iface = &config->interface[0];
    const struct libusb_interface_descriptor* alt0 = (iface->num_altsetting > 0) ? &iface->altsetting[0] : NULL;
    if (!alt0) {
        libusb_free_config_descriptor(config);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    *interface_number = alt0->bInterfaceNumber;
    libusb_free_config_descriptor(config);
    return THINGINO_SUCCESS;
}

// Dump active configuration (interfaces, alt settings, endpoints) for debugging
void usb_device_dump_active_config(usb_device_t* device, bool verbose) {
    if (!device || !device->device) return;
    struct libusb_config_descriptor* config = NULL;
    int r = libusb_get_active_config_descriptor(device->device, &config);
    if (r != LIBUSB_SUCCESS || !config) {
        DEBUG_PRINT("dump_active_config: get_active_config_descriptor failed: %s\n", libusb_error_name(r));
        return;
    }
    printf("USB active config: %u interface(s)\n", config->bNumInterfaces);
    for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
        const struct libusb_interface* iface = &config->interface[i];
        printf("  Interface[%u]: %d altsetting(s)\n", i, iface->num_altsetting);
        for (int a = 0; a < iface->num_altsetting; ++a) {
            const struct libusb_interface_descriptor* d = &iface->altsetting[a];
            printf("    Alt[%d]: bInterfaceNumber=%u bAlternateSetting=%u bNumEndpoints=%u\n",
                   a, d->bInterfaceNumber, d->bAlternateSetting, d->bNumEndpoints);
            if (verbose) {
                for (uint8_t e = 0; e < d->bNumEndpoints; ++e) {
                    const struct libusb_endpoint_descriptor* ep = &d->endpoint[e];
                    printf("      EP[%u]: addr=0x%02X attr=0x%02X maxpkt=%u\n",
                           e, ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize);
                }
            }
        }
    }
    libusb_free_config_descriptor(config);
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

// Strict single-attempt vendor request (no recipient switching, no retries)
thingino_error_t usb_device_vendor_request_strict(usb_device_t* device, uint8_t request_type,
    uint8_t request, uint16_t value, uint16_t index, uint8_t* data, uint16_t length, uint8_t* response, int* response_length) {

    if (!device || !device->handle || device->closed) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Adaptive timeout similar to the regular path, but send exactly one URB
    int timeout_ms = 5000;
    if (request == VR_SET_DATA_ADDR || request == VR_SET_DATA_LEN ||
        request == VR_PROG_STAGE1   || request == VR_PROG_STAGE2) {
        timeout_ms = 12000;
    }

    int result = libusb_control_transfer(device->handle, request_type, request, value, index,
        response ? response : data, response ? 8 : length, timeout_ms);

    if (result >= 0) {
        if (response_length) {
            *response_length = result;
        }
        return THINGINO_SUCCESS;
    }

    DEBUG_PRINT("Vendor request STRICT failed: %s (bmReqType=0x%02X, bReq=0x%02X, wValue=0x%04X, wIndex=0x%04X, wLength=%u)\n",
                libusb_error_name(result), request_type, request, value, index, (unsigned) (response ? 8 : length));
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
        // Adaptive control transfer timeout: longer for address/length/program operations
        int timeout_ms = 5000;
        if (request == VR_SET_DATA_ADDR || request == VR_SET_DATA_LEN ||
            request == VR_PROG_STAGE1   || request == VR_PROG_STAGE2) {
            timeout_ms = 12000; // 12s to accommodate device settling after SPL
        }
        int result = libusb_control_transfer(device->handle, request_type, request, value, index,
            response ? response : data, response ? 8 : length, timeout_ms);

        if (result >= 0) {
            // Success
            if (response_length) {
                *response_length = result;
            }
            return THINGINO_SUCCESS;
        }

        // On timeout/pipe/no-device, try an immediate recipient=interface fallback for key ops
        if ((result == LIBUSB_ERROR_TIMEOUT || result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_NO_DEVICE)) {
            bool is_key_op = (request == VR_SET_DATA_ADDR || request == VR_SET_DATA_LEN || request == VR_PROG_STAGE2);
            bool is_vendor_type = ((request_type & 0x60) == 0x40);
            bool is_device_recipient = ((request_type & 0x1F) == 0x00); // recipient=device
            if (is_key_op && is_vendor_type && is_device_recipient) {
                uint8_t alt_request_type = (uint8_t)((request_type & 0xE0) | 0x01); // keep dir+type, set recipient=interface
                int alt = libusb_control_transfer(device->handle, alt_request_type, request, value, index,
                    response ? response : data, response ? 8 : length, timeout_ms);
                if (alt >= 0) {
                    if (response_length) {
                        *response_length = alt;
                    }
                    DEBUG_PRINT("Vendor request succeeded after switching to recipient=interface (0x%02X)\n", alt_request_type);
                    return THINGINO_SUCCESS;
                } else {
                    DEBUG_PRINT("Vendor request recipient=interface (0x%02X) attempt failed: %s\n", alt_request_type, libusb_error_name(alt));
                }
            }

            // Continue retry loop
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
// Fixed version of usb_device_get_cpu_info function
#include "thingino.h"

thingino_error_t usb_device_get_cpu_info_fixed(usb_device_t* device, cpu_info_t* info) {
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
    
    DEBUG_PRINT("GetCPUInfo: CPU magic bytes: ");
    for (int i = 0; i < 8; i++) {
        printf("0x%02X ", data[i]);
    }
    printf("-> string = '%s'\n", cpu_str);
    
    // Determine boot stage based on CPU info
    if (strncmp(cpu_str, "Boot", 4) == 0) {
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
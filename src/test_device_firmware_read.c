#include "thingino.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Real device test program for firmware reading with timeout fixes
int main() {
    printf("=== Device Firmware Reader Test ===\n");
    printf("Testing enhanced firmware reading with real device...\n\n");
    
    // Initialize USB manager
    usb_manager_t manager;
    thingino_error_t result = usb_manager_init(&manager);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to initialize USB manager: %s\n", thingino_error_to_string(result));
        return 1;
    }
    
    // Find connected devices
    device_info_t* devices = NULL;
    int device_count = 0;
    result = usb_manager_find_devices(&manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to find devices: %s\n", thingino_error_to_string(result));
        usb_manager_cleanup(&manager);
        return 1;
    }
    
    if (device_count == 0) {
        printf("No Ingenic devices found. Please connect a device and try again.\n");
        usb_manager_cleanup(&manager);
        return 1;
    }
    
    printf("Found %d device(s):\n", device_count);
    for (int i = 0; i < device_count; i++) {
        device_info_t* dev = &devices[i];
        printf("  Device %d: VID=0x%04X, PID=0x%04X, Bus=%d, Addr=%d, Stage=%s\n",
            i, dev->vendor, dev->product, dev->bus, dev->address, 
            device_stage_to_string(dev->stage));
    }
    
    // Use first device for testing
    device_info_t* target_device = &devices[0];
    printf("\nUsing device 0 for firmware reading test...\n");
    
    // Open device
    usb_device_t device;
    result = usb_device_init(&device, target_device->bus, target_device->address);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to open device: %s\n", thingino_error_to_string(result));
        free(devices);
        usb_manager_cleanup(&manager);
        return 1;
    }
    
    printf("Device opened successfully\n");
    
    // Check device stage and CPU info
    cpu_info_t cpu_info;
    result = usb_device_get_cpu_info(&device, &cpu_info);
    if (result == THINGINO_SUCCESS) {
        // Show raw hex bytes for debugging
        printf("CPU magic (raw hex): ");
        for (int i = 0; i < 8; i++) {
            printf("%02X ", cpu_info.magic[i]);
        }
        printf("\n");

        printf("CPU Info: '%s' (clean: '%s')\n",
            cpu_info.clean_magic, cpu_info.clean_magic);
        printf("Device Stage: %s\n", device_stage_to_string(cpu_info.stage));

        // Detect and display processor variant
        processor_variant_t detected_variant = detect_variant_from_magic(cpu_info.clean_magic);
        printf("Detected processor variant: %s\n", processor_variant_to_string(detected_variant));
    } else {
        printf("Warning: Could not get CPU info: %s\n", thingino_error_to_string(result));
    }
    
    // Test firmware reading if device is in firmware stage
    if (cpu_info.stage == STAGE_FIRMWARE) {
        printf("\nDevice is in firmware stage - testing firmware reading...\n");
        
        // Test our enhanced firmware reading
        uint8_t* firmware_data = NULL;
        uint32_t firmware_size = 0;
        
        printf("Attempting to read firmware with enhanced timeout handling...\n");
        result = firmware_read_full(&device, &firmware_data, &firmware_size);
        
        if (result == THINGINO_SUCCESS) {
            printf("SUCCESS: Firmware read completed!\n");
            printf("  Size: %u bytes (%.2f MB)\n", firmware_size, (float)firmware_size / (1024 * 1024));
            
            // Save to file for verification
            char filename[256];
            snprintf(filename, sizeof(filename), "firmware_test_%u.bin", (unsigned int)time(NULL));
            
            FILE* file = fopen(filename, "wb");
            if (file) {
                fwrite(firmware_data, 1, firmware_size, file);
                fclose(file);
                printf("  Saved to: %s\n", filename);
            } else {
                printf("  Warning: Could not save firmware to file\n");
            }
            
            free(firmware_data);
        } else {
            printf("FAILED: Firmware read failed with error: %s\n", thingino_error_to_string(result));
            
            // Provide troubleshooting guidance
            printf("\nTroubleshooting:\n");
            printf("1. Ensure device is properly bootstrapped to firmware stage\n");
            printf("2. Check USB cable connection\n");
            printf("3. Try running with sudo for USB access\n");
            printf("4. Device may need to be power-cycled\n");
        }
    } else {
        printf("\nDevice is in bootrom stage - firmware reading not available\n");
        printf("Device needs to be bootstrapped first to transition to firmware stage\n");
    }
    
    // Cleanup
    usb_device_close(&device);
    free(devices);
    usb_manager_cleanup(&manager);
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
#include "thingino.h"

// ============================================================================
// USB MANAGER IMPLEMENTATION
// ============================================================================

thingino_error_t usb_manager_init(usb_manager_t* manager) {
    if (!manager) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("Initializing USB manager...\n");
    
    // Initialize libusb
    int result = libusb_init(&manager->context);
    if (result < 0) {
        DEBUG_PRINT("libusb_init failed: %d\n", result);
        return THINGINO_ERROR_INIT_FAILED;
    }
    
    DEBUG_PRINT("libusb initialized successfully\n");
    manager->initialized = true;
    return THINGINO_SUCCESS;
}

thingino_error_t usb_manager_find_devices(usb_manager_t* manager, device_info_t** devices, int* count) {
    if (!manager || !devices || !count) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    if (!manager->initialized) {
        return THINGINO_ERROR_INIT_FAILED;
    }
    
    *devices = NULL;
    *count = 0;
    
    // Get device list
    libusb_device** device_list;
    ssize_t device_count = libusb_get_device_list(manager->context, &device_list);
    if (device_count < 0) {
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    
    // Count Ingenic devices first
    int ingenic_count = 0;
    DEBUG_PRINT("Processing %zd devices\n", device_count);
    
    for (ssize_t i = 0; i < device_count && i < 1000; i++) {
        if (device_list[i] == NULL) {
            DEBUG_PRINT("Device %zd is NULL, breaking\n", i);
            break;
        }
        
        libusb_device* device = device_list[i];
        
        // Get device descriptor
        struct libusb_device_descriptor desc;
        int result = libusb_get_device_descriptor(device, &desc);
        if (result < 0) {
            DEBUG_PRINT("Failed to get descriptor for device %zd: %d\n", i, result);
            continue; // Skip devices we can't read
        }
        
        DEBUG_PRINT("Device %zd: VID=0x%04X, PID=0x%04X\n", i, desc.idVendor, desc.idProduct);
        
        // Check if this is an Ingenic device (support both vendor IDs)
        bool is_ingenic = (desc.idVendor == VENDOR_ID_INGENIC || desc.idVendor == VENDOR_ID_INGENIC_ALT);
        if (is_ingenic) {
            // Check for supported product IDs
            bool is_bootrom = (desc.idProduct == PRODUCT_ID_BOOTROM ||
                              desc.idProduct == PRODUCT_ID_BOOTROM2 ||
                              desc.idProduct == PRODUCT_ID_BOOTROM3);
            
            bool is_firmware = (desc.idProduct == PRODUCT_ID_FIRMWARE ||
                               desc.idProduct == PRODUCT_ID_FIRMWARE2);
            
            if (is_bootrom || is_firmware) {
                ingenic_count++;
                DEBUG_PRINT("Found Ingenic device %zd (VID:0x%04X, PID:0x%04X)\n",
                    i, desc.idVendor, desc.idProduct);
            }
        }
    }
    
    DEBUG_PRINT("Found %d Ingenic devices\n", ingenic_count);
    
    if (ingenic_count == 0) {
        DEBUG_PRINT("No Ingenic devices found\n");
        libusb_free_device_list(device_list, 1);
        *devices = NULL;
        *count = 0;
        return THINGINO_SUCCESS;
    }
    
    DEBUG_PRINT("Allocating memory for %d devices\n", ingenic_count);
    
    // Allocate device info array
    *devices = (device_info_t*)malloc(ingenic_count * sizeof(device_info_t));
    if (!*devices) {
        DEBUG_PRINT("Memory allocation failed\n");
        libusb_free_device_list(device_list, 1);
        return THINGINO_ERROR_MEMORY;
    }
    
    DEBUG_PRINT("Memory allocated successfully\n");
    
    // Fill device info
    int device_index = 0;
    for (ssize_t i = 0; i < device_count; i++) {
        libusb_device* device = device_list[i];
        
        // Get device descriptor
        struct libusb_device_descriptor desc;
        int result = libusb_get_device_descriptor(device, &desc);
        if (result < 0) {
            continue; // Skip devices we can't read
        }
        
        // Check if this is an Ingenic device (support both vendor IDs)
        bool is_ingenic = (desc.idVendor == VENDOR_ID_INGENIC || desc.idVendor == VENDOR_ID_INGENIC_ALT);
        if (is_ingenic) {
            // Check for supported product IDs
            bool is_bootrom = (desc.idProduct == PRODUCT_ID_BOOTROM ||
                              desc.idProduct == PRODUCT_ID_BOOTROM2 ||
                              desc.idProduct == PRODUCT_ID_BOOTROM3);
            
            bool is_firmware = (desc.idProduct == PRODUCT_ID_FIRMWARE ||
                               desc.idProduct == PRODUCT_ID_FIRMWARE2);
            
            if (is_bootrom || is_firmware) {
                // Get bus and address
                uint8_t bus = libusb_get_bus_number(device);
                uint8_t addr = libusb_get_device_address(device);
                
                // Determine initial stage
                device_stage_t stage = STAGE_BOOTROM;
                if (is_firmware) {
                    stage = STAGE_FIRMWARE;
                }
                
                // Create device info
                device_info_t* info = &(*devices)[device_index];
                info->bus = bus;
                info->address = addr;
                info->vendor = desc.idVendor;
                info->product = desc.idProduct;
                info->stage = stage;
                info->variant = VARIANT_T31X; // Default
                
                // Check CPU info for bootrom devices to determine actual stage
                if (is_bootrom) {
                    DEBUG_PRINT("Checking CPU info for device %d to determine actual stage\n", device_index);
                    usb_device_t* test_device;
                    if (usb_manager_open_device(manager, info, &test_device) == THINGINO_SUCCESS) {
                        cpu_info_t cpu_info;
                        thingino_error_t cpu_result = usb_device_get_cpu_info(test_device, &cpu_info);
                        if (cpu_result == THINGINO_SUCCESS) {
                            // Check CPU magic to determine actual stage
                            if (strncmp((char*)cpu_info.magic, "Boot", 4) == 0) {
                                info->stage = STAGE_FIRMWARE;
                                DEBUG_PRINT("Device %d is actually in firmware stage (CPU magic: %.8s)\n",
                                    device_index, cpu_info.magic);
                            } else {
                                DEBUG_PRINT("Device %d is in bootrom stage (CPU magic: %.8s)\n",
                                    device_index, cpu_info.magic);
                            }
                            
                            // Update variant based on clean CPU magic string
                            processor_variant_t detected_variant = detect_variant_from_magic(cpu_info.clean_magic);
                            // Always update variant based on CPU magic detection
                            info->variant = detected_variant;
                            DEBUG_PRINT("Updated device %d variant to %s (%d) based on CPU magic\n",
                                device_index, processor_variant_to_string(detected_variant), detected_variant);
                        } else {
                            DEBUG_PRINT("Failed to get CPU info for device %d: %s\n",
                                device_index, thingino_error_to_string(cpu_result));
                        }
                        usb_device_close(test_device);
                        free(test_device);
                    } else {
                        DEBUG_PRINT("Failed to open device %d for CPU info check\n", device_index);
                    }
                }
                
                device_index++;
            }
        }
    }
    
    // Free device list
    libusb_free_device_list(device_list, 1);
    
    *count = ingenic_count;
    return THINGINO_SUCCESS;
}

// Fast enumeration that skips CPU info checking (useful during bootstrap re-detection)
thingino_error_t usb_manager_find_devices_fast(usb_manager_t* manager, device_info_t** devices, int* count) {
    if (!manager || !devices || !count) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    if (!manager->initialized) {
        return THINGINO_ERROR_INIT_FAILED;
    }
    
    *devices = NULL;
    *count = 0;
    
    // Get device list
    libusb_device** device_list;
    ssize_t device_count = libusb_get_device_list(manager->context, &device_list);
    if (device_count < 0) {
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    
    // Count Ingenic devices first
    int ingenic_count = 0;
    DEBUG_PRINT("Fast enumeration: processing %zd devices\n", device_count);
    
    for (ssize_t i = 0; i < device_count && i < 1000; i++) {
        if (device_list[i] == NULL) {
            continue;
        }
        
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device_list[i], &desc) < 0) {
            continue;
        }
        
        // Check for Ingenic vendor IDs (skip CPU info check for speed)
        if ((desc.idVendor == VENDOR_ID_INGENIC || desc.idVendor == VENDOR_ID_INGENIC_ALT) &&
            (desc.idProduct == PRODUCT_ID_BOOTROM2 || desc.idProduct == PRODUCT_ID_BOOTROM || 
             desc.idProduct == PRODUCT_ID_FIRMWARE || desc.idProduct == PRODUCT_ID_FIRMWARE2)) {
            ingenic_count++;
        }
    }
    
    if (ingenic_count == 0) {
        libusb_free_device_list(device_list, 1);
        return THINGINO_SUCCESS;
    }
    
    // Allocate space for all devices
    *devices = (device_info_t*)malloc(sizeof(device_info_t) * ingenic_count);
    if (!*devices) {
        libusb_free_device_list(device_list, 1);
        return THINGINO_ERROR_MEMORY;
    }
    
    // Populate device list (without CPU info queries)
    int device_index = 0;
    for (ssize_t i = 0; i < device_count && device_index < ingenic_count; i++) {
        if (device_list[i] == NULL) {
            continue;
        }
        
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device_list[i], &desc) < 0) {
            continue;
        }
        
        if ((desc.idVendor == VENDOR_ID_INGENIC || desc.idVendor == VENDOR_ID_INGENIC_ALT) &&
            (desc.idProduct == PRODUCT_ID_BOOTROM2 || desc.idProduct == PRODUCT_ID_BOOTROM || 
             desc.idProduct == PRODUCT_ID_FIRMWARE || desc.idProduct == PRODUCT_ID_FIRMWARE2)) {
            
            DEBUG_PRINT("Fast enumeration: found Ingenic device %d (VID:0x%04X, PID:0x%04X)\n",
                device_index, desc.idVendor, desc.idProduct);
            
            device_info_t* info = &(*devices)[device_index];
            info->bus = libusb_get_bus_number(device_list[i]);
            info->address = libusb_get_device_address(device_list[i]);
            info->vendor = desc.idVendor;
            info->product = desc.idProduct;
            
            // Assume bootrom stage for now (CPU info check skipped)
            info->stage = STAGE_BOOTROM;
            info->variant = VARIANT_T31X;
            
            device_index++;
        }
    }
    
    // Free device list
    libusb_free_device_list(device_list, 1);
    
    *count = ingenic_count;
    return THINGINO_SUCCESS;
}

thingino_error_t usb_manager_open_device(usb_manager_t* manager, const device_info_t* info, usb_device_t** device) {
    if (!manager || !info || !device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    if (!manager->initialized) {
        return THINGINO_ERROR_INIT_FAILED;
    }
    
    DEBUG_PRINT("Allocating device structure...\n");
    // Allocate device structure
    *device = (usb_device_t*)malloc(sizeof(usb_device_t));
    if (!*device) {
        DEBUG_PRINT("Failed to allocate device structure\n");
        return THINGINO_ERROR_MEMORY;
    }
    
    DEBUG_PRINT("Setting device info and context...\n");
    // Copy device info and set context before initialization
    (*device)->info = *info;
    (*device)->context = manager->context;
    DEBUG_PRINT("Manager device variant: %d (%s)\n",
        info->variant, processor_variant_to_string(info->variant));
    
    DEBUG_PRINT("Initializing device (bus=%d, addr=%d)...\n", info->bus, info->address);
    // Initialize device
    thingino_error_t result = usb_device_init(*device, info->bus, info->address);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Device init failed: %s\n", thingino_error_to_string(result));
        free(*device);
        *device = NULL;
        return result;
    }
    
    DEBUG_PRINT("Device initialized successfully\n");
    
    return THINGINO_SUCCESS;
}

void usb_manager_cleanup(usb_manager_t* manager) {
    if (manager && manager->initialized && manager->context) {
        libusb_exit(manager->context);
        manager->context = NULL;
        manager->initialized = false;
    }
}
/*
 * Auto-generated write sequence from USB capture
 * Source: usb_captures/vendor_write_real_20251118_122703.pcap
 */

// Write Sequence #1
// Flash Address: 0x00020100
// Data Size: Unknown
thingino_error_t write_sequence_1(usb_device_t* device, const uint8_t* data, uint32_t data_size) {
    thingino_error_t result;

    // Step 1: Set flash address
    result = protocol_set_data_address(device, 0x00020100);
    if (result != THINGINO_SUCCESS) return result;

    // Step 2: Bulk OUT transfer (12 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 3: Bulk OUT transfer (12 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 4: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 5: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 6: Bulk OUT transfer (12 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 7: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 8: Bulk OUT transfer (12 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 9: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    return THINGINO_SUCCESS;
}

// Write Sequence #2
// Flash Address: 0x10008000
// Data Size: 21233664 bytes
thingino_error_t write_sequence_2(usb_device_t* device, const uint8_t* data, uint32_t data_size) {
    thingino_error_t result;

    // Step 1: Set flash address
    result = protocol_set_data_address(device, 0x10008000);
    if (result != THINGINO_SUCCESS) return result;

    // Step 2: Set data length
    result = protocol_set_data_length(device, data_size);
    if (result != THINGINO_SUCCESS) return result;

    // Step 3: Bulk OUT transfer (324 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 4: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    return THINGINO_SUCCESS;
}

// Write Sequence #3
// Flash Address: 0x18008000
// Data Size: 1879048192 bytes
thingino_error_t write_sequence_3(usb_device_t* device, const uint8_t* data, uint32_t data_size) {
    thingino_error_t result;

    // Step 1: Set flash address
    result = protocol_set_data_address(device, 0x18008000);
    if (result != THINGINO_SUCCESS) return result;

    // Step 2: Set data length
    result = protocol_set_data_length(device, data_size);
    if (result != THINGINO_SUCCESS) return result;

    // Step 3: Bulk OUT transfer (10092 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 4: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 5: Set data length
    result = protocol_set_data_length(device, data_size);
    if (result != THINGINO_SUCCESS) return result;

    return THINGINO_SUCCESS;
}

// Write Sequence #4
// Flash Address: 0x00008010
// Data Size: 4119068677 bytes
thingino_error_t write_sequence_4(usb_device_t* device, const uint8_t* data, uint32_t data_size) {
    thingino_error_t result;

    // Step 1: Set flash address
    result = protocol_set_data_address(device, 0x00008010);
    if (result != THINGINO_SUCCESS) return result;

    // Step 2: Set data length
    result = protocol_set_data_length(device, data_size);
    if (result != THINGINO_SUCCESS) return result;

    // Step 3: Bulk OUT transfer (245760 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 4: Bulk OUT transfer (0 bytes)
    int transferred;
    result = libusb_bulk_transfer(device->handle, ENDPOINT_OUT,
        (uint8_t*)data, data_size, &transferred, 5000);
    if (result != LIBUSB_SUCCESS) return THINGINO_ERROR_TRANSFER_FAILED;

    // Step 5: Flush cache
    result = protocol_flush_cache(device);
    if (result != THINGINO_SUCCESS) return result;

    return THINGINO_SUCCESS;
}


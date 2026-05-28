/**
 * File Operations Tests
 *
 * Tests firmware file readability checks used to validate
 * firmware files before erase operations.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "thingino.h"

bool g_debug_enabled = false;

static int passed = 0;
static int failed = 0;

#define TEST(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

static void test_null_path(void) {
    printf("\nNull path tests:\n");

    thingino_error_t result = firmware_file_check_readable(NULL);
    TEST(result == THINGINO_ERROR_INVALID_PARAMETER,
         "NULL path returns INVALID_PARAMETER");
}

static void test_nonexistent_file(void) {
    printf("\nNonexistent file tests:\n");

    thingino_error_t result = firmware_file_check_readable("/nonexistent/path/firmware.bin");
    TEST(result == THINGINO_ERROR_FILE_IO,
         "nonexistent file returns FILE_IO");

    result = firmware_file_check_readable("");
    TEST(result == THINGINO_ERROR_FILE_IO,
         "empty string path returns FILE_IO");
}

static void test_valid_file(void) {
    printf("\nValid file tests:\n");

    const char *tmp_path = "/tmp/thingino_test_valid_fw.bin";

    /* Create a temporary file */
    FILE *f = fopen(tmp_path, "wb");
    TEST(f != NULL, "create temp file");
    if (f) {
        uint8_t header[] = {0x00, 0x01, 0x02, 0x03};
        fwrite(header, 1, sizeof(header), f);
        fclose(f);

        thingino_error_t result = firmware_file_check_readable(tmp_path);
        TEST(result == THINGINO_SUCCESS,
             "existing readable file returns SUCCESS");

        remove(tmp_path);
    }
}

static void test_load_file_fail(void) {
    printf("\nload_file with nonexistent file:\n");

    uint8_t *data = NULL;
    size_t size = 0;
    thingino_error_t result = load_file("/nonexistent/path/spl.bin", &data, &size);
    TEST(result == THINGINO_ERROR_FILE_IO,
         "load_file returns FILE_IO for nonexistent file");
    TEST(data == NULL,
         "data is NULL after failed load");
}

int main(void) {
    printf("=== File Operations Tests ===\n");

    test_null_path();
    test_nonexistent_file();
    test_valid_file();
    test_load_file_fail();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

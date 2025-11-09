# Embedded Firmware Database

## Overview

The thingino-cloner tool includes embedded SPL and U-Boot binaries for all supported Ingenic processors. This eliminates the need to distribute separate firmware files with the tool.

## Architecture

The firmware database uses a modular architecture with separate files for each processor:

```
src/firmware/
├── firmware_database.h      # Public API
├── firmware_database.c      # Registry implementation (auto-generated)
├── firmware_registry.h      # Internal registry (auto-generated)
├── firmware_a1_n_ne_x.c    # A1 N/NE/X firmware (auto-generated)
├── firmware_a1_nt_a.c      # A1 NT/A firmware (auto-generated)
├── firmware_t20.c          # T20 firmware (auto-generated)
├── firmware_t21.c          # T21 firmware (auto-generated)
├── firmware_t23.c          # T23 firmware (auto-generated)
├── firmware_t30.c          # T30 firmware (auto-generated)
├── firmware_t30a.c         # T30A firmware (auto-generated)
├── firmware_t30nl.c        # T30NL firmware (auto-generated)
├── firmware_t30x.c         # T30X firmware (auto-generated)
├── firmware_t31.c          # T31 firmware (auto-generated)
├── firmware_t31a.c         # T31A firmware (auto-generated)
├── firmware_t31nl.c        # T31NL firmware (auto-generated)
├── firmware_t31x.c         # T31X firmware (auto-generated)
├── firmware_t40.c          # T40 firmware (auto-generated)
└── firmware_t41.c          # T41 firmware (auto-generated)
```

### Benefits of Split Architecture

1. **Faster Compilation**: Each processor firmware is in a separate file (~2.5 MB each), allowing parallel compilation
2. **Incremental Builds**: Only changed firmware files need recompilation
3. **Modularity**: Easy to add/remove processors without affecting others
4. **Maintainability**: Registry file is small (~80 lines) and easy to understand

## Supported Processors

The following processors have embedded firmware:

| Processor | SPL Size | U-Boot Size | Total |
|-----------|----------|-------------|-------|
| a1_n_ne_x | 8.5 KB   | 387 KB      | 396 KB |
| a1_nt_a   | 8.5 KB   | 386 KB      | 395 KB |
| t20       | 9.9 KB   | 381 KB      | 391 KB |
| t21       | 8.6 KB   | 381 KB      | 390 KB |
| t23       | 9.9 KB   | 380 KB      | 390 KB |
| t30       | 9.7 KB   | 381 KB      | 391 KB |
| t30a      | 9.7 KB   | 381 KB      | 391 KB |
| t30nl     | 9.7 KB   | 381 KB      | 391 KB |
| t30x      | 9.7 KB   | 381 KB      | 391 KB |
| t31       | 9.9 KB   | 381 KB      | 391 KB |
| t31a      | 9.9 KB   | 381 KB      | 391 KB |
| t31nl     | 9.9 KB   | 381 KB      | 391 KB |
| t31x      | 9.9 KB   | 381 KB      | 391 KB |
| t40       | 11.7 KB  | 413 KB      | 425 KB |
| t41       | 9.6 KB   | 395 KB      | 405 KB |

**Total Embedded Size**: ~5.8 MB (145 KB SPL + 5.6 MB U-Boot)

## API Usage

### Get Firmware for a Processor

```c
#include "firmware_database.h"

const firmware_binary_t *fw = firmware_get("t31x");
if (fw) {
    printf("SPL: %zu bytes\n", fw->spl_size);
    printf("U-Boot: %zu bytes\n", fw->uboot_size);
    
    // Use the firmware data
    send_firmware(fw->spl_data, fw->spl_size);
    send_firmware(fw->uboot_data, fw->uboot_size);
}
```

### Check if Firmware is Available

```c
if (firmware_available("t41")) {
    printf("T41 firmware is available\n");
}
```

### List All Available Firmwares

```c
size_t count;
const firmware_binary_t *firmwares = firmware_list(&count);

for (size_t i = 0; i < count; i++) {
    printf("%s: SPL=%zu, U-Boot=%zu\n",
           firmwares[i].processor,
           firmwares[i].spl_size,
           firmwares[i].uboot_size);
}
```

## Regenerating Firmware Database

If you need to update the embedded firmware (e.g., to add new processors or update existing ones):

```bash
# Generate firmware database from cloner firmwares
python3 tools/generate_firmware_database.py \
    references/cloner-2.5.43-ubuntu_thingino/firmwares \
    src/firmware

# Rebuild
make clean
make build
```

The script will:
1. Read SPL and U-Boot binaries from the firmwares directory
2. Generate separate C files for each processor
3. Generate a registry file that ties them together
4. Generate a small database implementation file

## Adding New Processors

To add a new processor to the embedded database:

1. Edit `tools/generate_firmware_database.py`
2. Add the processor name to the `EMBEDDED_PROCESSORS` list
3. Ensure the firmware files exist in the source directory
4. Regenerate the database (see above)

Example:
```python
EMBEDDED_PROCESSORS = [
    'a1_n_ne_x', 'a1_nt_a',
    't20', 't21', 't23',
    't30', 't30a', 't30nl', 't30x',
    't31', 't31a', 't31nl', 't31x',
    't40', 't41',
    't41n',  # Add new processor here
]
```

## Binary Size Considerations

The embedded firmware adds approximately 5.8 MB to the final binary size. This is acceptable for a desktop tool but may be too large for embedded systems. If binary size is a concern, you can:

1. Reduce the number of embedded processors in `EMBEDDED_PROCESSORS`
2. Implement external firmware loading as a fallback
3. Use compression (future enhancement)

## Testing

Test the firmware database:

```bash
./build/test_firmware_database
```

This will verify:
- All processors are accessible
- Firmware sizes are correct
- Data integrity (first bytes of SPL)


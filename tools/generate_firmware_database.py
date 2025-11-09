#!/usr/bin/env python3
"""
Generate embedded firmware database from binary files

This script reads SPL and U-Boot binaries from the cloner firmwares directory
and generates separate C source files for each processor, plus a registry file
that ties them all together. This approach makes compilation much faster and
more modular than having all binaries in a single file.
"""

import os
import sys
from pathlib import Path

def read_binary(filepath):
    """Read binary file and return bytes"""
    with open(filepath, 'rb') as f:
        return f.read()

def bytes_to_c_array(data, name, bytes_per_line=12):
    """Convert bytes to C array initialization"""
    lines = []
    lines.append(f"static const uint8_t {name}[] = {{")
    
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i:i+bytes_per_line]
        hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f"    {hex_values},")
    
    lines.append("};")
    return '\n'.join(lines)

def generate_processor_file(proc_name, spl_data, uboot_data, output_dir):
    """Generate a single processor firmware C file"""

    proc_safe_name = proc_name.replace('-', '_').replace('.', '_')
    output_file = output_dir / f"firmware_{proc_safe_name}.c"

    with open(output_file, 'w') as f:
        f.write(f"""/**
 * Embedded Firmware for {proc_name}
 * Auto-generated - DO NOT EDIT
 */

#include <stdint.h>
#include <stddef.h>

""")

        # Write SPL array
        f.write(f"// {proc_name} SPL ({len(spl_data)} bytes)\n")
        f.write(bytes_to_c_array(spl_data, f"spl_{proc_safe_name}"))
        f.write("\n\n")

        # Write U-Boot array
        f.write(f"// {proc_name} U-Boot ({len(uboot_data)} bytes)\n")
        f.write(bytes_to_c_array(uboot_data, f"uboot_{proc_safe_name}"))
        f.write("\n\n")

        # Write accessor functions
        f.write(f"""const uint8_t* firmware_{proc_safe_name}_get_spl(size_t *size) {{
    if (size) *size = sizeof(spl_{proc_safe_name});
    return spl_{proc_safe_name};
}}

const uint8_t* firmware_{proc_safe_name}_get_uboot(size_t *size) {{
    if (size) *size = sizeof(uboot_{proc_safe_name});
    return uboot_{proc_safe_name};
}}
""")

    return output_file

def generate_firmware_database(firmwares_dir, output_dir):
    """Generate firmware database with separate files per processor"""

    firmwares_path = Path(firmwares_dir)
    output_path = Path(output_dir)

    if not firmwares_path.exists():
        print(f"Error: Firmwares directory not found: {firmwares_dir}")
        return 1

    # Create output directory if it doesn't exist
    output_path.mkdir(parents=True, exist_ok=True)

    # Only embed the most commonly used processors to keep binary size reasonable
    EMBEDDED_PROCESSORS = [
        'a1_n_ne_x', 'a1_nt_a',  # A-series
        't20', 't21', 't23',      # T20 series
        't30', 't30a', 't30nl', 't30x',  # T30 series
        't31', 't31a', 't31nl', 't31x',  # T31 series
        't40', 't41',             # T40 series
    ]

    # Find all processor directories with spl.bin and uboot.bin
    processors = []
    for proc_dir in sorted(firmwares_path.iterdir()):
        if not proc_dir.is_dir():
            continue

        # Only include processors in our embedded list
        if proc_dir.name not in EMBEDDED_PROCESSORS:
            continue

        spl_file = proc_dir / "spl.bin"
        uboot_file = proc_dir / "uboot.bin"

        if spl_file.exists() and uboot_file.exists():
            processors.append({
                'name': proc_dir.name,
                'spl_path': spl_file,
                'uboot_path': uboot_file
            })

    print(f"Found {len(processors)} processors with firmware binaries (embedded subset)")

    # Generate individual processor files
    generated_files = []
    for proc in processors:
        spl_data = read_binary(proc['spl_path'])
        uboot_data = read_binary(proc['uboot_path'])

        print(f"  {proc['name']}: SPL={len(spl_data)} bytes, U-Boot={len(uboot_data)} bytes")

        proc_file = generate_processor_file(proc['name'], spl_data, uboot_data, output_path)
        generated_files.append(proc_file)

    # Generate registry header file
    registry_header = output_path / "firmware_registry.h"
    with open(registry_header, 'w') as f:
        f.write("""/**
 * Firmware Registry - Auto-generated
 * DO NOT EDIT
 */

#ifndef FIRMWARE_REGISTRY_H
#define FIRMWARE_REGISTRY_H

#include <stdint.h>
#include <stddef.h>

""")

        # Declare external functions for each processor
        for proc in processors:
            proc_safe_name = proc['name'].replace('-', '_').replace('.', '_')
            f.write(f"const uint8_t* firmware_{proc_safe_name}_get_spl(size_t *size);\n")
            f.write(f"const uint8_t* firmware_{proc_safe_name}_get_uboot(size_t *size);\n")

        f.write("\n#endif // FIRMWARE_REGISTRY_H\n")

    # Generate registry implementation
    registry_impl = output_path / "firmware_database.c"
    with open(registry_impl, 'w') as f:
        f.write("""/**
 * Firmware Database Registry - Auto-generated
 * DO NOT EDIT
 */

#include "firmware_database.h"
#include "firmware_registry.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

""")

        # Generate firmware table with function pointers
        f.write("// Firmware registry table\n")
        f.write("typedef struct {\n")
        f.write("    const char *processor;\n")
        f.write("    const uint8_t* (*get_spl)(size_t *size);\n")
        f.write("    const uint8_t* (*get_uboot)(size_t *size);\n")
        f.write("} firmware_registry_entry_t;\n\n")

        f.write("static const firmware_registry_entry_t firmware_registry[] = {\n")
        for proc in processors:
            proc_safe_name = proc['name'].replace('-', '_').replace('.', '_')
            f.write(f"    {{\"{proc['name']}\", firmware_{proc_safe_name}_get_spl, firmware_{proc_safe_name}_get_uboot}},\n")
        f.write("};\n\n")

        # Generate lookup functions
        f.write("""const firmware_binary_t* firmware_get(const char *processor) {
    if (!processor) return NULL;

    static firmware_binary_t result;

    for (size_t i = 0; i < sizeof(firmware_registry) / sizeof(firmware_registry[0]); i++) {
        if (strcasecmp(firmware_registry[i].processor, processor) == 0) {
            result.processor = firmware_registry[i].processor;
            result.spl_data = firmware_registry[i].get_spl(&result.spl_size);
            result.uboot_data = firmware_registry[i].get_uboot(&result.uboot_size);
            return &result;
        }
    }

    return NULL;
}

const firmware_binary_t* firmware_list(size_t *count) {
    if (count) {
        *count = sizeof(firmware_registry) / sizeof(firmware_registry[0]);
    }

    // Build array of firmware_binary_t on first call
    static firmware_binary_t *list = NULL;
    static size_t list_size = 0;

    if (!list) {
        list_size = sizeof(firmware_registry) / sizeof(firmware_registry[0]);
        list = malloc(list_size * sizeof(firmware_binary_t));

        for (size_t i = 0; i < list_size; i++) {
            list[i].processor = firmware_registry[i].processor;
            list[i].spl_data = firmware_registry[i].get_spl(&list[i].spl_size);
            list[i].uboot_data = firmware_registry[i].get_uboot(&list[i].uboot_size);
        }
    }

    return list;
}

int firmware_available(const char *processor) {
    return firmware_get(processor) != NULL;
}
""")

    print(f"\nGenerated {len(generated_files)} processor files in: {output_path}")
    print(f"Generated registry: {registry_header}")
    print(f"Generated database: {registry_impl}")
    print(f"Total processors: {len(processors)}")
    return 0

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: generate_firmware_database.py <firmwares_dir> <output_dir>")
        print("Example: generate_firmware_database.py references/cloner-2.5.43-ubuntu_thingino/firmwares src/firmware")
        sys.exit(1)

    sys.exit(generate_firmware_database(sys.argv[1], sys.argv[2]))


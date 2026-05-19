#include "thingino.h"
#include "platform.h"
#include "flash_descriptor.h"
#include "spi_nor_db.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>

/*
 * Flash descriptor format (972 bytes):
 *
 * [0x000-0x01F] GBD/ILOP header (32 bytes)
 * [0x020-0x0C7] Policy section (168 bytes, partition layout placeholder)
 * [0x0C8-0x34F] SFC Config 1 (648 bytes, full flash config + partition)
 * [0x350-0x3CB] SFC Config 2 (124 bytes, abbreviated flash config)
 *
 * SFC Config 1 layout (relative to 0xC8):
 *   +0x00: "\0CFS" magic + u32 size (0x2FC)
 *   +0x08: reserved (16 bytes)
 *   +0x12: flag = 0x01
 *   +0x20: "nor\0" + type=1
 *   +0x28: chip name (32 bytes, null-padded)
 *   +0x48: JEDEC ID (4 bytes LE)
 *   +0x4C: 7 commands x 6 bytes: {opcode, 0, dummy, addr_bytes, mode, 0}
 *          read, qread, write, qwrite, erase, we, en4b
 *   +0x76: 3 status regs x 8 bytes: {cmd, 0, bit, rd_en, rd_cmd, wr_en, rsv, 0}
 *   +0x8E: quad_enable (u8), addr_mode (u8)
 *   +0x90: 5 timing values as u32 LE
 *   +0xA4: geometry flags
 *   +0xAC: erase_size as u16 LE + page_size
 *   +0xB0: chip_erase_cmd as u32 LE
 *   +0xB4: partition name "full" (32 bytes)
 *   +0xD4: partition size = 0xFFFFFFFF (whole chip)
 */

/* Write a command (6 bytes) into the descriptor buffer */
static void write_cmd(uint8_t *buf, const int32_t cmd[4]) {
    buf[0] = (uint8_t)(cmd[0] & 0xFF); /* opcode */
    buf[1] = 0;
    buf[2] = (uint8_t)(cmd[1] & 0xFF); /* dummy_cycles */
    buf[3] = (uint8_t)(cmd[2] & 0xFF); /* addr_bytes */
    buf[4] = (uint8_t)(cmd[3] & 0xFF); /* mode */
    buf[5] = 0;
}

/* Write a status register config (8 bytes) */
static void write_sr(uint8_t *buf, const int32_t sr[6]) {
    buf[0] = (uint8_t)(sr[0] & 0xFF); /* cmd */
    buf[1] = 0;
    buf[2] = (uint8_t)(sr[1] & 0xFF); /* bit */
    buf[3] = (uint8_t)(sr[2] & 0xFF); /* read_en */
    buf[4] = (uint8_t)(sr[3] & 0xFF); /* read_cmd */
    buf[5] = (uint8_t)(sr[4] & 0xFF); /* write_en */
    buf[6] = (uint8_t)(sr[5] & 0xFF); /* reserved */
    buf[7] = 0;
}

/* Write u32 LE */
static void write_u32(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

/**
 * Fill SFC Config 1 section (648 bytes at descriptor offset 0xC8).
 * Contains full flash config with commands, timing, and partition info.
 */
static void fill_sfc_config1_ex(uint8_t *buf, const spi_nor_chip_t *chip, int erase_flag) {
    memset(buf, 0, 648);

    /* SFC magic + section size */
    buf[0x00] = 0x00;
    buf[0x01] = 'C';
    buf[0x02] = 'F';
    buf[0x03] = 'S';
    write_u32(buf + 0x04, 0x2FC);
    buf[0x12] = 0x01;
    buf[0x14] = erase_flag ? 0x01 : 0x00; /* SPI_ERASE_PART=1, SPI_NO_ERASE=0 */

    /* Flash type */
    buf[0x20] = 'n';
    buf[0x21] = 'o';
    buf[0x22] = 'r';
    write_u32(buf + 0x24, 1);

    /* Chip name (32 bytes) */
    size_t nlen = strlen(chip->name);
    if (nlen > 31)
        nlen = 31;
    memcpy(buf + 0x28, chip->name, nlen);

    /* JEDEC ID */
    write_u32(buf + 0x48, chip->jedec_id);

    /* 7 commands x 6 bytes each starting at +0x4C */
    write_cmd(buf + 0x4C, chip->cmd_read);
    write_cmd(buf + 0x52, chip->cmd_qread);
    write_cmd(buf + 0x58, chip->cmd_write);
    write_cmd(buf + 0x5E, chip->cmd_qwrite);
    write_cmd(buf + 0x64, chip->cmd_erase);
    write_cmd(buf + 0x6A, chip->cmd_we);
    write_cmd(buf + 0x70, chip->cmd_en4b);

    /* 3 status registers x 8 bytes each */
    write_sr(buf + 0x76, chip->sr1);
    write_sr(buf + 0x7E, chip->sr2);
    write_sr(buf + 0x86, chip->sr3);

    /* quad_enable + addr_mode */
    buf[0x8E] = chip->quad_enable;
    buf[0x8F] = chip->addr_mode;

    /* 5 timing values as u32 LE (skip timing[0], start from timing[1]) */
    write_u32(buf + 0x90, 0); /* timing[0] reserved in binary */
    write_u32(buf + 0x94, (uint32_t)chip->timing[1]);
    write_u32(buf + 0x98, (uint32_t)chip->timing[2]);
    write_u32(buf + 0x9C, (uint32_t)chip->timing[3]);
    write_u32(buf + 0xA0, (uint32_t)chip->timing[4]);

    /* Geometry */
    buf[0xA4] = 0x00;
    buf[0xA5] = 0x00;
    buf[0xA6] = 0x00;
    buf[0xA7] = 0x01;
    buf[0xA9] = 0x01;
    write_u32(buf + 0xAC, chip->erase_size);
    write_u32(buf + 0xB0, chip->chip_erase_cmd);

    /* Partition: "full_image" (32-byte name), size = chip size */
    memcpy(buf + 0xB4, "full_image", 10);
    write_u32(buf + 0xD4, chip->size); /* partition size = chip capacity */
}

/* Default: erase enabled (for write operations) */
static void fill_sfc_config1(uint8_t *buf, const spi_nor_chip_t *chip) {
    fill_sfc_config1_ex(buf, chip, 1);
}

/**
 * Fill SFC Config 1 for T32 (652 bytes = T31's 648 + 4).
 *
 * T32's vendor cloner 2.5.49 uses a shifted layout: blocksize at +0x10,
 * SFC frequency at +0x18, then all fields from "nor" onwards shifted +4
 * compared to T31. Verified from t32_vendor_write.pcap.
 */
static void fill_sfc_config1_t32(uint8_t *buf, const spi_nor_chip_t *chip, int erase_flag) {
    memset(buf, 0, 652);

    buf[0x00] = 0x00;
    buf[0x01] = 'C';
    buf[0x02] = 'F';
    buf[0x03] = 'S';
    write_u32(buf + 0x04, 0x300); /* section size = 768 (T31 = 764) */

    write_u32(buf + 0x10, 0x8000);        /* blocksize = 32KB */
    buf[0x14] = erase_flag ? 0x01 : 0x00; /* erase flag */
    write_u32(buf + 0x18, 50000000);      /* SFC frequency = 50MHz (0x02FAF080) */
    write_u32(buf + 0x20, 0x6800);        /* SFC rate config (from vendor pcap) */

    /* "nor" flash type — shifted +4 from T31 */
    buf[0x24] = 'n';
    buf[0x25] = 'o';
    buf[0x26] = 'r';
    write_u32(buf + 0x28, 1);

    /* Chip name at +0x2C (T31: +0x28) */
    size_t nlen = strlen(chip->name);
    if (nlen > 31)
        nlen = 31;
    memcpy(buf + 0x2C, chip->name, nlen);

    /* JEDEC ID at +0x4C (T31: +0x48) */
    write_u32(buf + 0x4C, chip->jedec_id);

    /* Commands at +0x50 (T31: +0x4C), all shifted +4 */
    write_cmd(buf + 0x50, chip->cmd_read);
    write_cmd(buf + 0x56, chip->cmd_qread);
    write_cmd(buf + 0x5C, chip->cmd_write);
    write_cmd(buf + 0x62, chip->cmd_qwrite);
    write_cmd(buf + 0x68, chip->cmd_erase);
    write_cmd(buf + 0x6E, chip->cmd_we);
    write_cmd(buf + 0x74, chip->cmd_en4b);

    /* Status registers at +0x7A (T31: +0x76) */
    write_sr(buf + 0x7A, chip->sr1);
    write_sr(buf + 0x82, chip->sr2);
    write_sr(buf + 0x8A, chip->sr3);

    /* quad_enable + addr_mode at +0x92 (T31: +0x8E) */
    buf[0x92] = chip->quad_enable;
    buf[0x93] = chip->addr_mode;

    /* Timing at +0x94 (T31: +0x90) */
    write_u32(buf + 0x94, 0);
    write_u32(buf + 0x98, (uint32_t)chip->timing[1]);
    write_u32(buf + 0x9C, (uint32_t)chip->timing[2]);
    write_u32(buf + 0xA0, (uint32_t)chip->timing[3]);
    write_u32(buf + 0xA4, (uint32_t)chip->timing[4]);

    /* Geometry at +0xA8 (T31: +0xA4) */
    buf[0xA8] = 0x00;
    buf[0xA9] = 0x00;
    buf[0xAA] = 0x00;
    buf[0xAB] = 0x01;
    buf[0xAD] = 0x01;
    write_u32(buf + 0xB0, chip->erase_size);
    write_u32(buf + 0xB4, chip->chip_erase_cmd);

    /* Partition at +0xB8 (T31: +0xB4) */
    memcpy(buf + 0xB8, "full", 4);
    write_u32(buf + 0xD8, 0xFFFFFFFF); /* partition size */
    write_u32(buf + 0xE4, 0x02);       /* partition count/flag (from vendor pcap) */
}

/**
 * Fill SFC Config 2 section (124 bytes at descriptor offset 0x350).
 * Abbreviated flash config - commands + SR + geometry, no partition info.
 *
 * Layout (from vendor WIN25Q128 template):
 *   +0x12: flag=1
 *   +0x18: chip name (32 bytes)
 *   +0x38: JEDEC ID (4 bytes)
 *   +0x3C: commands (read, qread only, then we+en4b)
 *   +0x4C: SR configs
 *   +0x64: quad_enable, addr_mode
 *   +0x68: geometry (size/erase)
 */
static void fill_sfc_config2(uint8_t *buf, const spi_nor_chip_t *chip) {
    memset(buf, 0, 124);

    buf[0x0C] = 0x01; /* flag */
    buf[0x12] = 0x01; /* flag */

    /* Chip name (24 bytes at +0x18) */
    size_t nlen = strlen(chip->name);
    if (nlen > 23)
        nlen = 23;
    memcpy(buf + 0x18, chip->name, nlen);

    /* JEDEC ID at +0x38 */
    write_u32(buf + 0x38, chip->jedec_id);

    /* Commands: read, qread, we, en4b (4 commands x 6 bytes) */
    write_cmd(buf + 0x3C, chip->cmd_read);
    write_cmd(buf + 0x42, chip->cmd_qread);
    write_cmd(buf + 0x48, chip->cmd_we);
    write_cmd(buf + 0x4E, chip->cmd_en4b);

    /* Status registers (3 x 8 bytes) */
    write_sr(buf + 0x54, chip->sr1);
    write_sr(buf + 0x5C, chip->sr2);
    write_sr(buf + 0x64, chip->sr3);

    /* quad_enable + addr_mode + flags */
    buf[0x6C] = chip->quad_enable;
    buf[0x73] = 0x01;
    buf[0x75] = 0x01;

    /* erase_size at +0x78 */
    write_u32(buf + 0x78, chip->erase_size);
}

/**
 * Fill SFC Config 1 for A1/xburst2 (shifted layout with SFC freq field).
 *
 * A1 SFC config has an extra u32 (SFC frequency) at +0x18 that shifts
 * all subsequent fields by 4 bytes compared to T31.
 * Verified from vendor A1 capture: bulk_out_0004_frame1813_992bytes.bin
 */
static void fill_sfc_config1_a1_ex(uint8_t *buf, const spi_nor_chip_t *chip, int erase_flag) {
    memset(buf, 0, 644); /* 644 not 648: avoid 4-byte overlap with SFC2 at GBD+0x354 */

    buf[0x00] = 0x00;
    buf[0x01] = 'C';
    buf[0x02] = 'F';
    buf[0x03] = 'S';
    write_u32(buf + 0x04, 0x2FC);
    buf[0x12] = 0x01;
    buf[0x14] = erase_flag ? 0x01 : 0x00;
    write_u32(buf + 0x18, 50000000); /* SFC frequency = 50MHz (A1 specific) */

    buf[0x1C] = 'n';
    buf[0x1D] = 'o';
    buf[0x1E] = 'r';
    write_u32(buf + 0x20, 1);

    /* Chip name at +0x24 (shifted -4 from T31's +0x28) */
    size_t nlen = strlen(chip->name);
    if (nlen > 31)
        nlen = 31;
    memcpy(buf + 0x24, chip->name, nlen);

    /* JEDEC ID at +0x44 (shifted -4 from T31's +0x48) */
    write_u32(buf + 0x44, chip->jedec_id);

    /* Commands at +0x48 (shifted -4) */
    write_cmd(buf + 0x48, chip->cmd_read);
    write_cmd(buf + 0x4E, chip->cmd_qread);
    write_cmd(buf + 0x54, chip->cmd_write);
    write_cmd(buf + 0x5A, chip->cmd_qwrite);
    write_cmd(buf + 0x60, chip->cmd_erase);
    write_cmd(buf + 0x66, chip->cmd_we);
    write_cmd(buf + 0x6C, chip->cmd_en4b);

    /* Status registers at +0x72 */
    write_sr(buf + 0x72, chip->sr1);
    write_sr(buf + 0x7A, chip->sr2);
    write_sr(buf + 0x82, chip->sr3);

    buf[0x8A] = chip->quad_enable;
    buf[0x8B] = chip->addr_mode;

    /* Timing at +0x8C */
    write_u32(buf + 0x8C, 0);
    write_u32(buf + 0x90, (uint32_t)chip->timing[1]);
    write_u32(buf + 0x94, (uint32_t)chip->timing[2]);
    write_u32(buf + 0x98, (uint32_t)chip->timing[3]);
    write_u32(buf + 0x9C, (uint32_t)chip->timing[4]);

    /* Geometry at +0xA0 */
    buf[0xA0] = 0x00;
    buf[0xA1] = 0x00;
    buf[0xA2] = 0x00;
    buf[0xA3] = 0x01;
    buf[0xA5] = 0x01;
    write_u32(buf + 0xA8, chip->erase_size);
    write_u32(buf + 0xAC, chip->chip_erase_cmd);

    /* Partition at +0xB0 */
    memcpy(buf + 0xB0, "full_image", 10);
    write_u32(buf + 0xD0, 0xFFFFFFFF); /* A1 vendor uses 0xFFFFFFFF */
}

static void fill_sfc_config1_a1(uint8_t *buf, const spi_nor_chip_t *chip) {
    fill_sfc_config1_a1_ex(buf, chip, 1);
}

/**
 * Generate 984-byte xburst2 flash descriptor (T40/T41).
 * Format: 12-byte RDD prefix (0xC9 marker) + 972-byte GBD body (same as T31).
 */
int flash_descriptor_create_xb2(const spi_nor_chip_t *chip, uint8_t *buffer) {
    if (!chip || !buffer)
        return -1;

    memset(buffer, 0, FLASH_DESCRIPTOR_SIZE_XB2);

    /* RDD prefix (12 bytes) */
    buffer[0x00] = 0x00;
    buffer[0x01] = 'R';
    buffer[0x02] = 'D';
    buffer[0x03] = 'D';
    write_u32(buffer + 0x04, 4);
    buffer[0x08] = 0xC9;
    buffer[0x0A] = 0xC9;

    /* GBD body at +12 is identical to T31's 972-byte format */
    int rc = flash_descriptor_create(chip, buffer + 12);
    if (rc != 0)
        return rc;

    /* T40/xb2 override: SFC+0x10 needs 0x8000 (blocksize) instead of
     * T31's flag byte at +0x12. SFC config starts at GBD+0xC8 = buffer+12+0xC8.
     * Verified from vendor T40 USB capture. */
    uint8_t *sfc = buffer + 12 + 0xC8;
    sfc[0x10] = 0x00;
    sfc[0x11] = 0x80; /* u16 LE = 0x8000 = 32768 */
    sfc[0x12] = 0x00; /* clear T31 flag */

    return 0;
}

/**
 * Generate 992-byte A1 flash descriptor.
 * Format: 12-byte RDD prefix (0x84 marker) + 980-byte GBD body (shifted SFC).
 */
int flash_descriptor_create_a1(const spi_nor_chip_t *chip, uint8_t *buffer) {
    if (!chip || !buffer)
        return -1;

    memset(buffer, 0, FLASH_DESCRIPTOR_SIZE_A1);

    /* RDD prefix (12 bytes) */
    buffer[0x00] = 0x00;
    buffer[0x01] = 'R';
    buffer[0x02] = 'D';
    buffer[0x03] = 'D';
    write_u32(buffer + 0x04, 4);
    buffer[0x08] = 0x84;
    buffer[0x0A] = 0x84;

    /* GBD header at +12 */
    uint8_t *gbd = buffer + 12;
    gbd[0x00] = 0x00;
    gbd[0x01] = 'G';
    gbd[0x02] = 'B';
    gbd[0x03] = 'D';
    write_u32(gbd + 0x04, 0x14);
    write_u32(gbd + 0x08, 1);
    write_u32(gbd + 0x0C, 1);
    write_u32(gbd + 0x10, 1);
    gbd[0x1C] = 'I';
    gbd[0x1D] = 'L';
    gbd[0x1E] = 'O';
    gbd[0x1F] = 'P';

    /* Policy header - A1 uses 0xAC (172), not 0xA4 (164) like T31 */
    write_u32(gbd + 0x20, 0xAC);
    write_u32(gbd + 0x38, 1);

    /* Debug config string at GBD+0xC8 (8 bytes, "5,100,60") */
    memcpy(gbd + 0xC8, "5,100,60", 8);

    /* SFC Config 1 at GBD+0xD0 (after 8-byte debug string) */
    fill_sfc_config1_a1(gbd + 0xD0, chip);

    /* SFC Config 2 at GBD+0x354 (T31 uses GBD+0x350) */
    fill_sfc_config2(gbd + 0x354, chip);

    return 0;
}

/**
 * Generate flash descriptor for any SPI NOR chip from the database.
 * Produces the 972-byte descriptor for xburst1 burners (T20/T31/T32 etc).
 */
int flash_descriptor_create(const spi_nor_chip_t *chip, uint8_t *buffer) {
    if (!chip || !buffer)
        return -1;

    memset(buffer, 0, FLASH_DESCRIPTOR_SIZE);

    /* GBD header (0x00-0x1F) */
    buffer[0x00] = 0x00;
    buffer[0x01] = 'G';
    buffer[0x02] = 'B';
    buffer[0x03] = 'D';
    write_u32(buffer + 0x04, 0x14); /* count = 20 */
    write_u32(buffer + 0x08, 1);    /* flag1 */
    write_u32(buffer + 0x0C, 1);    /* flag2 */
    write_u32(buffer + 0x10, 1);    /* flag3 */
    buffer[0x1C] = 'I';
    buffer[0x1D] = 'L';
    buffer[0x1E] = 'O';
    buffer[0x1F] = 'P';

    /* Policy header (0x20-0xC7) */
    write_u32(buffer + 0x20, 0xA4); /* policy size = 164 */
    write_u32(buffer + 0x38, 1);    /* flag in policy */

    /* SFC Config 1 at 0xC8 (648 bytes) */
    fill_sfc_config1(buffer + 0xC8, chip);

    /* SFC Config 2 at 0x350 (124 bytes) */
    fill_sfc_config2(buffer + 0x350, chip);

    return 0;
}

/**
 * Generate 976-byte descriptor for T32.
 * T32's vendor cloner 2.5.49 uses a 4-byte larger SFC Config 1 section
 * (size=0x300 vs T31's 0x2FC) with blocksize and SFC freq fields added.
 * Verified from t32_vendor_write.pcap.
 */
int flash_descriptor_create_t32(const spi_nor_chip_t *chip, uint8_t *buffer) {
    if (!chip || !buffer)
        return -1;

    memset(buffer, 0, FLASH_DESCRIPTOR_SIZE_T32);

    /* GBD header */
    buffer[0x00] = 0x00;
    buffer[0x01] = 'G';
    buffer[0x02] = 'B';
    buffer[0x03] = 'D';
    write_u32(buffer + 0x04, 0x14);
    write_u32(buffer + 0x08, 1);
    write_u32(buffer + 0x0C, 1);
    write_u32(buffer + 0x10, 1);
    buffer[0x1C] = 'I';
    buffer[0x1D] = 'L';
    buffer[0x1E] = 'O';
    buffer[0x1F] = 'P';

    write_u32(buffer + 0x20, 0xA4);
    write_u32(buffer + 0x38, 1);

    /* SFC Config 1 at 0xC8 (652 bytes, 4 more than T31) */
    fill_sfc_config1_t32(buffer + 0xC8, chip, 1);

    /* SFC Config 2 at 0x354 (shifted +4 from T31's 0x350) */
    fill_sfc_config2(buffer + 0x354, chip);

    /* T32 override: SFC2+0x11=0x80 (blocksize flag), SFC2+0x12=0x00 (no T31 flag) */
    buffer[0x354 + 0x11] = 0x80;
    buffer[0x354 + 0x12] = 0x00;

    return 0;
}

int flash_descriptor_create_t32_read(const spi_nor_chip_t *chip, uint8_t *buffer) {
    int rc = flash_descriptor_create_t32(chip, buffer);
    if (rc != 0)
        return rc;
    buffer[0xC8 + 0x14] = 0x00; /* SPI_NO_ERASE */
    return 0;
}

/**
 * Generate 972-byte read descriptor (no erase).
 * Identical to flash_descriptor_create() but with SPI_NO_ERASE flag.
 */
int flash_descriptor_create_read(const spi_nor_chip_t *chip, uint8_t *buffer) {
    int rc = flash_descriptor_create(chip, buffer);
    if (rc != 0)
        return rc;
    /* Patch erase flag: SFC Config 1 is at 0xC8, erase flag at +0x14 */
    buffer[0xC8 + 0x14] = 0x00; /* SPI_NO_ERASE */
    return 0;
}

/**
 * Generate 984-byte xb2 read descriptor (no erase).
 */
int flash_descriptor_create_xb2_read(const spi_nor_chip_t *chip, uint8_t *buffer) {
    int rc = flash_descriptor_create_xb2(chip, buffer);
    if (rc != 0)
        return rc;
    /* SFC Config 1 at GBD+0xC8 = buffer+12+0xC8 = buffer+0xD4, erase at +0x14 */
    buffer[12 + 0xC8 + 0x14] = 0x00;
    return 0;
}

/**
 * Generate 992-byte A1 read descriptor (no erase).
 */
int flash_descriptor_create_a1_read(const spi_nor_chip_t *chip, uint8_t *buffer) {
    int rc = flash_descriptor_create_a1(chip, buffer);
    if (rc != 0)
        return rc;
    /* A1 SFC Config 1 at GBD+0xD0 = buffer+12+0xD0 = buffer+0xDC, erase at +0x14 */
    buffer[12 + 0xD0 + 0x14] = 0x00;
    return 0;
}

/**
 * Create flash descriptor for W25Q128JVSQ (legacy, calls generic function)
 */
int flash_descriptor_create_win25q128(uint8_t *buffer) {
    const spi_nor_chip_t *chip = spi_nor_find_by_name("W25Q128JVSQ");
    if (!chip)
        chip = spi_nor_find_by_id(0xef4018);
    if (!chip)
        return -1;
    return flash_descriptor_create(chip, buffer);
}

/**
 * Create flash descriptor for T31x NOR writer_full
 */
int flash_descriptor_create_t31x_writer_full(uint8_t *buffer) {
    /* T31 cameras commonly use GD25Q127CSIG or W25Q128.
     * Use GD25Q127CSIG as default, fall back to W25Q128. */
    const spi_nor_chip_t *chip = spi_nor_find_by_name("GD25Q127CSIG");
    if (!chip)
        chip = spi_nor_find_by_name("W25Q128JVSQ");
    if (!chip)
        return -1;
    return flash_descriptor_create(chip, buffer);
}

/**
 * Create flash descriptor for A1 NOR writer_full
 */
int flash_descriptor_create_a1_writer_full(uint8_t *buffer) {
    const spi_nor_chip_t *chip = spi_nor_find_by_name("XM25QH128B");
    if (!chip)
        chip = spi_nor_find_by_name("GD25Q127CSIG");
    if (!chip)
        return -1;
    return flash_descriptor_create_a1(chip, buffer);
}

/**
 * Send a FW_WRITE2 control command followed by a bulk OUT transfer.
 *
 * The vendor cloner sends a 40-byte VR_FW_WRITE2 (0x14) control transfer
 * before every bulk OUT. The first u32 LE of the command is the size of
 * the following bulk payload. Remaining bytes are address/config data
 * (zeros work for our use case).
 *
 * Verified from vendor USB capture: frames 13427/13441 (marker) and
 * 13449/13463 (descriptor).
 */
static thingino_error_t send_fw_write2_bulk(usb_device_t *device, const uint8_t *data, uint32_t size) {
    /* Build 40-byte FW_WRITE2 command: first u32 = bulk payload size */
    uint8_t cmd[40];
    memset(cmd, 0, sizeof(cmd));
    write_u32(cmd, size);

    /* Ensure interface is claimed — required on Windows (WinUSB) for bulk transfers */
    libusb_claim_interface(device->handle, 0);

    int result = libusb_control_transfer(device->handle, 0x40, VR_FW_WRITE2, 0, 0, cmd, 40, 5000);
    if (result < 0) {
        LOG_ERROR("FW_WRITE2 control failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    int transferred = 0;
    result = libusb_bulk_transfer(device->handle, 0x01, (unsigned char *)data, (int)size, &transferred, 5000);
    if (result != 0 || transferred != (int)size) {
        LOG_ERROR("Bulk transfer failed: %s, %d/%u bytes\n", libusb_error_name(result), transferred, size);
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    return THINGINO_SUCCESS;
}

/**
 * Send partition marker ("ILOP" header, 172 bytes) to device.
 * Sliced from the descriptor at bytes 0x1C-0xC7.
 *
 * Vendor flow: FW_WRITE2 control (40 bytes, size=172) + bulk OUT (172 bytes).
 */
thingino_error_t flash_partition_marker_send(usb_device_t *device) {
    if (!device)
        return THINGINO_ERROR_INVALID_PARAMETER;

    uint8_t descriptor[FLASH_DESCRIPTOR_SIZE];
    if (flash_descriptor_create_t31x_writer_full(descriptor) != 0) {
        LOG_ERROR("Failed to generate flash descriptor for partition marker\n");
        return THINGINO_ERROR_FILE_IO;
    }

    const size_t marker_offset = 0x1C;
    const size_t marker_size = 0xAC; /* 172 bytes */

    DEBUG_PRINT("Sending partition marker (ILOP, %zu bytes)...\n", marker_size);

    thingino_error_t rc = send_fw_write2_bulk(device, descriptor + marker_offset, (uint32_t)marker_size);
    if (rc != THINGINO_SUCCESS)
        return rc;

    platform_sleep_ms(100);
    DEBUG_PRINT("Partition marker sent successfully\n");
    return THINGINO_SUCCESS;
}

/**
 * Send raw bulk partition marker for T10/T20/T21/T30.
 *
 * These platforms use DESC_RAW_BULK_THEN_SEND mode where the marker
 * is sent as a raw bulk OUT transfer without the VR_FW_WRITE2 control prefix.
 */
thingino_error_t flash_partition_marker_send_raw(usb_device_t *device) {
    if (!device)
        return THINGINO_ERROR_INVALID_PARAMETER;

    uint8_t descriptor[FLASH_DESCRIPTOR_SIZE];
    if (flash_descriptor_create_t31x_writer_full(descriptor) != 0) {
        LOG_ERROR("Failed to generate flash descriptor for partition marker\n");
        return THINGINO_ERROR_FILE_IO;
    }

    const size_t marker_offset = 0x1C;
    const size_t marker_size = 0xAC; /* 172 bytes */

    DEBUG_PRINT("Sending raw bulk partition marker (%zu bytes)...\n", marker_size);

    /* Ensure interface is claimed — required on Windows (WinUSB) for bulk transfers */
    libusb_claim_interface(device->handle, 0);

    /* Raw bulk OUT without control transfer */
    int transferred = 0;
    int result = libusb_bulk_transfer(device->handle, 0x01, descriptor + marker_offset, (int)marker_size, &transferred, 5000);
    if (result != 0 || transferred != (int)marker_size) {
        LOG_ERROR("Raw bulk marker transfer failed: %s, %d/%zu bytes\n", libusb_error_name(result), transferred, marker_size);
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    platform_sleep_ms(100);
    DEBUG_PRINT("Raw bulk partition marker sent successfully\n");
    return THINGINO_SUCCESS;
}

/**
 * Send flash descriptor (972 bytes) to device.
 *
 * Vendor flow: FW_WRITE2 control (40 bytes, size=972) + bulk OUT (972 bytes).
 */
thingino_error_t flash_descriptor_send_sized(usb_device_t *device, const uint8_t *descriptor, uint32_t size) {
    if (!device || !descriptor)
        return THINGINO_ERROR_INVALID_PARAMETER;

    DEBUG_PRINT("Sending flash descriptor (%u bytes)...\n", size);

    thingino_error_t rc = send_fw_write2_bulk(device, descriptor, size);
    if (rc != THINGINO_SUCCESS)
        return rc;

    DEBUG_PRINT("Flash descriptor sent successfully\n");
    return THINGINO_SUCCESS;
}

thingino_error_t flash_descriptor_send(usb_device_t *device, const uint8_t *descriptor) {
    return flash_descriptor_send_sized(device, descriptor, FLASH_DESCRIPTOR_SIZE);
}

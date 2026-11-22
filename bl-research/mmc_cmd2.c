/*
 * mmc_cmd2.c - SHV34 raw eMMC command tool (corrected v2)
 *
 * Fixes vs v1 (see bl-research/WP-AUDIT.md):
 *  - v1 read EXT_CSD[33] as USER_WP. Byte 33 is CACHE_CTRL.
 *    USER_WP is byte 171, BOOT_WP 173, HC_WP_GRP_SIZE 221,
 *    HC_ERASE_GRP_SIZE 224. (verified in the device's own OSS kernel
 *    include/linux/mmc/mmc.h)
 *  - v1 "wp" mode never started a data phase: it set CMD30 then
 *    overwrote the struct with CMD31 + flags 0x07 (R2, no ADTC).
 *    CMD30 SEND_WRITE_PROT   -> 4 bytes  (32 groups, 1 bit each)
 *    CMD31 SEND_WRITE_PROT_TYPE -> 8 bytes (32 groups, 2 bits each)
 *  - added raw CMD17/CMD24 single-block read/write so the block-layer
 *    write filter can be bypassed entirely.
 *
 * All raw commands require the Sharp ioctl lock to be bypassed first
 * (CONFIG_RW_PROTECT_EMMC_CUST_SH returns -EOPNOTSUPP when
 *  mmc_card_mmc(card) is true). Bypass = kctl5 patch card->type = 1.
 *
 * build:
 *   aarch64-linux-android23-clang -O2 -static -w -o mmc_cmd2 mmc_cmd2.c
 * usage:
 *   mmc_cmd2 r8                          dump the real EXT_CSD fields
 *   mmc_cmd2 wp  <group_addr_hex>        CMD30 + CMD31 with data phases
 *   mmc_cmd2 r1  <addr_hex> <outfile>    CMD17 read 512B
 *   mmc_cmd2 w1  <addr_hex> <infile>     CMD24 write 512B
 *   mmc_cmd2 cmd <op> <arg_hex> <flags_hex> [resp_only]
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/major.h>
#include <linux/mmc/ioctl.h>

#define RSP_R1_ADTC 0x35u   /* MMC_RSP_R1 (0x15) | MMC_CMD_ADTC (0x20) */

static int fd;

static int xfer(struct mmc_ioc_cmd *c, const char *tag) {
    int ret = ioctl(fd, MMC_IOC_CMD, c);
    if (ret) {
        printf("%s: ioctl_err=%d errno=%d(%s)\n", tag, ret, errno, strerror(errno));
        return ret;
    }
    printf("%s: resp %08x %08x %08x %08x\n", tag,
           c->response[0], c->response[1], c->response[2], c->response[3]);
    return 0;
}

static void hex(const char *tag, const unsigned char *p, int n) {
    int i;
    printf("%s:", tag);
    for (i = 0; i < n; i++) printf(" %02x", p[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    static unsigned char buf[512];
    struct mmc_ioc_cmd c;
    const char *path = "/dev/block/mmcblk0";

    fd = open(path, O_RDWR);
    if (fd < 0) { printf("open_err %s\n", strerror(errno)); return 1; }
    memset(&c, 0, sizeof(c));

    /* ---------- EXT_CSD ---------- */
    if (argc == 2 && !strcmp(argv[1], "r8")) {
        memset(buf, 0, sizeof(buf));
        c.opcode = 8; c.arg = 0; c.flags = RSP_R1_ADTC;
        c.blksz = 512; c.blocks = 1;
        mmc_ioc_cmd_set_data(c, buf);
        if (xfer(&c, "CMD8")) return 1;
        printf("EXT_CSD[171] USER_WP            = 0x%02x\n", buf[171]);
        printf("   bit0 US_PWR_WP_EN / bit2 US_PERM_WP_EN / bit3 US_PWR_WP_DIS / bit4 US_PERM_WP_DIS\n");
        printf("EXT_CSD[173] BOOT_WP            = 0x%02x\n", buf[173]);
        printf("EXT_CSD[174] BOOT_WP_STATUS     = 0x%02x\n", buf[174]);
        printf("EXT_CSD[221] HC_WP_GRP_SIZE     = 0x%02x (%u KiB)\n", buf[221], buf[221] * 512u);
        printf("EXT_CSD[224] HC_ERASE_GRP_SIZE  = 0x%02x\n", buf[224]);
        printf("EXT_CSD[175] ERASE_GROUP_DEF    = 0x%02x\n", buf[175]);
        return 0;
    }

    /* ---------- CMD30 / CMD31 with real data phases ---------- */
    if (argc == 3 && !strcmp(argv[1], "wp")) {
        unsigned int addr = (unsigned int)strtoul(argv[2], 0, 16);
        int i;

        memset(buf, 0, sizeof(buf));
        memset(&c, 0, sizeof(c));
        c.opcode = 30; c.arg = addr; c.flags = RSP_R1_ADTC;
        c.blksz = 4; c.blocks = 1;
        mmc_ioc_cmd_set_data(c, buf);
        if (xfer(&c, "CMD30")) return 1;
        hex("CMD30 SEND_WRITE_PROT bitmap", buf, 4);
        for (i = 0; i < 32; i++)
            if (buf[i / 8] & (1 << (i % 8))) printf("  group %d: PROTECTED\n", i);

        memset(buf, 0, sizeof(buf));
        memset(&c, 0, sizeof(c));
        c.opcode = 31; c.arg = addr; c.flags = RSP_R1_ADTC;
        c.blksz = 8; c.blocks = 1;
        mmc_ioc_cmd_set_data(c, buf);
        if (xfer(&c, "CMD31")) return 1;
        hex("CMD31 WRITE_PROT_TYPE", buf, 8);
        /* 2 bits per group: 0=none 1=temporary 2=power-on 3=permanent */
        for (i = 0; i < 32; i++) {
            unsigned int t = (buf[i / 4] >> ((i % 4) * 2)) & 3;
            if (t) printf("  group %d: type=%u (%s)\n", i, t,
                          t == 1 ? "temporary" : t == 2 ? "power-on" : "PERMANENT");
        }
        return 0;
    }

    /* ---------- CMD17 single block read ---------- */
    if (argc == 4 && !strcmp(argv[1], "r1")) {
        FILE *f;
        memset(buf, 0, sizeof(buf));
        c.opcode = 17; c.arg = (unsigned int)strtoul(argv[2], 0, 16);
        c.flags = RSP_R1_ADTC; c.blksz = 512; c.blocks = 1;
        mmc_ioc_cmd_set_data(c, buf);
        if (xfer(&c, "CMD17")) return 1;
        f = fopen(argv[3], "wb");
        if (!f) { printf("fopen_err %s\n", strerror(errno)); return 1; }
        fwrite(buf, 1, 512, f); fclose(f);
        hex("first 32B", buf, 32);
        return 0;
    }

    /* ---------- CMD24 single block write (bypass block layer) ---------- */
    if (argc == 4 && !strcmp(argv[1], "w1")) {
        FILE *f;
        size_t n;
        memset(buf, 0, sizeof(buf));
        f = fopen(argv[3], "rb");
        if (!f) { printf("fopen_err %s\n", strerror(errno)); return 1; }
        n = fread(buf, 1, 512, f); fclose(f);
        if (n != 512) { printf("need exactly 512 bytes, got %zu\n", n); return 1; }
        c.opcode = 24; c.arg = (unsigned int)strtoul(argv[2], 0, 16);
        c.flags = RSP_R1_ADTC; c.blksz = 512; c.blocks = 1;
        c.write_flag = 1;
        mmc_ioc_cmd_set_data(c, buf);
        if (xfer(&c, "CMD24")) return 1;
        printf("CMD24 issued, arg=0x%x\n", c.arg);
        return 0;
    }

    /* ---------- generic command (no data) ---------- */
    if (argc >= 5 && !strcmp(argv[1], "cmd")) {
        c.opcode = atoi(argv[2]);
        c.arg = (unsigned int)strtoul(argv[3], 0, 16);
        c.flags = (unsigned int)strtoul(argv[4], 0, 16);
        return xfer(&c, "CMD");
    }

    printf("bad args\n"); return 1;
}

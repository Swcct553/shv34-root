/*
 * rpmb_cmd.c - SHV34: send raw eMMC commands via MMC_IOC_RPMB_CMD
 *
 * WHY: the production kernel's CONFIG_RW_PROTECT_EMMC_CUST_SH adds
 *   if (card && mmc_card_mmc(card)) return -EOPNOTSUPP;
 * to mmc_blk_ioctl_cmd() (drivers/mmc/card/block.c:937), which blocks
 * MMC_IOC_CMD (errno 95).  mmc_blk_ioctl_rpmb_cmd() has NO such check
 * and copies opcode/arg/flags straight from userspace, so it is a
 * kernel-sanctioned raw-command path that needs no kernel R/W at all.
 *
 * Caveat: the rpmb ioctl switches the card to the RPMB area first
 * (mmc_blk_part_switch).  CMD8/CMD30/CMD31 are global/user-area
 * registers and work; for CMD17/CMD24 we prepend CMD6 SWITCH to
 * PARTITION_CONFIG (EXT_CSD[179]) value 0 = user area:
 *   arg = (0x03<<24)|(179<<16)|(0<<8)|1 = 0x03B30001, flags R1B 0x1d
 *
 * build: aarch64-linux-android23-clang -O2 -static -w -o rpmb_cmd rpmb_cmd.c
 * usage:
 *   rpmb_cmd r8                        -> EXT_CSD (real byte indices)
 *   rpmb_cmd wp  <group_addr_hex>      -> CMD30 (4B) + CMD31 (8B)
 *   rpmb_cmd r1  <addr_hex> <outfile>  -> CMD6 + CMD17 read 512B
 *   rpmb_cmd w1  <addr_hex> <infile>   -> CMD6 + CMD24 write 512B
 *   rpmb_cmd cmd <op> <arg_hex> <flags_hex> [blksz] -> single raw cmd
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/major.h>
#include <linux/mmc/ioctl.h>

/* NDK's uapi header omits the RPMB ioctl; take the layout from the
 * device's own kernel (include/uapi/linux/mmc/ioctl.h). */
#ifndef MMC_IOC_RPMB_CMD
#define MMC_IOC_MAX_RPMB_CMD 3
struct mmc_ioc_rpmb { struct mmc_ioc_cmd cmds[MMC_IOC_MAX_RPMB_CMD]; };
#define MMC_IOC_RPMB_CMD _IOWR(MMC_BLOCK_MAJOR, 0, struct mmc_ioc_rpmb)
#endif

#define RSP_R1_ADTC 0x35u   /* MMC_RSP_R1 | MMC_CMD_ADTC */
#define RSP_R1B     0x1du   /* MMC_RSP_R1B | MMC_CMD_AC  */
#define SWITCH_USER 0x03B30001u  /* CMD6: PARTITION_CONFIG -> user area */

static int fd;
static unsigned char b0[512], b1[512], b2[512];

static void hex(const char *tag, const unsigned char *p, int n) {
    int i; printf("%s:", tag);
    for (i = 0; i < n; i++) printf(" %02x", p[i]);
    printf("\n");
}
static void resp(const char *tag, struct mmc_ioc_cmd *c) {
    printf("%s resp: %08x %08x %08x %08x\n", tag,
           c->response[0], c->response[1], c->response[2], c->response[3]);
}
static void setup(struct mmc_ioc_cmd *c, int op, unsigned int arg,
                  unsigned int flags, unsigned int blksz, int wflag,
                  unsigned char *buf) {
    memset(c, 0, sizeof(*c));
    c->opcode = op; c->arg = arg; c->flags = flags;
    if (blksz) {
        c->blksz = blksz; c->blocks = 1; c->write_flag = wflag;
        c->data_ptr = (uint64_t)(unsigned long)buf;
    }
}

int main(int argc, char **argv) {
    struct mmc_ioc_rpmb r;
    int ret, i, ncmd = 0;

    fd = open("/dev/block/mmcblk0rpmb", O_RDWR);
    if (fd < 0) { printf("open_err %s\n", strerror(errno)); return 1; }
    memset(&r, 0, sizeof(r));
    memset(b0, 0, sizeof(b0)); memset(b1, 0, sizeof(b1)); memset(b2, 0, sizeof(b2));

    /* ---------- CMD8 SEND_EXT_CSD ---------- */
    if (argc == 2 && !strcmp(argv[1], "r8")) {
        setup(&r.cmds[0], 8, 0, RSP_R1_ADTC, 512, 0, b0); ncmd = 1;
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD8", &r.cmds[0]);
        hex("EXT_CSD[0..15]", b0, 16);
        printf("EXT_CSD[59]  CLASS_6_CTRL       = 0x%02x\n", b0[59]);
        printf("EXT_CSD[168] RPMB_MULT          = 0x%02x\n", b0[168]);
        printf("EXT_CSD[171] USER_WP           = 0x%02x\n", b0[171]);
        printf("EXT_CSD[172] BOOT_WP_STATUS    = 0x%02x\n", b0[172]);
        printf("   bit0 US_PWR_WP_EN / bit2 US_PERM_WP_EN / bit3 US_PWR_WP_DIS / bit4 US_PERM_WP_DIS\n");
        printf("EXT_CSD[173] BOOT_WP           = 0x%02x\n", b0[173]);
        printf("EXT_CSD[174] BOOT_WP_STATUS    = 0x%02x\n", b0[174]);
        printf("EXT_CSD[175] ERASE_GROUP_DEF   = 0x%02x\n", b0[175]);
        printf("EXT_CSD[179] PART_CONFIG       = 0x%02x\n", b0[179]);
        printf("EXT_CSD[192] REV               = 0x%02x\n", b0[192]);
        printf("EXT_CSD[221] HC_WP_GRP_SIZE    = 0x%02x (%u KiB)\n", b0[221], b0[221]*512u);
        printf("EXT_CSD[224] HC_ERASE_GRP_SIZE = 0x%02x\n", b0[224]);
        return 0;
    }

    /* ---------- CMD6 + CMD30 + CMD31, real data phases ----------
     * blksz must be 512 on this host: the old MMC_IOC_CMD attempt with
     * blksz=4 timed out (errno 110). The card only returns 4B/8B of
     * meaningful data; the rest of the 512B block is padding. */
    if (argc == 3 && !strcmp(argv[1], "wp")) {
        unsigned int a = (unsigned int)strtoul(argv[2], 0, 16);
        setup(&r.cmds[0], 6, SWITCH_USER, RSP_R1B, 0, 0, NULL);
        setup(&r.cmds[1], 30, a, RSP_R1_ADTC, 512, 0, b1);
        setup(&r.cmds[2], 31, a, RSP_R1_ADTC, 512, 0, b2);
        ncmd = 3;
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s) ncmd=%d\n", ret, errno, strerror(errno), ncmd);
        resp("CMD6",  &r.cmds[0]);
        resp("CMD30", &r.cmds[1]); hex("CMD30 SEND_WRITE_PROT (first 8B)", b1, 8);
        resp("CMD31", &r.cmds[2]); hex("CMD31 WRITE_PROT_TYPE (first 16B)", b2, 16);
        for (i = 0; i < 32; i++) {
            int wp = (b1[i/8] >> (i%8)) & 1;
            int t  = (b2[i/4] >> ((i%4)*2)) & 3;
            if (wp || t) printf("  group %2d: wp=%d type=%d (%s)\n", i, wp, t,
                t == 1 ? "temporary" : t == 2 ? "power-on" : t == 3 ? "PERMANENT" : "none");
        }
        return 0;
    }

    /* ---------- CMD6 + CMD29 / CMD28 : clear / set write protect ---------- */
    if (argc == 3 && (!strcmp(argv[1], "clr") || !strcmp(argv[1], "set"))) {
        unsigned int a = (unsigned int)strtoul(argv[2], 0, 16);
        int op = !strcmp(argv[1], "clr") ? 29 : 28;
        setup(&r.cmds[0], 6, SWITCH_USER, RSP_R1B, 0, 0, NULL);
        setup(&r.cmds[1], op, a, RSP_R1B, 0, 0, NULL);
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD6", &r.cmds[0]); resp(op == 29 ? "CMD29" : "CMD28", &r.cmds[1]);
        printf("arg=0x%08x\n", a);
        return 0;
    }

    /* ---------- CMD6 + CMD24 + CMD13 : look for R1_WP_VIOLATION ----------
     * If the card itself refuses the write because the group is write
     * protected, CMD13 status carries bit26 (0x04000000).  If the status
     * is clean, the write was dropped somewhere below the card. */
    if (argc == 4 && !strcmp(argv[1], "probe")) {
        FILE *f; size_t n;
        f = fopen(argv[3], "rb");
        if (!f) { printf("fopen_err %s\n", strerror(errno)); return 1; }
        n = fread(b1, 1, 512, f); fclose(f);
        if (n != 512) { printf("need 512 bytes, got %zu\n", n); return 1; }
        setup(&r.cmds[0], 6,  SWITCH_USER, RSP_R1B, 0, 0, NULL);
        setup(&r.cmds[1], 24, (unsigned int)strtoul(argv[2], 0, 16), RSP_R1_ADTC, 512, 1, b1);
        setup(&r.cmds[2], 13, 0x00010000u, 0x15u, 0, 0, NULL);
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD6",  &r.cmds[0]);
        resp("CMD24", &r.cmds[1]);
        resp("CMD13", &r.cmds[2]);
        printf("CMD13 status=0x%08x  WP_VIOLATION(bit26)=%d  WP_ERASE_SKIP(bit15)=%d\n",
               r.cmds[2].response[0],
               !!(r.cmds[2].response[0] & 0x04000000u),
               !!(r.cmds[2].response[0] & 0x00008000u));
        return 0;
    }

    /* ---------- CMD6 + arbitrary write opcode (24/25/...) ---------- */
    if (argc >= 5 && !strcmp(argv[1], "wraw")) {
        FILE *f; size_t n;
        int op = atoi(argv[4]);
        f = fopen(argv[3], "rb");
        if (!f) { printf("fopen_err %s\n", strerror(errno)); return 1; }
        n = fread(b1, 1, 512, f); fclose(f);
        if (n != 512) { printf("need 512 bytes, got %zu\n", n); return 1; }
        setup(&r.cmds[0], 6, SWITCH_USER, RSP_R1B, 0, 0, NULL);
        setup(&r.cmds[1], op, (unsigned int)strtoul(argv[2], 0, 16), RSP_R1_ADTC, 512, 1, b1);
        /* optional 6th arg "rel": set bit31 in write_flag so the RPMB
         * path calls mmc_set_blockcount(..., is_rel_write=1) -> CMD23
         * with the reliable-write bit, a different card write path. */
        if (argc >= 6 && !strcmp(argv[5], "rel"))
            r.cmds[1].write_flag |= (1 << 31);
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD6", &r.cmds[0]); printf("CMD%d", op); resp("", &r.cmds[1]);
        return 0;
    }

    /* ---------- CMD6 + CMD17 read ---------- */
    if (argc == 4 && !strcmp(argv[1], "r1")) {
        FILE *f;
        setup(&r.cmds[0], 6, SWITCH_USER, RSP_R1B, 0, 0, NULL);
        setup(&r.cmds[1], 17, (unsigned int)strtoul(argv[2], 0, 16), RSP_R1_ADTC, 512, 0, b1);
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD6", &r.cmds[0]); resp("CMD17", &r.cmds[1]);
        f = fopen(argv[3], "wb"); if (f) { fwrite(b1, 1, 512, f); fclose(f); }
        hex("first 32B", b1, 32);
        return 0;
    }

    /* ---------- CMD6 + CMD24 write ---------- */
    if (argc == 4 && !strcmp(argv[1], "w1")) {
        FILE *f; size_t n;
        f = fopen(argv[3], "rb");
        if (!f) { printf("fopen_err %s\n", strerror(errno)); return 1; }
        n = fread(b1, 1, 512, f); fclose(f);
        if (n != 512) { printf("need 512 bytes, got %zu\n", n); return 1; }
        setup(&r.cmds[0], 6, SWITCH_USER, RSP_R1B, 0, 0, NULL);
        setup(&r.cmds[1], 24, (unsigned int)strtoul(argv[2], 0, 16), RSP_R1_ADTC, 512, 1, b1);
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD6", &r.cmds[0]); resp("CMD24", &r.cmds[1]);
        printf("CMD24 arg=0x%08x\n", r.cmds[1].arg);
        return 0;
    }

    /* ---------- generic single command ---------- */
    if (argc >= 5 && !strcmp(argv[1], "cmd")) {
        unsigned int blksz = (argc >= 6) ? (unsigned int)strtoul(argv[5], 0, 0) : 0;
        setup(&r.cmds[0], atoi(argv[2]), (unsigned int)strtoul(argv[3], 0, 16),
              (unsigned int)strtoul(argv[4], 0, 16), blksz, 0, b0);
        ret = ioctl(fd, MMC_IOC_RPMB_CMD, &r);
        printf("ioctl ret=%d errno=%d(%s)\n", ret, errno, strerror(errno));
        resp("CMD", &r.cmds[0]);
        if (blksz) hex("data", b0, blksz > 32 ? 32 : (int)blksz);
        return 0;
    }

    printf("bad args\n"); return 1;
}

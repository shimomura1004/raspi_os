/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "peripherals/base.h"
#include "peripherals/gpio.h"
#include "debug.h"
#include "delays.h"
#include "sd.h"
#include "spinlock.h"
#include "utils.h"

// SD カードにアクセスするための規格を SDHCI と呼ぶ
// SDHOST とは SDHCI 規格に準拠した標準的な実装を指す
// SD カードへのアクセスモードは主に SD モード(Native mode)と SPI モードの2つがある
//   Native mode が現代の標準的なモードで、4ビット幅のバスを使い高速なアクセスが可能
//   SPI モードはシリアル通信になるため低速だが実装が容易で組み込みシステムでよく使われる

// Raspberry Pi には BCM2835-sdhost と、Arasan のコントローラの2つが搭載されている
//   デフォルトでは SD カードは Arasan のコントローラに接続されている
//   Arasan のコントローラは WiFi モジュールに搭載された eMMC を読み取る用途で使われているが、
//     SD カードを読み取る用途でも使うことができる
//   BCM2835 の SDHOST は SPI で SDHCI のコマンドを受け取るような実装になっている？
// どちらも SDHCI 規格に準拠しているため、SD カードへのコマンドは共通

// このコードでは C0_SPI_MODE_EN フラグを使っていないことから、
// Native モードで SD カードにアクセスしていると思われる

// Raspberry Pi 3 での MMIO アドレス
// BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
// 5. External Mass Media Controller
#define EMMC_ARG2           (PBASE + 0x00300000)
#define EMMC_BLKSIZECNT     (PBASE + 0x00300004)
#define EMMC_ARG1           (PBASE + 0x00300008)
#define EMMC_CMDTM          (PBASE + 0x0030000C)
#define EMMC_RESP0          (PBASE + 0x00300010)
#define EMMC_RESP1          (PBASE + 0x00300014)
#define EMMC_RESP2          (PBASE + 0x00300018)
#define EMMC_RESP3          (PBASE + 0x0030001C)
#define EMMC_DATA           (PBASE + 0x00300020)
#define EMMC_STATUS         (PBASE + 0x00300024)
#define EMMC_CONTROL0       (PBASE + 0x00300028)
#define EMMC_CONTROL1       (PBASE + 0x0030002C)
#define EMMC_INTERRUPT      (PBASE + 0x00300030)
#define EMMC_INT_MASK       (PBASE + 0x00300034)
#define EMMC_INT_EN         (PBASE + 0x00300038)
#define EMMC_CONTROL2       (PBASE + 0x0030003C)
#define EMMC_SLOTISR_VER    (PBASE + 0x003000FC)

// command flags
#define CMD_NEED_APP        0x80000000
#define CMD_RSPNS_48        0x00020000
#define CMD_ERRORS_MASK     0xfff9c004
#define CMD_RCA_MASK        0xffff0000

// COMMANDs
// SPI Mode でのコマンド一覧
// http://elm-chan.org/docs/mmc/mmc_e.html
#define CMD_GO_IDLE         0x00000000  // Software reset
#define CMD_ALL_SEND_CID    0x02010000  // ?
#define CMD_SEND_REL_ADDR   0x03020000  // ?
#define CMD_CARD_SELECT     0x07030000  // ?
#define CMD_SEND_IF_COND    0x08020000  // For only SDC V2, Check voltage range.
#define CMD_STOP_TRANS      0x0C030000  // Stop to read data
#define CMD_READ_SINGLE     0x11220010  // Read a block
#define CMD_READ_MULTI      0x12220032  // Read multiple blocks
#define CMD_SET_BLOCKCNT    0x17020000  // For only MMC. Define number of blocks to transfer
                                        // with next multi-block read/write command.
#define CMD_APP_CMD         0x37000000  // Leading command of ACMD<n> command
#define CMD_SET_BUS_WIDTH   (0x06020000 | CMD_NEED_APP)
#define CMD_SEND_OP_COND    (0x29020000 | CMD_NEED_APP)
#define CMD_SEND_SCR        (0x33220010 | CMD_NEED_APP)

// STATUS register settings
#define SR_READ_AVAILABLE   0x00000800
#define SR_DAT_INHIBIT      0x00000002
#define SR_CMD_INHIBIT      0x00000001
#define SR_APP_CMD          0x00000020

// INTERRUPT register settings
#define INT_DATA_TIMEOUT    0x00100000
#define INT_CMD_TIMEOUT     0x00010000
#define INT_READ_RDY        0x00000020
#define INT_CMD_DONE        0x00000001

#define INT_ERROR_MASK      0x017E8000

// CONTROL register settings
#define C0_SPI_MODE_EN      0x00100000
#define C0_HCTL_HS_EN       0x00000004
#define C0_HCTL_DWITDH      0x00000002

#define C1_SRST_DATA        0x04000000
#define C1_SRST_CMD         0x02000000
#define C1_SRST_HC          0x01000000
#define C1_TOUNIT_DIS       0x000f0000
#define C1_TOUNIT_MAX       0x000e0000
#define C1_CLK_GENSEL       0x00000020
#define C1_CLK_EN           0x00000004
#define C1_CLK_STABLE       0x00000002
#define C1_CLK_INTLEN       0x00000001

// SLOTISR_VER values
#define HOST_SPEC_NUM       0x00ff0000
#define HOST_SPEC_NUM_SHIFT 16
#define HOST_SPEC_V3        2
#define HOST_SPEC_V2        1
#define HOST_SPEC_V1        0

// SCR flags
#define SCR_SD_BUS_WIDTH_4  0x00000400
#define SCR_SUPP_SET_BLKCNT 0x02000000
// added by my driver
#define SCR_SUPP_CCS        0x00000001

#define ACMD41_VOLTAGE      0x00ff8000
#define ACMD41_CMD_COMPLETE 0x80000000
#define ACMD41_CMD_CCS      0x40000000
#define ACMD41_ARG_HC       0x51ff8000

unsigned long sd_scr[2], sd_ocr, sd_rca, sd_err, sd_hv;

// SD カードにアクセスするときのロック
// たとえば EMMC_INTERRUPT などのフラグはグローバルなので、それを触る処理はスレッドセーフではない
struct spinlock sd_lock;

/**
 * Wait for data or command ready
 */
int sd_status(unsigned int mask)
{
    int cnt = 500000;
    while ((get32(EMMC_STATUS) & mask) && !(get32(EMMC_INTERRUPT) & INT_ERROR_MASK) && cnt--) {
        wait_msec_st(1);
    }
    return (cnt <= 0 || (get32(EMMC_INTERRUPT) & INT_ERROR_MASK)) ? SD_ERROR : SD_OK;
}

/**
 * Wait for interrupt
 */
int sd_int(unsigned int mask)
{
    unsigned int r, m = mask | INT_ERROR_MASK;
    int cnt = 1000000;
    while (!(get32(EMMC_INTERRUPT) & m) && cnt--) {
        wait_msec_st(1);
    }
    r = get32(EMMC_INTERRUPT);
    if (cnt <= 0 || (r & INT_CMD_TIMEOUT) || (r & INT_DATA_TIMEOUT) ) {
        put32(EMMC_INTERRUPT, r);
        return SD_TIMEOUT;
    }
    else if (r & INT_ERROR_MASK) {
        put32(EMMC_INTERRUPT, r);
        return SD_ERROR;
    }
    put32(EMMC_INTERRUPT, mask);
    return 0;
}

/**
 * Send a command
 */
int sd_cmd(unsigned int code, unsigned int arg)
{
    int r = 0;
    sd_err = SD_OK;
    if (code & CMD_NEED_APP) {
        r = sd_cmd(CMD_APP_CMD | (sd_rca ? CMD_RSPNS_48 : 0), sd_rca);
        if (sd_rca && !r) {
            WARN("ERROR: failed to send SD APP command");
            sd_err = SD_ERROR;
            return 0;
        }
        code &= ~CMD_NEED_APP;
    }
    if (sd_status(SR_CMD_INHIBIT)) {
        WARN("ERROR: EMMC busy");
        sd_err = SD_TIMEOUT;
        return 0;
    }
    // INFO("EMMC: Sending command %x arg %x", code, arg);
    put32(EMMC_INTERRUPT, get32(EMMC_INTERRUPT));
    put32(EMMC_ARG1, arg);
    put32(EMMC_CMDTM, code);
    if (code == CMD_SEND_OP_COND) {
        wait_msec_st(1000);
    }
    else if (code == CMD_SEND_IF_COND || code == CMD_APP_CMD) {
        wait_msec_st(100);
    }

    if ((r = sd_int(INT_CMD_DONE))) {
        WARN("ERROR: failed to send EMMC command(%d)", r);
        sd_err = r;
        return 0;
    }

    r = get32(EMMC_RESP0);

    if (code == CMD_GO_IDLE || code == CMD_APP_CMD) {
        return 0;
    }
    else if (code == (CMD_APP_CMD | CMD_RSPNS_48)) {
        return r & SR_APP_CMD;
    }
    else if (code == CMD_SEND_OP_COND) {
        return r;
    }
    else if (code == CMD_SEND_IF_COND) {
        return r == arg ? SD_OK : SD_ERROR;
    }
    else if (code == CMD_ALL_SEND_CID) {
        r |= get32(EMMC_RESP3);
        r |= get32(EMMC_RESP2);
        r |= get32(EMMC_RESP1);
        return r;
    }
    else if (code == CMD_SEND_REL_ADDR) {
        sd_err = (((r & 0x1fff)) | ((r & 0x2000) << 6) | 
                  ((r & 0x4000) << 8) | ((r & 0x8000) << 8)) & CMD_ERRORS_MASK;
        return r & CMD_RCA_MASK;
    }
    return r & CMD_ERRORS_MASK;
    // make gcc happy
    return 0;
}

/**
 * read a block from sd card and return the number of bytes read
 * returns 0 on error.
 */
int sd_readblock(unsigned int lba, unsigned char *buffer, unsigned int num) {
    acquire_lock(&sd_lock);

    int r, c = 0, d;
    if (num < 1) {
        num = 1;
    }
    // INFO("sd_readblock lba %x num %x", lba, num);
    if (sd_status(SR_DAT_INHIBIT)) {
        sd_err = SD_TIMEOUT;
        release_lock(&sd_lock);
        return 0;
    }
    unsigned int *buf = (unsigned int *)buffer;
    if (sd_scr[0] & SCR_SUPP_CCS) {
        if (num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT)) {
            sd_cmd(CMD_SET_BLOCKCNT, num);
            if (sd_err) {
                release_lock(&sd_lock);
                return 0;
            }
        }
        put32(EMMC_BLKSIZECNT, (num << 16) | 512);
        sd_cmd(num == 1 ? CMD_READ_SINGLE : CMD_READ_MULTI, lba);
        if (sd_err) {
            release_lock(&sd_lock);
            return 0;
        }
    } else {
        put32(EMMC_BLKSIZECNT, (1 << 16) | 512);
    }
    while (c < num) {
        if (!(sd_scr[0] & SCR_SUPP_CCS)) {
            sd_cmd(CMD_READ_SINGLE, (lba + c) * 512);
            if (sd_err) {
                release_lock(&sd_lock);
                return 0;
            }
        }
        if ((r = sd_int(INT_READ_RDY))) {
            WARN("ERROR: Timeout waiting for ready to read");
            sd_err = r;
            release_lock(&sd_lock);
            return 0;
        }
        for (d = 0; d < 128; d++) {
            buf[d] = get32(EMMC_DATA);
        }
        c++;
        buf += 128;
    }
    if (num > 1 && !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) &&
        (sd_scr[0] & SCR_SUPP_CCS)) {
        sd_cmd(CMD_STOP_TRANS, 0);
    }

    release_lock(&sd_lock);

    return sd_err != SD_OK || c != num ? 0 : num * 512;
}

/**
 * set SD clock to frequency in Hz
 */
int sd_clk(unsigned int f) {
    unsigned int d, c = 41666666 / f, x, s = 32, h = 0;
    int cnt = 100000;
    while ((get32(EMMC_STATUS) & (SR_CMD_INHIBIT | SR_DAT_INHIBIT)) && cnt--) {
        wait_msec_st(1);
    }
    if (cnt <= 0) {
        WARN("ERROR: timeout waiting for inhibit flag");
        return SD_ERROR;
    }

    put32(EMMC_CONTROL1, get32(EMMC_CONTROL1) & ~C1_CLK_EN);
    wait_msec_st(10);
    x = c - 1;
    if (!x) {
        s = 0;
    }
    else {
        if (!(x & 0xffff0000u)) {
            x <<= 16;
            s -= 16;
        }
        if (!(x & 0xff000000u)) {
            x <<= 8;
            s -= 8;
        }
        if (!(x & 0xf0000000u)) {
            x <<= 4;
            s -= 4;
        }
        if (!(x & 0xc0000000u)) {
            x <<= 2;
            s -= 2;
        }
        if (!(x & 0x80000000u)) {
            x <<= 1;
            s -= 1;
        }
        if (s > 0) {
            s--;
        }
        if (s > 7) {
            s = 7;
        }
    }
    if (sd_hv > HOST_SPEC_V2) {
        d = c;
    }
    else {
        d = (1 << s);
    }
    if (d <= 2) {
        d = 2;
        s = 0;
    }
    // INFO("sd_clk divisor %x, shift %x", d, s);
    if (sd_hv > HOST_SPEC_V2) {
        h = (d & 0x300) >> 2;
    }
    d = (((d & 0x0ff) << 8) | h);
    put32(EMMC_CONTROL1, (get32(EMMC_CONTROL1) & 0xffff003f) | d);
    wait_msec_st(10);
    put32(EMMC_CONTROL1, get32(EMMC_CONTROL1) | C1_CLK_EN);
    wait_msec_st(10);
    cnt = 10000;
    while (!(get32(EMMC_CONTROL1) & C1_CLK_STABLE) && cnt--) {
        wait_msec_st(10);
    }
    if (cnt <= 0) {
        WARN("ERROR: failed to get stable clock");
        return SD_ERROR;
    }
    return SD_OK;
}

/**
 * initialize EMMC to read SDHC card
 */
int sd_init() {
    long r, cnt, ccs = 0;

    init_lock(&sd_lock, "sd_lock");

    // GPIO_CD
    r = get32(GPFSEL4);
    r &= ~(7 << (7 * 3));
    put32(GPFSEL4, r);
    put32(GPPUD, 2);
    wait_cycles(150);
    put32(GPPUDCLK1, (1) << 15);
    wait_cycles(150);
    put32(GPPUD, 0);
    put32(GPPUDCLK1, 0);
    r = get32(GPHEN1);
    r |= 1 << 15;
    put32(GPHEN1, r);

    // GPIO_CLK, GPIO_CMD
    r = get32(GPFSEL4);
    r |= (7 << (8 * 3)) | (7 << (9 * 3));
    put32(GPFSEL4, r);
    put32(GPPUD, 2);
    wait_cycles(150);
    put32(GPPUDCLK1, (1 << 16) | (1 << 17));
    wait_cycles(150);
    put32(GPPUD, 0);
    put32(GPPUDCLK1, 0);

    // GPIO_DAT0, GPIO_DAT1, GPIO_DAT2, GPIO_DAT3
    r = get32(GPFSEL5);
    r |= (7 << (0 * 3)) | (7 << (1 * 3)) | (7 << (2 * 3)) | (7 << (3 * 3));
    put32(GPFSEL5, r);
    put32(GPPUD, 2);
    wait_cycles(150);
    put32(GPPUDCLK1, (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21));
    wait_cycles(150);
    put32(GPPUD, 0);
    put32(GPPUDCLK1, 0);

    sd_hv = (get32(EMMC_SLOTISR_VER) & HOST_SPEC_NUM) >> HOST_SPEC_NUM_SHIFT;
    // INFO("EMMC: GPIO set up");
    // Reset the card.
    put32(EMMC_CONTROL0, 0);
    put32(EMMC_CONTROL1, get32(EMMC_CONTROL1) | C1_SRST_HC);
    cnt = 10000;
    do {
        wait_msec_st(10);
    } while ((get32(EMMC_CONTROL1) & C1_SRST_HC) && cnt--);
    if (cnt <= 0) {
        WARN("ERROR: failed to reset EMMC");
        return SD_ERROR;
    }
    // INFO("EMMC: reset OK");
    put32(EMMC_CONTROL1, get32(EMMC_CONTROL1) | C1_CLK_INTLEN | C1_TOUNIT_MAX);
    wait_msec_st(10);
    // Set clock to setup frequency.
    if ((r = sd_clk(400000))) {
        return r;
    }
    put32(EMMC_INT_EN, 0xffffffff);
    put32(EMMC_INT_MASK, 0xffffffff);
    sd_scr[0] = sd_scr[1] = sd_rca = sd_err = 0;
    sd_cmd(CMD_GO_IDLE, 0);
    if (sd_err) {
        return sd_err;
    }

    sd_cmd(CMD_SEND_IF_COND, 0x000001AA);
    if (sd_err) {
        return sd_err;
    }
    cnt = 6;
    r = 0;
    while (!(r & ACMD41_CMD_COMPLETE) && cnt--) {
        wait_cycles(400);
        r = sd_cmd(CMD_SEND_OP_COND, ACMD41_ARG_HC);
        // const char *ret = "";
        // if (r & ACMD41_CMD_COMPLETE) {
        //     ret = "COMPLETE";
        // }
        // if (r & ACMD41_VOLTAGE) {
        //     ret = "VOLTAGE";
        // }
        // if (r & ACMD41_CMD_CCS) {
        //     ret = "CCS";
        // }
        // INFO("EMMC: CMD_SEND_OP_COND returned %s %x %x", ret, r >> 32, r);
        if (sd_err != SD_TIMEOUT && sd_err != SD_OK) {
            WARN("ERROR: EMMC ACMD41 returned error");
            return sd_err;
        }
    }
    if (!(r & ACMD41_CMD_COMPLETE) || !cnt) {
        return SD_TIMEOUT;
    }
    if (!(r & ACMD41_VOLTAGE)) {
        return SD_ERROR;
    }
    if (r & ACMD41_CMD_CCS) {
        ccs = SCR_SUPP_CCS;
    }

    sd_cmd(CMD_ALL_SEND_CID, 0);

    sd_rca = sd_cmd(CMD_SEND_REL_ADDR, 0);
    // INFO("EMMC: CMD_SEND_REL_ADDR returned %x %x", sd_rca >> 32, sd_rca);
    if (sd_err) {
        return sd_err;
    }

    if ((r = sd_clk(25000000))) {
        return r;
    }

    sd_cmd(CMD_CARD_SELECT, sd_rca);
    if (sd_err) {
        return sd_err;
    }

    if (sd_status(SR_DAT_INHIBIT)) {
        return SD_TIMEOUT;
    }
    put32(EMMC_BLKSIZECNT, (1 << 16) | 8);
    sd_cmd(CMD_SEND_SCR, 0);
    if (sd_err) {
        return sd_err;
    }
    if (sd_int(INT_READ_RDY)) {
        return SD_TIMEOUT;
    }

    r = 0;
    cnt = 100000;
    while (r < 2 && cnt) {
        if (get32(EMMC_STATUS) & SR_READ_AVAILABLE) {
            sd_scr[r++] = get32(EMMC_DATA);
        }
        else {
            wait_msec_st(1);
        }
    }
    if (r != 2) {
        return SD_TIMEOUT;
    }
    if (sd_scr[0] & SCR_SD_BUS_WIDTH_4) {
        sd_cmd(CMD_SET_BUS_WIDTH, sd_rca | 2);
        if (sd_err) {
            return sd_err;
        }
        put32(EMMC_CONTROL0, get32(EMMC_CONTROL0) | C0_HCTL_DWITDH);
    }
    // add software flag
    // const char *suppstr = "";
    // if (sd_scr[0] & SCR_SUPP_SET_BLKCNT) {
    //     suppstr = "SET_BLKCNT ";
    // }
    // if (ccs) {
    //     suppstr = "CCS ";
    // }
    // INFO("EMMC: supports %s", suppstr);
    sd_scr[0] &= ~SCR_SUPP_CCS;
    sd_scr[0] |= ccs;
    return SD_OK;
}

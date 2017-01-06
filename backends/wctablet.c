/*
 * QEMU Wacome serial tablet emulation
 *
 * Copyright (c) 2008 Lubomir Rintel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/char.h"
#include "ui/console.h"
#include "ui/input.h"
#include "trace.h"


#define WC_OUTPUT_BUF_MAX_LEN 512
#define WC_COMMAND_MAX_LEN 60

#define WC_L7(n) ((n) & 127)
#define WC_M7(n) (((n) >> 7) & 127)
#define WC_H2(n) ((n) >> 14)

#define WC_L4(n) ((n) & 15)
#define WC_H4(n) (((n) >> 4) & 15)

/* Model string and config string */
#define WC_MODEL_STRING_LENGTH 18
uint8_t WC_MODEL_STRING[WC_MODEL_STRING_LENGTH + 1] = "~#CT-0045R,V1.3-5,";

#define WC_CONFIG_STRING_LENGTH 8
uint8_t WC_CONFIG_STRING[WC_CONFIG_STRING_LENGTH + 1] = "96,N,8,0";

#define WC_FULL_CONFIG_STRING_LENGTH 61
uint8_t WC_FULL_CONFIG_STRING[WC_FULL_CONFIG_STRING_LENGTH + 1] = {
    0x5c, 0x39, 0x36, 0x2c, 0x4e, 0x2c, 0x38, 0x2c,
    0x31, 0x28, 0x01, 0x24, 0x57, 0x41, 0x43, 0x30,
    0x30, 0x34, 0x35, 0x5c, 0x5c, 0x50, 0x45, 0x4e, 0x5c,
    0x57, 0x41, 0x43, 0x30, 0x30, 0x30, 0x30, 0x5c,
    0x54, 0x61, 0x62, 0x6c, 0x65, 0x74, 0x0d, 0x0a,
    0x43, 0x54, 0x2d, 0x30, 0x30, 0x34, 0x35, 0x52,
    0x2c, 0x56, 0x31, 0x2e, 0x33, 0x2d, 0x35, 0x0d,
    0x0a, 0x45, 0x37, 0x29
};

/* This structure is used to save private info for Wacom Tablet. */
typedef struct {
    CharDriverState *chr;

    /* Query string from serial */
    uint8_t query[100];
    int query_index;

    /* Command to be sent to serial port */
    uint8_t outbuf[WC_OUTPUT_BUF_MAX_LEN];
    int outlen;

    int line_speed;
} TabletState;

static void wctablet_chr_accept_input(CharDriverState *chr);

static void wctablet_shift_input(TabletState *tablet, int count)
{
    tablet->query_index -= count;
    memmove(tablet->query, tablet->query + count, tablet->query_index);
    tablet->query[tablet->query_index] = 0;
}

static void wctablet_queue_output(TabletState *tablet, uint8_t *buf, int count)
{
    if (tablet->outlen + count > sizeof(tablet->outbuf)) {
        return;
    }

    memcpy(tablet->outbuf + tablet->outlen, buf, count);
    tablet->outlen += count;
    wctablet_chr_accept_input(tablet->chr);
}

static void wctablet_reset(TabletState *tablet)
{
    /* clear buffers */
    tablet->query_index = 0;
    tablet->outlen = 0;
}

static void wctablet_event(void *opaque, int x,
                           int y, int dz, int buttons_state)
{
    CharDriverState *chr = (CharDriverState *) opaque;
    TabletState *tablet = (TabletState *) chr->opaque;
    uint8_t codes[8] = { 0xe0, 0, 0, 0, 0, 0, 0 };

    if (tablet->line_speed != 9600) {
        return;
    }

    int newX = x * 0.1537;
    int nexY = y * 0.1152;

    codes[0] = codes[0] | WC_H2(newX);
    codes[1] = codes[1] | WC_M7(newX);
    codes[2] = codes[2] | WC_L7(newX);

    codes[3] = codes[3] | WC_H2(nexY);
    codes[4] = codes[4] | WC_M7(nexY);
    codes[5] = codes[5] | WC_L7(nexY);

    if (buttons_state == 0x01) {
        codes[0] = 0xa0;
    }

    wctablet_queue_output(tablet, codes, 7);
}

static void wctablet_chr_accept_input(CharDriverState *chr)
{
    TabletState *tablet = (TabletState *) chr->opaque;
    int len, canWrite;

    canWrite = qemu_chr_be_can_write(chr);
    len = canWrite;
    if (len > tablet->outlen) {
        len = tablet->outlen;
    }

    if (len) {
        qemu_chr_be_write(chr, tablet->outbuf, len);
        tablet->outlen -= len;
        if (tablet->outlen) {
            memmove(tablet->outbuf, tablet->outbuf + len, tablet->outlen);
        }
    }
}

static int wctablet_chr_write(struct CharDriverState *s,
                              const uint8_t *buf, int len)
{
    TabletState *tablet = (TabletState *) s->opaque;
    unsigned int i, clen;
    char *pos;

    if (tablet->line_speed != 9600) {
        return len;
    }
    for (i = 0; i < len && tablet->query_index < sizeof(tablet->query) - 1; i++) {
        tablet->query[tablet->query_index++] = buf[i];
    }
    tablet->query[tablet->query_index] = 0;

    while (tablet->query_index > 0 && (tablet->query[0] == '@'  ||
                                       tablet->query[0] == '\r' ||
                                       tablet->query[0] == '\n')) {
        wctablet_shift_input(tablet, 1);
    }
    if (!tablet->query_index) {
        return len;
    }

    if (strncmp((char *)tablet->query, "~#", 2) == 0) {
        /* init / detect sequence */
        trace_wct_init();
        wctablet_shift_input(tablet, 2);
        wctablet_queue_output(tablet, WC_MODEL_STRING,
                              WC_MODEL_STRING_LENGTH);
        return len;
    }

    /* detect line */
    pos = strchr((char *)tablet->query, '\r');
    if (!pos) {
        pos = strchr((char *)tablet->query, '\n');
    }
    if (!pos) {
        return len;
    }
    clen = pos - (char *)tablet->query;

    /* process commands */
    if (strncmp((char *)tablet->query, "RE", 2) == 0 &&
        clen == 2) {
        trace_wct_cmd_re();
        wctablet_shift_input(tablet, 3);
        wctablet_queue_output(tablet, WC_CONFIG_STRING,
                              WC_CONFIG_STRING_LENGTH);

    } else if (strncmp((char *)tablet->query, "TS", 2) == 0 &&
               clen == 3) {
        unsigned int input = tablet->query[2];
        uint8_t codes[7] = {
            0xa3,
            ((input & 0x80) == 0) ? 0x7e : 0x7f,
            (((WC_H4(input) & 0x7) ^ 0x5) << 4) | (WC_L4(input) ^ 0x7),
            0x03,
            0x7f,
            0x7f,
            0x00,
        };
        trace_wct_cmd_ts(input);
        wctablet_shift_input(tablet, 4);
        wctablet_queue_output(tablet, codes, 7);

    } else {
        tablet->query[clen] = 0; /* terminate line for printing */
        trace_wct_cmd_other((char *)tablet->query);
        wctablet_shift_input(tablet, clen + 1);

    }

    return len;
}

static int wctablet_chr_ioctl(CharDriverState *s, int cmd, void *arg)
{
    TabletState *tablet = (TabletState *) s->opaque;
    QEMUSerialSetParams *ssp;

    switch (cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        ssp = arg;
        if (tablet->line_speed != ssp->speed) {
            trace_wct_speed(ssp->speed);
            wctablet_reset(tablet);
            tablet->line_speed = ssp->speed;
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void wctablet_chr_free(struct CharDriverState *chr)
{
    g_free (chr->opaque);
    g_free (chr);
}

static CharDriverState *qemu_chr_open_wctablet(const char *id,
                                              ChardevBackend *backend,
                                              ChardevReturn *ret,
                                              bool *be_opened,
                                              Error **errp)
{
    ChardevCommon *common = backend->u.wctablet.data;
    CharDriverState *chr;
    TabletState *tablet;

    chr = qemu_chr_alloc(common, errp);
    tablet = g_malloc0(sizeof(TabletState));
    if (!chr) {
        return NULL;
    }
    chr->chr_write = wctablet_chr_write;
    chr->chr_ioctl = wctablet_chr_ioctl;
    chr->chr_free = wctablet_chr_free;
    chr->chr_accept_input = wctablet_chr_accept_input;
    *be_opened = true;

    /* init state machine */
    memcpy(tablet->outbuf, WC_FULL_CONFIG_STRING, WC_FULL_CONFIG_STRING_LENGTH);
    tablet->outlen = WC_FULL_CONFIG_STRING_LENGTH;
    tablet->query_index = 0;

    chr->opaque = tablet;
    tablet->chr = chr;

    qemu_add_mouse_event_handler(wctablet_event, chr, 1,
                                 "QEMU Wacome Pen Tablet");

    return chr;
}

static void register_types(void)
{
    register_char_driver("wctablet", CHARDEV_BACKEND_KIND_WCTABLET, NULL,
                         qemu_chr_open_wctablet);
}

type_init(register_types);

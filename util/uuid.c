/*
 *  QEMU UUID functions
 *
 *  Copyright 2016 Red Hat, Inc.
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/uuid.h"
#include "qemu/bswap.h"

void qemu_uuid_generate(QemuUUID *uuid)
{
    int i;
    uint32_t tmp[4];

    QEMU_BUILD_BUG_ON(sizeof(QemuUUID) != 16);

    for (i = 0; i < 4; ++i) {
        tmp[i] = g_random_int();
    }
    memcpy(uuid, tmp, sizeof(tmp));
    /* Set the two most significant bits (bits 6 and 7) of the
      clock_seq_hi_and_reserved to zero and one, respectively. */
    uuid->data[8] = (uuid->data[8] & 0x3f) | 0x80;
    /* Set the four most significant bits (bits 12 through 15) of the
      time_hi_and_version field to the 4-bit version number.
      */
    uuid->data[6] = (uuid->data[6] & 0xf) | 0x40;
}

int qemu_uuid_is_null(const QemuUUID *uu)
{
    QemuUUID null_uuid = { 0 };
    return memcmp(uu, &null_uuid, sizeof(QemuUUID)) == 0;
}

void qemu_uuid_unparse(const QemuUUID *uuid, char *out)
{
    const unsigned char *uu = &uuid->data[0];
    snprintf(out, UUID_FMT_LEN + 1, UUID_FMT,
             uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7],
             uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}

char *qemu_uuid_unparse_strdup(const QemuUUID *uuid)
{
    const unsigned char *uu = &uuid->data[0];
    return g_strdup_printf(UUID_FMT,
                           uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6],
                           uu[7], uu[8], uu[9], uu[10], uu[11], uu[12],
                           uu[13], uu[14], uu[15]);
}

int qemu_uuid_parse(const char *str, uint8_t *uuid)
{
    int ret;

    if (strlen(str) != 36) {
        return -1;
    }

    ret = sscanf(str, UUID_FMT, &uuid[0], &uuid[1], &uuid[2], &uuid[3],
                 &uuid[4], &uuid[5], &uuid[6], &uuid[7], &uuid[8], &uuid[9],
                 &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14],
                 &uuid[15]);

    if (ret != 16) {
        return -1;
    }
    return 0;
}

/* Swap from UUID format endian (BE) to the opposite or vice versa.
 */
void qemu_uuid_bswap(QemuUUID *uuid)
{
    assert(QEMU_IS_ALIGNED((uint64_t)uuid, sizeof(uint32_t)));
    bswap32s(&uuid->fields.time_low);
    bswap16s(&uuid->fields.time_mid);
    bswap16s(&uuid->fields.time_high_and_version);
}

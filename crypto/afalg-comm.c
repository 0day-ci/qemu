/*
 * QEMU Crypto af_alg support
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "crypto/afalg-comm.h"

void afalg_comm_format_type(AfalgSocketAddress *afalg,
                            const char *type)
{
    afalg->type = (char *)g_new0(int8_t, SALG_TYPE_LEN_MAX);
    pstrcpy(afalg->type, SALG_TYPE_LEN_MAX, type);
}

void afalg_comm_free(QCryptoAfalg *afalg)
{
    if (afalg) {
        if (afalg->msg) {
            g_free(afalg->msg->msg_control);
            g_free(afalg->msg);
        }

        if (afalg->tfmfd != -1) {
            closesocket(afalg->tfmfd);
        }

        if (afalg->opfd != -1) {
            closesocket(afalg->opfd);
        }

        g_free(afalg);
    }
}

QCryptoAfalg *afalg_comm_alloc(SocketAddress *saddr)
{
    QCryptoAfalg *afalg = NULL;
    Error *err = NULL;

    afalg = g_new0(QCryptoAfalg, 1);
    /* initilize crypto API socket */
    afalg->opfd = -1;
    afalg->tfmfd = socket_bind(saddr, &err);
    if (afalg->tfmfd == -1) {
        goto error;
    }

    afalg->opfd = qemu_accept(afalg->tfmfd, NULL, 0);
    if (afalg->opfd == -1) {
        closesocket(afalg->tfmfd);
        goto error;
    }

    return afalg;

error:
    error_free(err);
    afalg_comm_free(afalg);
    return NULL;
}

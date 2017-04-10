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
#ifndef QCRYPTO_AFALG_H
#define QCRYPTO_AFALG_H

#include "qapi-types.h"

#ifndef SOL_ALG
#define SOL_ALG 279
#endif

#define ALG_TYPE_CIPHER "skcipher"
#define ALG_TYPE_HASH   "hash"
#define ALG_TYPE_HMAC   "hmac"

#define ALG_OPTYPE_LEN 4
#define ALG_MSGIV_LEN(len) (sizeof(struct af_alg_iv) + (len))

typedef struct QCryptoAfalg QCryptoAfalg;
struct QCryptoAfalg {
    int tfmfd;
    int opfd;
    struct msghdr *msg;
    struct cmsghdr *cmsg;
};


/**
 * afalg_comm_format_type:
 * @afalg: the AfalgSocketAddress object
 * @type: the type of crypto alg.
 *
 * Set the type field of the @afalg according to @type.
 */
void afalg_comm_format_type(AfalgSocketAddress *afalg,
                            const char *type);

/**
 * afalg_comm_alloc:
 * @saddr: the SocketAddress object
 *
 * Allocate a QCryptoAfalg object and bind itself to
 * a AF_ALG socket.
 *
 * Returns:
 *  a new QCryptoAfalg object, or NULL in error.
 */
QCryptoAfalg *afalg_comm_alloc(SocketAddress *saddr);

/**
 * afalg_comm_free:
 * @afalg: the QCryptoAfalg object
 *
 * Free the @afalg.
 */
void afalg_comm_free(QCryptoAfalg *afalg);

extern QCryptoAfalg *
afalg_cipher_ctx_new(QCryptoCipherAlgorithm alg, QCryptoCipherMode mode,
                     const uint8_t *key, size_t nkey, Error **errp);

extern struct QCryptoCipherDriver qcrypto_cipher_afalg_driver;

#endif

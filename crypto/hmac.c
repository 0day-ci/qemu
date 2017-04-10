/*
 * QEMU Crypto hmac algorithms
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/hmac.h"
#include "crypto/afalg-comm.h"

static const char hex[] = "0123456789abcdef";

int qcrypto_hmac_bytesv(QCryptoHmac *hmac,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp)
{
    return hmac->driver->hmac_bytesv(hmac, iov, niov,
                                     result, resultlen, errp);
}

int qcrypto_hmac_bytes(QCryptoHmac *hmac,
                       const char *buf,
                       size_t len,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp)
{
    struct iovec iov = {
            .iov_base = (char *)buf,
            .iov_len = len
    };

    return qcrypto_hmac_bytesv(hmac, &iov, 1, result, resultlen, errp);
}

int qcrypto_hmac_digestv(QCryptoHmac *hmac,
                         const struct iovec *iov,
                         size_t niov,
                         char **digest,
                         Error **errp)
{
    uint8_t *result = NULL;
    size_t resultlen = 0;
    size_t i;

    if (qcrypto_hmac_bytesv(hmac, iov, niov, &result, &resultlen, errp) < 0) {
        return -1;
    }

    *digest = g_new0(char, (resultlen * 2) + 1);

    for (i = 0 ; i < resultlen ; i++) {
        (*digest)[(i * 2)] = hex[(result[i] >> 4) & 0xf];
        (*digest)[(i * 2) + 1] = hex[result[i] & 0xf];
    }

    (*digest)[resultlen * 2] = '\0';

    g_free(result);
    return 0;
}

int qcrypto_hmac_digest(QCryptoHmac *hmac,
                        const char *buf,
                        size_t len,
                        char **digest,
                        Error **errp)
{
    struct iovec iov = {
            .iov_base = (char *)buf,
            .iov_len = len
    };

    return qcrypto_hmac_digestv(hmac, &iov, 1, digest, errp);
}

QCryptoHmac *qcrypto_hmac_new(QCryptoHashAlgorithm alg,
                              const uint8_t *key, size_t nkey,
                              Error **errp)
{
    QCryptoHmac *hmac;
    void *ctx;
    QCryptoHmacDriver *drv;
#ifdef CONFIG_AF_ALG
    Error *errp2 = NULL;

    ctx = afalg_hmac_ctx_new(alg, key, nkey, &errp2);
    if (ctx) {
        drv = &qcrypto_hmac_afalg_driver;
        goto set;
    }

    if (errp2) {
        error_free(errp2);
    }
#endif

    ctx = qcrypto_hmac_ctx_new(alg, key, nkey, errp);
    if (ctx == NULL) {
        return NULL;
    }
    drv = &qcrypto_hmac_lib_driver;

set:
    hmac = g_new0(QCryptoHmac, 1);
    hmac->alg = alg;
    hmac->opaque = ctx;
    hmac->driver = drv;

    return hmac;
}

void qcrypto_hmac_free(QCryptoHmac *hmac)
{
    if (hmac) {
        hmac->driver->hmac_free(hmac);
        g_free(hmac);
    }
}

bool qcrypto_hmac_using_afalg_drv(QCryptoHmac *hmac)
{
#ifdef CONFIG_AF_ALG
    return hmac->driver == &qcrypto_hmac_afalg_driver;
#else
    return false;
#endif
}

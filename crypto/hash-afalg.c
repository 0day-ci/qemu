/*
 * QEMU Crypto af_alg-backend hash/hmac support
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
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "crypto/hash.h"
#include "crypto/hmac.h"
#include "crypto/afalg-comm.h"
#include <linux/if_alg.h>

static int afalg_hash_format_name(QCryptoHashAlgorithm alg,
                                  AfalgSocketAddress *afalg)
{
    const char *alg_name = NULL;

    switch (alg) {
    case QCRYPTO_HASH_ALG_MD5:
        alg_name = "md5";
        break;
    case QCRYPTO_HASH_ALG_SHA1:
        alg_name = "sha1";
        break;
    case QCRYPTO_HASH_ALG_SHA224:
        alg_name = "sha224";
        break;
    case QCRYPTO_HASH_ALG_SHA256:
        alg_name = "sha256";
        break;
    case QCRYPTO_HASH_ALG_SHA384:
        alg_name = "sha384";
        break;
    case QCRYPTO_HASH_ALG_SHA512:
        alg_name = "sha512";
        break;
    case QCRYPTO_HASH_ALG_RIPEMD160:
        alg_name = "rmd160";
        break;

    default:
        return -1;
    }

    afalg->name = (char *)g_new0(int8_t, SALG_NAME_LEN_MAX);
    sprintf(afalg->name, "%s", alg_name);

    return 0;
}

static QCryptoAfalg *
afalg_hash_hmac_ctx_new(QCryptoHashAlgorithm alg,
                        const uint8_t *key, size_t nkey,
                        bool is_hash)
{
    SocketAddress *saddr = NULL;
    QCryptoAfalg *afalg = NULL;
    int ret = 0;

    saddr = g_new0(SocketAddress, 1);
    saddr->u.afalg.data = g_new0(AfalgSocketAddress, 1);
    saddr->type = SOCKET_ADDRESS_KIND_AFALG;
    ret = afalg_hash_format_name(alg, saddr->u.afalg.data);
    if (ret != 0) {
        goto error;
    }

    if (is_hash) {
        afalg_comm_format_type(saddr->u.afalg.data, ALG_TYPE_HASH);
    } else {
        afalg_comm_format_type(saddr->u.afalg.data, ALG_TYPE_HMAC);
    }

    afalg = afalg_comm_alloc(saddr);
    if (!afalg) {
        goto error;
    }

    /* HMAC needs setkey */
    if (!is_hash) {
        ret = qemu_setsockopt(afalg->tfmfd, SOL_ALG, ALG_SET_KEY,
                              key, nkey);
        if (ret != 0) {
            goto error;
        }
    }

    /* prepare msg header */
    afalg->msg = g_new0(struct msghdr, 1);

cleanup:
    g_free(saddr->u.afalg.data->type);
    g_free(saddr->u.afalg.data->name);
    g_free(saddr->u.afalg.data);
    g_free(saddr);
    return afalg;

error:
    afalg_comm_free(afalg);
    afalg = NULL;
    goto cleanup;
}

static QCryptoAfalg *afalg_hash_ctx_new(QCryptoHashAlgorithm alg)
{
    return afalg_hash_hmac_ctx_new(alg, NULL, 0, true);
}

QCryptoAfalg *afalg_hmac_ctx_new(QCryptoHashAlgorithm alg,
                                 const uint8_t *key, size_t nkey,
                                 Error **errp)
{
    QCryptoAfalg *afalg;

    afalg = afalg_hash_hmac_ctx_new(alg, key, nkey, false);
    if (afalg == NULL) {
        error_setg(errp, "Afalg cannot initialize hmac and set key");
        return NULL;
    }

    return afalg;
}

static int
afalg_hash_hmac_bytesv(QCryptoAfalg *hmac,
                       QCryptoHashAlgorithm alg,
                       const struct iovec *iov,
                       size_t niov, uint8_t **result,
                       size_t *resultlen,
                       Error **errp)
{
    QCryptoAfalg *afalg = NULL;
    struct iovec outv;
    int ret = 0;
    bool is_hmac = (hmac != NULL) ? true : false;
    const int except_len = qcrypto_hash_digest_len(alg);

    if (*resultlen == 0) {
        *resultlen = except_len;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != except_len) {
        error_setg(errp,
                   "Result buffer size %zu is not match hash %d",
                   *resultlen, except_len);
        return -1;
    }

    if (is_hmac) {
        afalg = hmac;
    } else {
        afalg = afalg_hash_ctx_new(alg);
        if (afalg == NULL) {
            error_setg(errp, "Alloc QCryptoAfalg object failed");
            return -1;
        }
    }

    /* send data to kernel's crypto core */
    ret = iov_send_recv(afalg->opfd, iov, niov,
                        0, iov_size(iov, niov), true);
    if (ret < 0) {
        error_setg(errp, "Send data to afalg-core failed");
        goto out;
    }

    /* hash && get result */
    outv.iov_base = *result;
    outv.iov_len = *resultlen;
    afalg->msg->msg_iov = &outv;
    afalg->msg->msg_iovlen = 1;
    ret = recvmsg(afalg->opfd, afalg->msg, 0);
    if (ret != -1) {
        ret = 0;
    } else {
        error_setg(errp, "Recv result from afalg-core failed");
    }

out:
    if (!is_hmac) { /* hash */
        afalg_comm_free(afalg);
    }
    return ret;
}

static int
afalg_hash_bytesv(QCryptoHashAlgorithm alg,
                  const struct iovec *iov,
                  size_t niov, uint8_t **result,
                  size_t *resultlen,
                  Error **errp)
{
    return afalg_hash_hmac_bytesv(NULL, alg, iov, niov,
                                  result, resultlen, errp);
}

static int
afalg_hmac_bytesv(QCryptoHmac *hmac,
                  const struct iovec *iov,
                  size_t niov, uint8_t **result,
                  size_t *resultlen,
                  Error **errp)
{
    return afalg_hash_hmac_bytesv(hmac->opaque, hmac->alg, iov, niov,
                                  result, resultlen, errp);
}

static void afalg_hmac_ctx_free(QCryptoHmac *hmac)
{
    QCryptoAfalg *afalg;

    afalg = hmac->opaque;
    afalg_comm_free(afalg);
}

QCryptoHashDriver qcrypto_hash_afalg_driver = {
    .hash_bytesv = afalg_hash_bytesv,
};

QCryptoHmacDriver qcrypto_hmac_afalg_driver = {
    .hmac_bytesv = afalg_hmac_bytesv,
    .hmac_free = afalg_hmac_ctx_free,
};

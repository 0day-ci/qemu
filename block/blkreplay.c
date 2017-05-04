/*
 * Block protocol for record/replay
 *
 * Copyright (c) 2010-2016 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/block_int.h"
#include "sysemu/replay.h"
#include "qapi/error.h"
#include "qapi/qmp/qstring.h"

typedef struct Request {
    Coroutine *co;
    QEMUBH *bh;
} Request;

static BlockDriverState *blkreplay_append_snapshot(BlockDriverState *bs,
                                                   Error **errp)
{
    int ret;
    BlockDriverState *bs_snapshot;
    int64_t total_size;
    QemuOpts *opts = NULL;
    char tmp_filename[PATH_MAX + 1];
    QDict *snapshot_options = qdict_new();

    /* Prepare options QDict for the overlay file */
    qdict_put(snapshot_options, "file.driver", qstring_from_str("file"));
    qdict_put(snapshot_options, "driver", qstring_from_str("qcow2"));

    /* Create temporary file */
    ret = get_tmp_filename(tmp_filename, PATH_MAX + 1);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not get temporary filename");
        goto out;
    }
    qdict_put(snapshot_options, "file.filename",
              qstring_from_str(tmp_filename));

    /* Get the required size from the image */
    total_size = bdrv_getlength(bs);
    if (total_size < 0) {
        error_setg_errno(errp, -total_size, "Could not get image size");
        goto out;
    }

    opts = qemu_opts_create(bdrv_qcow2.create_opts, NULL, 0, &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, total_size, &error_abort);
    ret = bdrv_create(&bdrv_qcow2, tmp_filename, opts, errp);
    qemu_opts_del(opts);
    if (ret < 0) {
        error_prepend(errp, "Could not create temporary overlay '%s': ",
                      tmp_filename);
        goto out;
    }

    bs_snapshot = bdrv_open(NULL, NULL, snapshot_options,
                            BDRV_O_RDWR | BDRV_O_TEMPORARY, errp);
    snapshot_options = NULL;
    if (!bs_snapshot) {
        goto out;
    }

    bdrv_append(bs_snapshot, bs, errp);

    return bs_snapshot;

out:
    QDECREF(snapshot_options);
    return NULL;
}

static int blkreplay_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    Error *local_err = NULL;
    int ret;

    /* Open the image file */
    bs->file = bdrv_open_child(NULL, options, "image",
                               bs, &child_file, false, &local_err);
    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        goto fail;
    }

    /* Add temporary snapshot to preserve the image */
    if (!replay_snapshot
        && !blkreplay_append_snapshot(bs->file->bs, &local_err)) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        goto fail;
    }

    ret = 0;
fail:
    if (ret < 0) {
        bdrv_unref_child(bs, bs->file);
    }
    return ret;
}

static void blkreplay_close(BlockDriverState *bs)
{
}

static int64_t blkreplay_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

/* This bh is used for synchronization of return from coroutines.
   It continues yielded coroutine which then finishes its execution.
   BH is called adjusted to some replay checkpoint, therefore
   record and replay will always finish coroutines deterministically.
*/
static void blkreplay_bh_cb(void *opaque)
{
    Request *req = opaque;
    aio_co_wake(req->co);
    qemu_bh_delete(req->bh);
    g_free(req);
}

static void block_request_create(uint64_t reqid, BlockDriverState *bs,
                                 Coroutine *co)
{
    Request *req = g_new(Request, 1);
    *req = (Request) {
        .co = co,
        .bh = aio_bh_new(bdrv_get_aio_context(bs), blkreplay_bh_cb, req),
    };
    replay_block_event(req->bh, reqid);
}

static int coroutine_fn blkreplay_co_preadv(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    uint64_t reqid = blkreplay_next_id();
    int ret = bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
    block_request_create(reqid, bs, qemu_coroutine_self());
    qemu_coroutine_yield();

    return ret;
}

static int coroutine_fn blkreplay_co_pwritev(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    uint64_t reqid = blkreplay_next_id();
    int ret = bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
    block_request_create(reqid, bs, qemu_coroutine_self());
    qemu_coroutine_yield();

    return ret;
}

static int coroutine_fn blkreplay_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int count, BdrvRequestFlags flags)
{
    uint64_t reqid = blkreplay_next_id();
    int ret = bdrv_co_pwrite_zeroes(bs->file, offset, count, flags);
    block_request_create(reqid, bs, qemu_coroutine_self());
    qemu_coroutine_yield();

    return ret;
}

static int coroutine_fn blkreplay_co_pdiscard(BlockDriverState *bs,
                                              int64_t offset, int count)
{
    uint64_t reqid = blkreplay_next_id();
    int ret = bdrv_co_pdiscard(bs->file->bs, offset, count);
    block_request_create(reqid, bs, qemu_coroutine_self());
    qemu_coroutine_yield();

    return ret;
}

static int coroutine_fn blkreplay_co_flush(BlockDriverState *bs)
{
    uint64_t reqid = blkreplay_next_id();
    int ret = bdrv_co_flush(bs->file->bs);
    block_request_create(reqid, bs, qemu_coroutine_self());
    qemu_coroutine_yield();

    return ret;
}

static int blkreplay_snapshot_goto(BlockDriverState *bs,
                                   const char *snapshot_id)
{
    return bdrv_snapshot_goto(bs->file->bs, snapshot_id);
}

static BlockDriver bdrv_blkreplay = {
    .format_name            = "blkreplay",
    .protocol_name          = "blkreplay",
    .instance_size          = 0,

    .bdrv_file_open         = blkreplay_open,
    .bdrv_close             = blkreplay_close,
    .bdrv_child_perm        = bdrv_filter_default_perms,
    .bdrv_getlength         = blkreplay_getlength,

    .bdrv_co_preadv         = blkreplay_co_preadv,
    .bdrv_co_pwritev        = blkreplay_co_pwritev,

    .bdrv_co_pwrite_zeroes  = blkreplay_co_pwrite_zeroes,
    .bdrv_co_pdiscard       = blkreplay_co_pdiscard,
    .bdrv_co_flush          = blkreplay_co_flush,

    .bdrv_snapshot_goto     = blkreplay_snapshot_goto,
};

static void bdrv_blkreplay_init(void)
{
    bdrv_register(&bdrv_blkreplay);
}

block_init(bdrv_blkreplay_init);

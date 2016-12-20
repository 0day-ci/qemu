/*
 * NVMe block driver based on vfio
 *
 * Copyright 2016 Red Hat, Inc.
 *
 * Authors:
 *   Fam Zheng <famz@redhat.com>
 *   Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "block/block_int.h"
#include "qemu/error-report.h"
#include "qemu/vfio-helpers.h"

#define NVME_DEBUG 0

#define DPRINTF(...) \
    if (NVME_DEBUG) { \
        printf(__VA_ARGS__); \
    }

#define NVME_SQ_ENTRY_BYTES 64
#define NVME_CQ_ENTRY_BYTES 16
#define NVME_QUEUE_SIZE 128

/* TODO: share definitions with hw/block/nvme.c? */
enum NvmeAdminCommands {
    NVME_ADM_CMD_DELETE_SQ      = 0x00,
    NVME_ADM_CMD_CREATE_SQ      = 0x01,
    NVME_ADM_CMD_GET_LOG_PAGE   = 0x02,
    NVME_ADM_CMD_DELETE_CQ      = 0x04,
    NVME_ADM_CMD_CREATE_CQ      = 0x05,
    NVME_ADM_CMD_IDENTIFY       = 0x06,
    NVME_ADM_CMD_ABORT          = 0x08,
    NVME_ADM_CMD_SET_FEATURES   = 0x09,
    NVME_ADM_CMD_GET_FEATURES   = 0x0a,
    NVME_ADM_CMD_ASYNC_EV_REQ   = 0x0c,
    NVME_ADM_CMD_ACTIVATE_FW    = 0x10,
    NVME_ADM_CMD_DOWNLOAD_FW    = 0x11,
    NVME_ADM_CMD_FORMAT_NVM     = 0x80,
    NVME_ADM_CMD_SECURITY_SEND  = 0x81,
    NVME_ADM_CMD_SECURITY_RECV  = 0x82,
};

enum NvmeIoCommands {
    NVME_CMD_FLUSH              = 0x00,
    NVME_CMD_WRITE              = 0x01,
    NVME_CMD_READ               = 0x02,
    NVME_CMD_WRITE_UNCOR        = 0x04,
    NVME_CMD_COMPARE            = 0x05,
    NVME_CMD_DSM                = 0x09,
};

typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} QEMU_PACKED NVMeCommand;

typedef struct {
    uint32_t cmd_specific;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sqid;
    uint16_t cid;
    uint16_t status;
} QEMU_PACKED NVMeCompletion;

typedef struct {
    int32_t  head, tail;
    uint8_t  *queue;
    uint64_t iova;
    volatile uint32_t *doorbell;
} NVMeQueue;

typedef struct {
    BlockCompletionFunc *cb;
    void *opaque;
    int cid;
    void *prp_list_page;
    uint64_t prp_list_iova;
} NVMeRequest;

typedef struct {
    int         index;
    NVMeQueue   sq, cq;
    int         cq_phase;
    uint8_t     *prp_list_pages;
    uint64_t    prp_list_base_iova;
    NVMeRequest reqs[NVME_QUEUE_SIZE];
    CoQueue     wait_queue;
    bool        busy;
    int         need_kick;
    int         inflight;
} NVMeQueuePair;

typedef volatile struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t reserved0;
    uint32_t csts;
    uint32_t nssr;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
    uint32_t cmbloc;
    uint32_t cmbsz;
    uint8_t  reserved1[0xec0];
    uint8_t  cmd_set_specfic[0x100];
    uint32_t doorbells[];
} QEMU_PACKED NVMeRegs;

QEMU_BUILD_BUG_ON(offsetof(NVMeRegs, doorbells) != 0x1000);

typedef struct {
    QEMUVFIOState *vfio;
    NVMeRegs *regs;
    /* The submission/completion queue pairs.
     * [0]: admin queue.
     * [1..]: io queues.
     */
    NVMeQueuePair **queues;
    int nr_queues;
    size_t page_size;
    /* How many uint32_t elements does each doorbell entry take. */
    size_t doorbell_scale;
    bool write_cache;
    EventNotifier event_notifier;
    uint64_t nsze; /* Namespace size reported by identify command */
    int nsid;      /* The namespace id to read/write data. */
    uint64_t max_transfer;
    int plugged;
    Notifier vfree_notify;
} BDRVNVMeState;

#define NVME_BLOCK_OPT_DEVICE "device"
#define NVME_BLOCK_OPT_NAMESPACE "namespace"

static QemuOptsList runtime_opts = {
    .name = "nvme",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = NVME_BLOCK_OPT_DEVICE,
            .type = QEMU_OPT_STRING,
            .help = "NVMe PCI device address",
        },
        {
            .name = NVME_BLOCK_OPT_NAMESPACE,
            .type = QEMU_OPT_NUMBER,
            .help = "NVMe namespace",
        },
        { /* end of list */ }
    },
};

static void nvme_init_queue(BlockDriverState *bs, NVMeQueue *q,
                            int nentries, int entry_bytes, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    size_t bytes;
    int r;

    bytes = ROUND_UP(nentries * entry_bytes, s->page_size);
    q->head = q->tail = 0;
    q->queue = qemu_try_blockalign0(bs, bytes);

    if (!q->queue) {
        error_setg(errp, "Cannot allocate queue");
        return;
    }
    r = qemu_vfio_dma_map(s->vfio, q->queue, bytes, true, &q->iova);
    if (r) {
        error_setg(errp, "Cannot map queue");
    }
}

static void nvme_free_queue_pair(BlockDriverState *bs, NVMeQueuePair *q)
{
    qemu_vfree(q->prp_list_pages);
    qemu_vfree(q->sq.queue);
    qemu_vfree(q->cq.queue);
    g_free(q);
}

static NVMeQueuePair *nvme_create_queue_pair(BlockDriverState *bs,
                                             int idx, int size,
                                             Error **errp)
{
    int i, r;
    BDRVNVMeState *s = bs->opaque;
    Error *local_err = NULL;
    NVMeQueuePair *q = g_new0(NVMeQueuePair, 1);
    uint64_t prp_list_iovas[NVME_QUEUE_SIZE];

    q->index = idx;
    qemu_co_queue_init(&q->wait_queue);
    q->prp_list_pages = qemu_blockalign0(bs, s->page_size * NVME_QUEUE_SIZE);
    r = qemu_vfio_dma_map(s->vfio, q->prp_list_pages,
                          s->page_size * NVME_QUEUE_SIZE,
                          false, prp_list_iovas);
    if (r) {
        goto fail;
    }
    for (i = 0; i < NVME_QUEUE_SIZE; i++) {
        NVMeRequest *req = &q->reqs[i];
        req->cid = i + 1;
        req->prp_list_page = q->prp_list_pages + i * s->page_size;
        req->prp_list_iova = prp_list_iovas[i];
    }
    nvme_init_queue(bs, &q->sq, size, NVME_SQ_ENTRY_BYTES, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }
    q->sq.doorbell = &s->regs->doorbells[idx * 2 * s->doorbell_scale];

    nvme_init_queue(bs, &q->cq, size, NVME_CQ_ENTRY_BYTES, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }
    q->cq.doorbell = &s->regs->doorbells[idx * 2 * s->doorbell_scale + 1];

    return q;
fail:
    nvme_free_queue_pair(bs, q);
    return NULL;
}

static void nvme_kick(BDRVNVMeState *s, NVMeQueuePair *q)
{
    if (s->plugged || !q->need_kick) {
        return;
    }
    DPRINTF("nvme kick queue %d\n", q->index);
    assert(!(q->sq.tail & 0xFF00));
    /* Fence the write to submission queue entry before notifying the guest. */
    smp_wmb();
    *q->sq.doorbell = cpu_to_le32(q->sq.tail);
    q->inflight += q->need_kick;
    q->need_kick = 0;
}

static NVMeRequest *nvme_get_free_req(NVMeQueuePair *q)
{
    int i;
    if (q->inflight + q->need_kick > NVME_QUEUE_SIZE - 2) {
        /* We have to leave one slot empty as that is the full queue case (head
         * == tail + 1). */
        return NULL;
    }
    for (i = 0; i < NVME_QUEUE_SIZE; i++) {
        if (!q->reqs[i].cb) {
            return &q->reqs[i];
        }
    }
    return NULL;
}

static inline int nvme_translate_error(const NVMeCompletion *c)
{
    if ((le16_to_cpu(c->status) >> 1) & 0xFF) {
        DPRINTF("NVMe error cmd specific %x sq head %x sqid %x cid %x status %x\n",
                c->cmd_specific, c->sq_head, c->sqid, c->cid, c->status);
    }
    switch ((le16_to_cpu(c->status) >> 1) & 0xFF) {
    case 0:
        return 0;
    case 1:
        return -ENOSYS;
    case 2:
        return -EINVAL;
    default:
        return -EIO;
    }
}

static bool nvme_process_completion(BDRVNVMeState *s, NVMeQueuePair *q)
{
    bool progress = false;
    NVMeRequest *req;
    NVMeCompletion *c;

    DPRINTF("nvme process completion %d inflight %d\n", q->index, q->inflight);
    if (q->busy || s->plugged) {
        DPRINTF("queue busy\n");
        return false;
    }
    q->busy = true;
    assert(q->inflight >= 0);
    while (q->inflight) {
        c = (NVMeCompletion *)&q->cq.queue[q->cq.head * NVME_CQ_ENTRY_BYTES];
        if (!c->cid || (le16_to_cpu(c->status) & 0x1) == q->cq_phase) {
            break;
        }
        q->cq.head = (q->cq.head + 1) % NVME_QUEUE_SIZE;
        if (!q->cq.head) {
            q->cq_phase = !q->cq_phase;
        }
        /* XXX: error handling instead of assert as it's from device? */
        assert(c->cid > 0);
        assert(c->cid <= NVME_QUEUE_SIZE);
        DPRINTF("nvme completing command %d\n", c->cid);
        req = &q->reqs[c->cid - 1];
        assert(req->cid == c->cid);
        assert(req->cb);
        req->cb(req->opaque, nvme_translate_error(c));
        req->cb = req->opaque = NULL;
        qemu_co_enter_next(&q->wait_queue);
        c->cid = 0;
        q->inflight--;
        /* Flip Phase Tag bit. */
        c->status = cpu_to_le16(le16_to_cpu(c->status) ^ 0x1);
        progress = true;
    }
    if (progress) {
        /* Notify the device so it can post more completions. */
        smp_mb_release();
        *q->cq.doorbell = cpu_to_le32(q->cq.head);
    }
    q->busy = false;
    return progress;
}

static void nvme_submit_command(BDRVNVMeState *s, NVMeQueuePair *q,
                                NVMeRequest *req,
                                NVMeCommand *cmd, BlockCompletionFunc cb,
                                void *opaque)
{
    req->cb = cb;
    req->opaque = opaque;
    cmd->cid = cpu_to_le32(req->cid);
    DPRINTF("nvme submit command %d to queue %d\n", req->cid, q->index);
    memcpy((uint8_t *)q->sq.queue +
           q->sq.tail * NVME_SQ_ENTRY_BYTES, cmd, sizeof(*cmd));
    q->sq.tail = (q->sq.tail + 1) % NVME_QUEUE_SIZE;
    q->need_kick++;
    nvme_kick(s, q);
    nvme_process_completion(s, q);
}

static void nvme_cmd_sync_cb(void *opaque, int ret)
{
    int *pret = opaque;
    *pret = ret;
}

static int nvme_cmd_sync(BlockDriverState *bs, NVMeQueuePair *q,
                         NVMeCommand *cmd)
{
    NVMeRequest *req;
    BDRVNVMeState *s = bs->opaque;
    int ret = -EINPROGRESS;
    req = nvme_get_free_req(q);
    if (!req) {
        return -EBUSY;
    }
    nvme_submit_command(s, q, req, cmd, nvme_cmd_sync_cb, &ret);

    BDRV_POLL_WHILE(bs, ret == -EINPROGRESS);
    return ret;
}

static bool nvme_identify(BlockDriverState *bs, int namespace, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    uint8_t *resp;
    int r;
    uint64_t iova;
    NVMeCommand cmd = {
        .opcode = NVME_ADM_CMD_IDENTIFY,
        .cdw10 = cpu_to_le32(0x1),
    };

    resp = qemu_try_blockalign0(bs, 4096);
    if (!resp) {
        error_setg(errp, "Cannot allocate buffer for identify response");
        return false;
    }
    r = qemu_vfio_dma_map(s->vfio, resp, 4096, true, &iova);
    if (r) {
        error_setg(errp, "Cannot map buffer for DMA");
        goto fail;
    }
    cmd.prp1 = cpu_to_le64(iova);

    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to identify controller");
        goto fail;
    }

    if (le32_to_cpu(*(uint32_t *)&resp[516]) < namespace) {
        error_setg(errp, "Invalid namespace");
        goto fail;
    }
    s->write_cache = le32_to_cpu(resp[525]) & 0x1;
    s->max_transfer = (resp[77] ? 1 << resp[77] : 0) * s->page_size;
    /* For now the page list buffer per command is one page, to hold at most
     * s->page_size / sizeof(uint64_t) entries. */
    s->max_transfer = MIN_NON_ZERO(s->max_transfer,
                          s->page_size / sizeof(uint64_t) * s->page_size);

    memset((char *)resp, 0, 4096);

    cmd.cdw10 = 0;
    cmd.nsid = namespace;
    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to identify namespace");
        goto fail;
    }

    /*qemu_hexdump((const char *)resp, stdout, "NS", 4096);*/
    s->nsze = le64_to_cpu(*(uint64_t *)&resp[0]);

    qemu_vfree(resp);
    return true;
fail:
    qemu_vfree(resp);
    return false;
}

static void nvme_handle_event(EventNotifier *n)
{
    int i;
    BDRVNVMeState *s = container_of(n, BDRVNVMeState, event_notifier);

    DPRINTF("nvme handle event\n");
    event_notifier_test_and_clear(n);
    for (i = 0; i < s->nr_queues; i++) {
        while (nvme_process_completion(s, s->queues[i])) {
            /* Keep polling until no progress. */
        }
    }
}

static bool nvme_add_io_queue(BlockDriverState *bs, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    int n = s->nr_queues;
    NVMeQueuePair *q;
    NVMeCommand cmd;
    int queue_size = NVME_QUEUE_SIZE;

    q = nvme_create_queue_pair(bs, n, queue_size, errp);
    if (!q) {
        return false;
    }
    cmd = (NVMeCommand) {
        .opcode = NVME_ADM_CMD_CREATE_CQ,
        .prp1 = cpu_to_le64(q->cq.iova),
        .cdw10 = cpu_to_le32(((queue_size - 1) << 16) | (n & 0xFFFF)),
        .cdw11 = cpu_to_le32(0x3),
    };
    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to create io queue [%d]", n);
        nvme_free_queue_pair(bs, q);
        return false;
    }
    cmd = (NVMeCommand) {
        .opcode = NVME_ADM_CMD_CREATE_SQ,
        .prp1 = cpu_to_le64(q->sq.iova),
        .cdw10 = cpu_to_le32(((queue_size - 1) << 16) | (n & 0xFFFF)),
        .cdw11 = cpu_to_le32(0x1 | (n << 16)),
    };
    if (nvme_cmd_sync(bs, s->queues[0], &cmd)) {
        error_setg(errp, "Failed to create io queue [%d]", n);
        nvme_free_queue_pair(bs, q);
        return false;
    }
    s->queues = g_renew(NVMeQueuePair *, s->queues, n + 1);
    s->queues[n] = q;
    s->nr_queues++;
    return true;
}

static bool nvme_poll_cb(void *opaque)
{
    int i;
    EventNotifier *e = opaque;
    BDRVNVMeState *s = container_of(e, BDRVNVMeState, event_notifier);
    bool progress = false;

    DPRINTF("nvme poll cb\n");
    for (i = 0; i < s->nr_queues; i++) {
        while (nvme_process_completion(s, s->queues[i])) {
            progress = true;
        }
    }
    return progress;
}

static void nvme_vfree_cb(Notifier *n, void *p)
{
    BDRVNVMeState *s = container_of(n, BDRVNVMeState, vfree_notify);
    qemu_vfio_dma_unmap(s->vfio, p);
}

static int nvme_init(BlockDriverState *bs, const char *device, int namespace,
                     Error **errp)
{
    BDRVNVMeState *s = bs->opaque;
    int ret;
    uint64_t cap;
    uint64_t timeout_ms;
    uint64_t deadline, now;

    s->nsid = namespace;
    ret = event_notifier_init(&s->event_notifier, 0);
    if (ret) {
        error_setg(errp, "Failed to init event notifier");
        return ret;
    }

    s->vfree_notify.notify = nvme_vfree_cb;
    qemu_vfree_add_notifier(&s->vfree_notify);

    s->vfio = qemu_vfio_open_pci(device, errp);
    if (!s->vfio) {
        ret = -EINVAL;
        goto fail;
    }

    s->regs = qemu_vfio_pci_map_bar(s->vfio, 0, errp);
    if (!s->regs) {
        ret = -EINVAL;
        goto fail;
    }

    /* Perform initialize sequence as described in NVMe spec "7.6.1
     * Initialization". */

    cap = le64_to_cpu(s->regs->cap);
    if (!(cap & (1ULL << 37))) {
        error_setg(errp, "Device doesn't support NVMe command set");
        ret = -EINVAL;
        goto fail;
    }

    s->page_size = MAX(4096, 1 << (12 + ((cap >> 48) & 0xF)));
    s->doorbell_scale = (4 << (((cap >> 32) & 0xF))) / sizeof(uint32_t);
    bs->bl.opt_mem_alignment = s->page_size;
    timeout_ms = MIN(500 * ((cap >> 24) & 0xFF), 30000);

    /* Reset device to get a clean state. */
    s->regs->cc = cpu_to_le32(le32_to_cpu(s->regs->cc) & 0xFE);
    /* Wait for CSTS.RDY = 0. */
    deadline = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + timeout_ms * 1000000ULL;
    while (le32_to_cpu(s->regs->csts) & 0x1) {
        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) > deadline) {
            error_setg(errp, "Timeout while waiting for device to reset (%ld ms)",
                       timeout_ms);
            ret = -ETIMEDOUT;
            goto fail;
        }
    }

    /* Set up admin queue. */
    s->queues = g_new(NVMeQueuePair *, 1);
    s->nr_queues = 1;
    s->queues[0] = nvme_create_queue_pair(bs, 0, NVME_QUEUE_SIZE, errp);
    if (!s->queues[0]) {
        ret = -EINVAL;
        goto fail;
    }
    QEMU_BUILD_BUG_ON(NVME_QUEUE_SIZE & 0xF000);
    s->regs->aqa = cpu_to_le32((NVME_QUEUE_SIZE << 16) | NVME_QUEUE_SIZE);
    s->regs->asq = cpu_to_le64(s->queues[0]->sq.iova);
    s->regs->acq = cpu_to_le64(s->queues[0]->cq.iova);

    /* After setting up all control registers we can enable device now. */
    s->regs->cc = cpu_to_le32((ctz32(NVME_CQ_ENTRY_BYTES) << 20) |
                              (ctz32(NVME_SQ_ENTRY_BYTES) << 16) |
                              0x1);
    /* Wait for CSTS.RDY = 1. */
    now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    deadline = now + timeout_ms * 1000000;
    while (!(le32_to_cpu(s->regs->csts) & 0x1)) {
        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) > deadline) {
            error_setg(errp, "Timeout while waiting for device to start (%ld ms)",
                       timeout_ms);
            ret = -ETIMEDOUT;
            goto fail_queue;
        }
    }

    ret = qemu_vfio_pci_init_irq(s->vfio, &s->event_notifier,
                                 VFIO_PCI_MSIX_IRQ_INDEX, errp);
    if (ret) {
        goto fail_queue;
    }
    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->event_notifier,
                           false, nvme_handle_event, nvme_poll_cb);

    if (!nvme_identify(bs, namespace, errp)) {
        ret = -EIO;
        goto fail_handler;
    }

    /* Set up command queues. */
    /* XXX: multiple io queues? */
    if (!nvme_add_io_queue(bs, errp)) {
        ret = -EIO;
        goto fail_handler;
    }
    return 0;

fail_handler:
    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->event_notifier,
                           false, NULL, NULL);
fail_queue:
    nvme_free_queue_pair(bs, s->queues[0]);
fail:
    qemu_vfio_pci_unmap_bar(s->vfio, 0, (void *)s->regs);
    qemu_vfio_close(s->vfio);
    event_notifier_cleanup(&s->event_notifier);
    return ret;
}

static void nvme_parse_filename(const char *filename, QDict *options,
                                Error **errp)
{
    int pref = strlen("nvme://");

    /* XXX: support namespace in URI like nvme://0000:00:00.0/1 ? */
    if (strlen(filename) > pref && !strncmp(filename, "nvme://", pref)) {
        qdict_put(options, NVME_BLOCK_OPT_DEVICE,
                  qstring_from_str(filename + pref));
    }
}

static int nvme_file_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    const char *device;
    QemuOpts *opts;
    int namespace;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &error_abort);
    device = qemu_opt_get(opts, NVME_BLOCK_OPT_DEVICE);
    if (!device) {
        error_setg(errp, "'" NVME_BLOCK_OPT_DEVICE "' option is required");
        return -EINVAL;
    }

    namespace = qemu_opt_get_number(opts, NVME_BLOCK_OPT_NAMESPACE, 1);
    nvme_init(bs, device, namespace, errp);

    qemu_opts_del(opts);
    return 0;
}

static void nvme_close(BlockDriverState *bs)
{
    int i;
    BDRVNVMeState *s = bs->opaque;

    for (i = 0; i < s->nr_queues; ++i) {
        nvme_free_queue_pair(bs, s->queues[i]);
    }
    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->event_notifier,
                           false, NULL, NULL);
    notifier_remove(&s->vfree_notify);
    qemu_vfio_pci_unmap_bar(s->vfio, 0, (void *)s->regs);
    qemu_vfio_close(s->vfio);
}

static int64_t nvme_getlength(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;

    return s->nsze << BDRV_SECTOR_BITS;
}

static inline int nvme_cmd_map_qiov(BlockDriverState *bs, NVMeCommand *cmd,
                                    NVMeRequest *req, QEMUIOVector *qiov)
{
    BDRVNVMeState *s = bs->opaque;
    uint64_t *pagelist = req->prp_list_page;
    int i, r;
    unsigned int entries = 0;

    assert(qiov->size);
    assert(QEMU_IS_ALIGNED(qiov->size, s->page_size));
    assert(qiov->size / s->page_size <= s->page_size / sizeof(uint64_t));
    for (i = 0; i < qiov->niov; ++i) {
        r = qemu_vfio_dma_map(s->vfio,
                              qiov->iov[i].iov_base,
                              qiov->iov[i].iov_len,
                              false, &pagelist[entries]);
        if (r) {
            return r;
        }

        entries += qiov->iov[i].iov_len / s->page_size;
        assert(entries <= s->page_size / sizeof(uint64_t));
    }

    switch (entries) {
    case 0:
        abort();
    case 1:
        cmd->prp1 = cpu_to_le64(pagelist[0]);
        cmd->prp2 = 0;
        break;
    case 2:
        cmd->prp1 = cpu_to_le64(pagelist[0]);
        cmd->prp2 = cpu_to_le64(pagelist[1]);;
        break;
    default:
        cmd->prp1 = cpu_to_le64(pagelist[0]);
        cmd->prp2 = cpu_to_le64(req->prp_list_iova);
        for (i = 0; i < entries - 1; ++i) {
            pagelist[i] = cpu_to_le64(pagelist[i + 1]);
        }
        pagelist[entries] = 0;
        break;
    }
    return 0;
}

typedef struct {
    Coroutine *co;
    int ret;
    AioContext *ctx;
} NVMeCoData;

static void nvme_rw_cb_bh(void *opaque)
{
    NVMeCoData *data = opaque;
    qemu_coroutine_enter(data->co);
}

static void nvme_rw_cb(void *opaque, int ret)
{
    NVMeCoData *data = opaque;
    data->ret = ret;
    if (!data->co) {
        /* The rw coroutine hasn't yielded, don't try to enter. */
        return;
    }
    if (qemu_coroutine_self() != data->co) {
        qemu_coroutine_enter(data->co);
    } else {
        aio_bh_schedule_oneshot(data->ctx, nvme_rw_cb_bh, data);
    }
}

static coroutine_fn int nvme_co_prw_aligned(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            QEMUIOVector *qiov,
                                            bool is_write)
{
    int r;
    BDRVNVMeState *s = bs->opaque;
    NVMeQueuePair *ioq = s->queues[1];
    NVMeRequest *req;
    NVMeCommand cmd = {
        .opcode = is_write ? NVME_CMD_WRITE : NVME_CMD_READ,
        .nsid = cpu_to_le32(s->nsid),
        .cdw10 = cpu_to_le32((offset >> BDRV_SECTOR_BITS) & 0xFFFFFFFF),
        .cdw11 = cpu_to_le32(((offset >> BDRV_SECTOR_BITS) >> 32) & 0xFFFFFFFF),
        .cdw12 = cpu_to_le32(((bytes >> BDRV_SECTOR_BITS) - 1) & 0xFFFF),
    };
    NVMeCoData data = {
        .ctx = bdrv_get_aio_context(bs),
        .ret = -EINPROGRESS,
    };

    DPRINTF(">>> NVMe %s offset %lx bytes %lx qiov[%d]\n",
            is_write ? "write" : "read",
            offset, bytes, qiov->niov);
    assert(s->nr_queues > 1);
    while (true) {
        req = nvme_get_free_req(ioq);
        if (req) {
            break;
        }
        DPRINTF("nvme wait req\n");
        qemu_co_queue_wait(&ioq->wait_queue);
        DPRINTF("nvme wait req done\n");
    }

    r = nvme_cmd_map_qiov(bs, &cmd, req, qiov);
    if (r) {
        return r;
    }
    nvme_submit_command(s, ioq, req, &cmd, nvme_rw_cb, &data);

    data.co = qemu_coroutine_self();
    while (data.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }

    DPRINTF("<<< NVMe %s offset %lx bytes %lx ret %d\n",
            is_write ? "write" : "read",
            offset, bytes, data.ret);
    return data.ret;
}

static inline bool nvme_qiov_aligned(BlockDriverState *bs,
                                     const QEMUIOVector *qiov)
{
    int i;
    BDRVNVMeState *s = bs->opaque;

    for (i = 0; i < qiov->niov; ++i) {
        if (!QEMU_PTR_IS_ALIGNED(qiov->iov[i].iov_base, s->page_size) ||
            !QEMU_IS_ALIGNED(qiov->iov[i].iov_len, s->page_size)) {
            return false;
        }
    }
    return true;
}

static inline coroutine_fn
int nvme_co_prw(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                QEMUIOVector *qiov, bool is_write)
{
    int r;
    uint8_t *buf;
    QEMUIOVector local_qiov;

    assert(offset % BDRV_SECTOR_SIZE == 0);
    assert(bytes % BDRV_SECTOR_SIZE == 0);
    if (nvme_qiov_aligned(bs, qiov)) {
        return nvme_co_prw_aligned(bs, offset, bytes, qiov, is_write);
    }
    buf = qemu_try_blockalign(bs, bytes);

    if (!buf) {
        return -ENOMEM;
    }
    qemu_iovec_init(&local_qiov, 1);
    qemu_iovec_add(&local_qiov, buf, bytes);
    r = nvme_co_prw_aligned(bs, offset, bytes, &local_qiov, is_write);
    qemu_iovec_destroy(&local_qiov);
    if (!r) {
        qemu_iovec_from_buf(qiov, 0, buf, bytes);
    }
    qemu_vfree(buf);
    return r;
}

static coroutine_fn int nvme_co_preadv(BlockDriverState *bs,
                                       uint64_t offset, uint64_t bytes,
                                       QEMUIOVector *qiov, int flags)
{
    return nvme_co_prw(bs, offset, bytes, qiov, false);
}

static coroutine_fn int nvme_co_pwritev(BlockDriverState *bs,
                                        uint64_t offset, uint64_t bytes,
                                        QEMUIOVector *qiov, int flags)
{
    return nvme_co_prw(bs, offset, bytes, qiov, true);
}

static coroutine_fn int nvme_co_flush(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;
    NVMeQueuePair *ioq = s->queues[1];
    NVMeRequest *req;
    NVMeCommand cmd = {
        .opcode = NVME_CMD_FLUSH,
        .nsid = cpu_to_le32(s->nsid),
    };
    NVMeCoData data = {
        .ctx = bdrv_get_aio_context(bs),
        .ret = -EINPROGRESS,
    };

    assert(s->nr_queues > 1);
    while (true) {
        req = nvme_get_free_req(ioq);
        if (req) {
            break;
        }
        qemu_co_queue_wait(&ioq->wait_queue);
    }

    nvme_submit_command(s, ioq, req, &cmd, nvme_rw_cb, &data);

    data.co = qemu_coroutine_self();
    if (data.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }

    return data.ret;
}


static int nvme_reopen_prepare(BDRVReopenState *reopen_state,
                               BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static int64_t coroutine_fn nvme_co_get_block_status(BlockDriverState *bs,
                                                     int64_t sector_num,
                                                     int nb_sectors, int *pnum,
                                                     BlockDriverState **file)
{
    *pnum = nb_sectors;
    *file = bs;

    return BDRV_BLOCK_OFFSET_VALID | (sector_num << BDRV_SECTOR_BITS);
}

static void nvme_refresh_filename(BlockDriverState *bs, QDict *opts)
{
    QINCREF(opts);
    qdict_del(opts, "filename");

    if (!qdict_size(opts)) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename), "%s://",
                 bs->drv->format_name);
    }

    qdict_put(opts, "driver", qstring_from_str(bs->drv->format_name));
    bs->full_open_options = opts;
}

static void nvme_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVNVMeState *s = bs->opaque;

    /* XXX: other fields from identify command/controller capacity? */
    bs->bl.opt_mem_alignment = s->page_size;
    /* XXX: are there other limits? */
    bs->bl.request_alignment = s->page_size;
    bs->bl.max_transfer = s->max_transfer;
}

static void nvme_detach_aio_context(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;

    aio_set_event_notifier(bdrv_get_aio_context(bs), &s->event_notifier,
                           false, NULL, NULL);
}

static void nvme_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    BDRVNVMeState *s = bs->opaque;

    aio_set_event_notifier(new_context, &s->event_notifier,
                           false, nvme_handle_event, nvme_poll_cb);
}

static void nvme_aio_plug(BlockDriverState *bs)
{
    BDRVNVMeState *s = bs->opaque;
    s->plugged++;
}

static void nvme_aio_unplug(BlockDriverState *bs)
{
    int i;
    BDRVNVMeState *s = bs->opaque;
    assert(s->plugged);
    if (!--s->plugged) {
        for (i = 1; i < s->nr_queues; i++) {
            nvme_kick(s, s->queues[i]);
            nvme_process_completion(s, s->queues[i]);
        }
    }
}

static BlockDriver bdrv_nvme = {
    .format_name              = "nvme",
    .protocol_name            = "nvme",
    .instance_size            = sizeof(BDRVNVMeState),

    .bdrv_parse_filename      = nvme_parse_filename,
    .bdrv_file_open           = nvme_file_open,
    .bdrv_close               = nvme_close,
    .bdrv_getlength           = nvme_getlength,

    .bdrv_co_preadv           = nvme_co_preadv,
    .bdrv_co_pwritev          = nvme_co_pwritev,
    .bdrv_co_flush_to_disk    = nvme_co_flush,
    .bdrv_reopen_prepare      = nvme_reopen_prepare,

    .bdrv_co_get_block_status = nvme_co_get_block_status,

    .bdrv_refresh_filename    = nvme_refresh_filename,
    .bdrv_refresh_limits      = nvme_refresh_limits,

    .bdrv_detach_aio_context  = nvme_detach_aio_context,
    .bdrv_attach_aio_context  = nvme_attach_aio_context,

    .bdrv_io_plug             = nvme_aio_plug,
    .bdrv_io_unplug           = nvme_aio_unplug,
};

static void bdrv_nvme_init(void)
{
    bdrv_register(&bdrv_nvme);
}

block_init(bdrv_nvme_init);

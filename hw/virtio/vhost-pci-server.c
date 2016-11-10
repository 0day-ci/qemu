/*
 * Vhost-pci server
 *
 * Copyright Intel Corp. 2016
 *
 * Authors:
 * Wei Wang    <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <qemu/osdep.h>
#include <qemu/thread.h>
#include <qemu/main-loop.h>
#include <qemu/bitops.h>
#include <qemu/bitmap.h>
#include <qemu/sockets.h>
#include <linux/virtio_net.h>
#include "sysemu/char.h"
#include "qapi/error.h"
#include "hw/virtio/vhost-pci-server.h"
#include "qemu/option.h"
#include "monitor/qdev.h"
#include "hw/virtio/vhost-user.h"
#include "hw/qdev.h"

#define VHOST_PCI_FEATURE_BITS (1ULL << VIRTIO_F_VERSION_1)

#define VHOST_PCI_NET_FEATURE_BITS (1ULL << VIRTIO_NET_F_MRG_RXBUF) | \
                                   (1ULL << VIRTIO_NET_F_CTRL_VQ) | \
                                   (1ULL << VIRTIO_NET_F_MQ)

#define VHOST_USER_SET_PEER_CONNECTION_OFF  0
#define VHOST_USER_SET_PEER_CONNECTION_ON   1
#define VHOST_USER_SET_PEER_CONNECTION_INIT 2

VhostPCIServer *vp_server;

QemuOptsList qemu_vhost_pci_server_opts = {
    .name = "vhost-pci-server",
    .implied_opt_name = "chardev",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_vhost_pci_server_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end of list */ }
    },
};

static int vhost_pci_server_write(CharDriverState *chr, VhostUserMsg *msg)
{
    int size = msg->size + VHOST_USER_HDR_SIZE;

    if (!msg)
        return 0;

    msg->flags &= ~VHOST_USER_VERSION_MASK;
    msg->flags |= VHOST_USER_VERSION;

    return qemu_chr_fe_write_all_n(chr, msg->conn_id,
                                 (const uint8_t *)msg, size) == size ? 0 : -1;
}

PeerConnectionTable *vp_server_find_table_ent(const char *dev_id)
{
    int i;
    PeerConnectionTable *ent;
    uint64_t max_connections = vp_server->chr->max_connections;

    for (i = 0; i < max_connections; i++) {
        ent = &vp_server->peer_table[i];
        if (!strcmp(dev_id, ent->dev_id))
            return ent;
    }
    return NULL;
}

static void vhost_pci_init_peer_table(uint64_t id)
{
    PeerConnectionTable *ent = &vp_server->peer_table[id];

    ent->peer_feature_bits |= 1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
    QLIST_INIT(&ent->vq_list);
    ent->vq_num = 0;
}

static int vhost_pci_get_conn_id(CharDriverState *chr, VhostUserMsg *msg)
{
    unsigned long *conn_bitmap = chr->conn_bitmap;
    unsigned long *old_conn_bitmap = vp_server->old_conn_bitmap;
    uint64_t nbits = chr->max_connections;
    uint64_t id;
    int r;

    bitmap_xor(old_conn_bitmap, old_conn_bitmap, conn_bitmap, (long)nbits);

    for (id = find_first_bit(old_conn_bitmap, nbits); id < nbits;
         id = find_next_bit(old_conn_bitmap, nbits, id + 1)) {
        vhost_pci_init_peer_table(id);
        msg->conn_id = id;
        msg->payload.u64 = id;
        msg->size = sizeof(msg->payload.u64);
        msg->flags |= VHOST_USER_REPLY_MASK;
        r = vhost_pci_server_write(chr, msg);
    }
    bitmap_copy(old_conn_bitmap, conn_bitmap, (long)nbits);

    return r;
}

static int vhost_pci_get_peer_features(CharDriverState *chr, VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];
    msg->payload.u64 = ent->peer_feature_bits;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;
    return vhost_pci_server_write(chr, msg);
}

static int vhost_pci_get_queue_num(CharDriverState *chr, VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];
    switch (ent->virtio_id) {
    case VIRTIO_ID_NET:
        msg->payload.u64 = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX;
        break;
    default:
        printf("%s: device type not supported yet..\n", __func__);
    }
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;
    return vhost_pci_server_write(chr, msg);
}

static int vhost_pci_get_protocol_features(CharDriverState *chr, VhostUserMsg *msg)
{
    msg->payload.u64 = VHOST_USER_PROTOCOL_FEATURES;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;
    return vhost_pci_server_write(chr, msg);
}

static void vhost_pci_set_protocol_features(VhostUserMsg *msg)
{
    vp_server->protocol_features = msg->payload.u64;
}

static int vhost_pci_device_create(uint64_t conn_id)
{
    PeerConnectionTable *ent = &vp_server->peer_table[conn_id];
    Error *local_err = NULL;
    QemuOpts *opts;
    DeviceState *dev;
    char params[50];

    switch (ent->virtio_id) {
    case VIRTIO_ID_NET:
        sprintf(params, "driver=vhost-pci-net-pci,id=vhost-pci-%ld", conn_id);
        sprintf(ent->dev_id, "vhost-pci-%ld", conn_id);
        break;
    default:
         printf("%s: device type not supported yet..\n", __func__);
    }

    opts = qemu_opts_parse_noisily(qemu_find_opts("device"), params, true);
    dev = qdev_device_add(opts, &local_err);
    if (!dev) {
        qemu_opts_del(opts);
        return -1;
    }
    object_unref(OBJECT(dev));
    return 0;
}

static void vhost_pci_set_device_info(VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];
    DeviceInfo *info = &msg->payload.dev_info;

    memcpy(ent->uuid, info->uuid, sizeof(uuid_t));
    ent->virtio_id = info->virtio_id;
    switch (ent->virtio_id) {
    case VIRTIO_ID_NET:
        ent->peer_feature_bits |= (VHOST_PCI_FEATURE_BITS | VHOST_PCI_NET_FEATURE_BITS);
        break;
    default:
        printf("%s: device type not supported yet..\n", __func__);
    }
}

static void vhost_pci_set_peer_feature_bits(VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];

    ent->peer_feature_bits = msg->payload.u64;
}

static uint64_t vhost_pci_peer_mem_size_get(VhostUserMemory *peer_mem)
{
    int i;
    uint64_t total_size;
    uint32_t nregions = peer_mem->nregions;
    VhostUserMemoryRegion *regions = peer_mem->regions;

    for (i = 0; i < nregions; i++) {
        total_size += regions[i].memory_size;
    }

    return total_size;
}

static int vhost_pci_set_mem_table(uint64_t conn_id, VhostUserMemory *peer_mem, int *fds)
{
    int i;
    void *mr_qva;
    PeerConnectionTable *ent = &vp_server->peer_table[conn_id];
    uint32_t nregions = peer_mem->nregions;
    VhostUserMemoryRegion *peer_mr = peer_mem->regions;
    MemoryRegion *bar_mr = g_malloc(sizeof(MemoryRegion));
    MemoryRegion *mr = g_malloc(nregions * sizeof(MemoryRegion));
    uint64_t bar_size = 2 * vhost_pci_peer_mem_size_get(peer_mem);
    uint64_t bar_map_offset = 0;

    bar_size = pow2ceil(bar_size);
    memory_region_init(bar_mr, NULL, "Peer Memory", bar_size);

    for (i = 0; i < nregions; i++) {
        mr_qva = mmap(NULL, peer_mr[i].memory_size + peer_mr[i].mmap_offset,
                      PROT_READ | PROT_READ, MAP_SHARED, fds[i], 0);
        if (mr_qva == MAP_FAILED) {
            printf("%s called: map failed \n", __func__);
            return -1;
        }
        mr_qva += peer_mr[i].mmap_offset;
        memory_region_init_ram_ptr(&mr[i], NULL, "Peer Memory", peer_mr[i].memory_size, mr_qva);
        memory_region_add_subregion(bar_mr, bar_map_offset, &mr[i]);
        bar_map_offset += peer_mr[i].memory_size;
    }
    ent->bar_mr = bar_mr;
    ent->bar_map_offset = bar_map_offset;

    return 0;
}

static void vhost_pci_alloc_peer_vring_info(uint64_t conn_id)
{
    PeerConnectionTable *ent = &vp_server->peer_table[conn_id];
    PeerVirtqInfo *virtq_info = g_malloc0(sizeof(PeerVirtqInfo));
    QLIST_INSERT_HEAD(&ent->vq_list, virtq_info, node);
    ent->vq_num++;
}

static void vhost_pci_set_vring_num(VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];
    PeerVirtqInfo *virtq_info = QLIST_FIRST(&ent->vq_list);

    virtq_info->vring_num = msg->payload.u64;
}

static void vhost_pci_set_vring_base(VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];
    PeerVirtqInfo *virtq_info = QLIST_FIRST(&ent->vq_list);

    virtq_info->last_avail_idx = msg->payload.u64;
}

static void vhost_pci_set_vring_addr(VhostUserMsg *msg)
{
    PeerConnectionTable *ent = &vp_server->peer_table[msg->conn_id];
    PeerVirtqInfo *virtq_info = QLIST_FIRST(&ent->vq_list);
    memcpy(&virtq_info->addr, &msg->payload.addr,
           sizeof(struct vhost_vring_addr));
}

static void vhost_pci_set_vring_kick(uint64_t conn_id, int fd)
{
    PeerConnectionTable *ent = &vp_server->peer_table[conn_id];
    PeerVirtqInfo *virtq_info = QLIST_FIRST(&ent->vq_list);
    if (!virtq_info)
        virtq_info->kickfd = fd;
}

static void vhost_pci_set_vring_call(uint64_t conn_id, int fd)
{
    PeerConnectionTable *ent = &vp_server->peer_table[conn_id];
    PeerVirtqInfo *virtq_info = QLIST_FIRST(&ent->vq_list);
    if (virtq_info)
        virtq_info->callfd = fd;
}

static void vhost_pci_set_peer_connection(VhostUserMsg *msg)
{
    uint64_t cmd = msg->payload.u64;
    uint64_t conn_id = msg->conn_id;

    switch (cmd) {
    case VHOST_USER_SET_PEER_CONNECTION_INIT:
        vhost_pci_device_create(conn_id);
        break;
    default:
        printf("%s called: cmd %lu not supported yet \n", __func__, cmd);
    }
}

static void vhost_pci_server_read(void *opaque, const uint8_t *buf, int size)
{
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    CharDriverState *chr = (CharDriverState *)opaque;
    int fds[8], fd_num;

    if (size != VHOST_USER_HDR_SIZE) {
        printf("Wrong message size received %d\n", size);
        return;
    }
    memcpy(p, buf, VHOST_USER_HDR_SIZE);

    if (msg.size) {
        p += VHOST_USER_HDR_SIZE;
        size = qemu_chr_fe_read_all_n(chr, msg.conn_id, p, msg.size);
        if (size != msg.size) {
            printf("Wrong message size received %d != %d\n",
                           size, msg.size);
            return;
        }
    }

    if (msg.request > VHOST_USER_MAX)
        printf("vhost read incorrect msg \n");

    switch(msg.request) {
    case VHOST_USER_GET_CONN_ID:
        vhost_pci_get_conn_id(chr, &msg);
        break;
    case VHOST_USER_GET_FEATURES:
        vhost_pci_get_peer_features(chr, &msg);
        break;
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        vhost_pci_get_protocol_features(chr, &msg);
        break;
    case VHOST_USER_SET_PROTOCOL_FEATURES:
        vhost_pci_set_protocol_features(&msg);
        break;
    case VHOST_USER_SET_DEV_INFO:
        vhost_pci_set_device_info(&msg);
        break;
    case VHOST_USER_GET_QUEUE_NUM:
        vhost_pci_get_queue_num(chr, &msg);
        break;
    case VHOST_USER_SET_OWNER:
        break;
    case VHOST_USER_SET_FEATURES:
        vhost_pci_set_peer_feature_bits(&msg);
        break;
    case VHOST_USER_SET_VRING_NUM:
        vhost_pci_alloc_peer_vring_info(msg.conn_id);
        vhost_pci_set_vring_num(&msg);
        break;
    case VHOST_USER_SET_VRING_BASE:
        vhost_pci_set_vring_base(&msg);
        break;
    case VHOST_USER_SET_VRING_ADDR:
        vhost_pci_set_vring_addr(&msg);
        break;
    case VHOST_USER_SET_VRING_KICK:
        /* consume the fd */
        qemu_chr_fe_get_msgfds_n(chr, msg.conn_id, fds, 1);
        printf("VHOST_USER_SET_VRING_KICK called:..kickfd = %d\n", fds[0]);
        vhost_pci_set_vring_kick(msg.conn_id, fds[0]);
        /*
         * This is a non-blocking eventfd.
         * The receive function forces it to be blocking,
         * so revert it back to non-blocking.
         */
        qemu_set_nonblock(fds[0]);
        break;
    case VHOST_USER_SET_VRING_CALL:
        /* consume the fd */
        qemu_chr_fe_get_msgfds_n(chr, msg.conn_id, fds, 1);
        vhost_pci_set_vring_call(msg.conn_id, fds[0]);
        /*
         * This is a non-blocking eventfd.
         * The receive function forces it to be blocking,
         * so revert it back to non-blocking.
         */
        qemu_set_nonblock(fds[0]);
        break;
    case VHOST_USER_SET_MEM_TABLE:
        fd_num = qemu_chr_fe_get_msgfds_n(chr, msg.conn_id,
                                          fds, sizeof(fds) / sizeof(int));
        printf("VHOST_USER_SET_MEM_TABLE: fd = %d \n", fd_num);
        vhost_pci_set_mem_table(msg.conn_id, &msg.payload.memory, fds);
        break;
    case VHOST_USER_SET_PEER_CONNECTION:
        vhost_pci_set_peer_connection(&msg);
        break;
    default:
        printf("default called..msg->request = %d \n", msg.request);
        break;
    }
}

static int vhost_pci_server_can_read(void *opaque)
{
    return VHOST_USER_HDR_SIZE;
}

static void vhost_pci_server_event(void *opaque, int event)
{
    switch (event) {
    case CHR_EVENT_OPENED:
        printf("vhost_pci_server_event called.. \n");
        break;
    case CHR_EVENT_CLOSED:
        printf("vhost_pci_server_event called: event close..\n");
        break;
    }
}

static CharDriverState *vhost_pci_server_parse_chardev(const char *id)
{
    CharDriverState *chr = qemu_chr_find(id);
    if (chr == NULL) {
        printf("chardev \"%s\" not found", id);
        return NULL;
    }

    qemu_chr_fe_claim_no_fail(chr);

    return chr;
}

int vhost_pci_server_init(QemuOpts *opts)
{
    CharDriverState *chr;
    const char *chardev_id = qemu_opt_get(opts, "chardev");
    uint64_t max_connections;

    vp_server = (VhostPCIServer *)malloc(sizeof(VhostPCIServer));

    chr = vhost_pci_server_parse_chardev(chardev_id);
    if (!chr) {
        return -1;
    }
    max_connections = chr->max_connections;

    qemu_chr_add_handlers(chr, vhost_pci_server_can_read, vhost_pci_server_read, vhost_pci_server_event, chr);

    vp_server->chr = chr;

    vp_server->peer_table = (PeerConnectionTable *)g_malloc0(max_connections * sizeof(PeerConnectionTable));

    vp_server->old_conn_bitmap = bitmap_new(max_connections);

    return 0;
}

int vhost_pci_server_cleanup(void)
{
    free(vp_server);
    printf("vhost_pci_server_cleanup called.. \n");
    return 0;
}

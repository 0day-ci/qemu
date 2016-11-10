#ifndef QEMU_VHOST_PCI_SERVER_H
#define QEMU_VHOST_PCI_SERVER_H

#include <uuid/uuid.h>
#include <linux/vhost.h>

typedef struct PeerVirtqInfo {
    int kickfd;
    int callfd;
    uint32_t vring_num;
    uint16_t last_avail_idx;
    struct vhost_vring_addr addr;
    QLIST_ENTRY(PeerVirtqInfo) node;
} PeerVirtqInfo;

typedef struct PeerConnectionTable {
    char dev_id[30];
    uuid_t uuid;
    uint16_t virtio_id;
    uint32_t bar_id;
    MemoryRegion *bar_mr;
    uint64_t bar_map_offset;
    uint64_t peer_feature_bits;
    void *opaque;
    uint16_t vq_num;
    QLIST_HEAD(, PeerVirtqInfo) vq_list;
} PeerConnectionTable;

typedef struct VhostPCIServer {
    CharDriverState *chr;
    uint64_t protocol_features;
    unsigned long *old_conn_bitmap;
    /* a table indexed by the peer connection id */
    PeerConnectionTable *peer_table;
} VhostPCIServer;

extern VhostPCIServer *vp_server;

extern int vhost_pci_server_init(QemuOpts *opts);

extern int vhost_pci_server_cleanup(void);

extern PeerConnectionTable *vp_server_find_table_ent(const char *dev_id);

#endif

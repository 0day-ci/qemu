/*
 * Virtio Network Device
 *
 * Copyright Intel, Corp. 2016
 *
 * Authors:
 *  Wei Wang   <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VHOST_PCI_NET_H
#define _QEMU_VHOST_PCI_NET_H

#include "standard-headers/linux/vhost_pci_net.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost-pci-server.h"

#define TYPE_VHOST_PCI_NET "vhost-pci-net-device"
#define VHOST_PCI_NET(obj) \
        OBJECT_CHECK(VhostPCINet, (obj), TYPE_VHOST_PCI_NET)

typedef struct VhostPCINet {
    VirtIODevice parent_obj;

    VirtQueue *cvq_rx, *cvq_tx;
    VirtQueue **rxqs;
    uint64_t device_features;
    size_t config_size;
    uint16_t max_rxq_num;
} VhostPCINet;

void vhost_pci_net_set_max_rxqs(VhostPCINet *dev, uint16_t num);

void vhost_pci_net_init_device_features(VhostPCINet *vpnet, uint64_t features);

#endif

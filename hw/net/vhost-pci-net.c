/*
 * vhost-pci-net support
 *
 * Copyright Intel, Inc. 2016
 *
 * Authors:
 *  Wei Wang <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "hw/virtio/virtio.h"
#include "net/net.h"
#include "net/checksum.h"
#include "net/tap.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio-net.h"
//#include "net/vhost_net.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/qmp/qjson.h"
#include "qapi-event.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-pci-net.h"

void vhost_pci_net_set_max_rxqs(VhostPCINet *vpnet, uint16_t num)
{
    vpnet->max_rxq_num = num;
}

static void vpnet_handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vpnet_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vhost_pci_net_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    int i;

    virtio_init(vdev, "vhost-pci-net", VIRTIO_ID_VHOST_PCI_NET, vpnet->config_size);

    /* control quque: host to guest */
    vpnet->cvq_rx = virtio_add_queue(vdev, 32, vpnet_handle_input);
    /* control quque: guest to host */
    vpnet->cvq_tx = virtio_add_queue(vdev, 32, vpnet_handle_output);

    vpnet->rxqs = g_malloc0(sizeof(VirtQueue *) * vpnet->max_rxq_num);
    for (i = 0; i < vpnet->max_rxq_num; i++) {
        vpnet->rxqs[i] = virtio_add_queue(vdev, 256, vpnet_handle_output);
    }
}

static void vhost_pci_net_device_unrealize(DeviceState *dev, Error **errp)
{
}

static void vhost_pci_net_get_config(VirtIODevice *vdev, uint8_t *config)
{
}

static void vhost_pci_net_set_config(VirtIODevice *vdev, const uint8_t *config)
{
}

void vhost_pci_net_init_device_features(VhostPCINet *vpnet, uint64_t features)
{
    vpnet->device_features = features;
}

static uint64_t vhost_pci_net_get_features(VirtIODevice *vdev, uint64_t features, Error **errp)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);

    return vpnet->device_features;
}

static void vhost_pci_net_set_features(VirtIODevice *vdev, uint64_t features)
{
}

static void vhost_pci_net_instance_init(Object *obj)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(obj);

    /*
     * The default config_size is sizeof(struct vhost_pci_net_config).
     * Can be overriden with vhost_pci_net_set_config_size.
     */
    vpnet->config_size = sizeof(struct vhost_pci_net_config);
}

static Property vhost_pci_net_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_pci_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_pci_net_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = vhost_pci_net_device_realize;
    vdc->unrealize = vhost_pci_net_device_unrealize;
    vdc->get_config = vhost_pci_net_get_config;
    vdc->set_config = vhost_pci_net_set_config;
    vdc->get_features = vhost_pci_net_get_features;
    vdc->set_features = vhost_pci_net_set_features;
//    vdc->bad_features = vhost_pci_net_bad_features;
//    vdc->reset = vhost_pci_net_reset;
//    vdc->set_status = vhost_pci_net_set_status;
//    vdc->guest_notifier_mask = vhost_pci_net_guest_notifier_mask;
//    vdc->guest_notifier_pending = vhost_pci_net_guest_notifier_pending;
//    vdc->load = vhost_pci_net_load_device;
//    vdc->save = vhost_pci_net_save_device;
}

static const TypeInfo vhost_pci_net_info = {
    .name = TYPE_VHOST_PCI_NET,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VhostPCINet),
    .instance_init = vhost_pci_net_instance_init,
    .class_init = vhost_pci_net_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_pci_net_info);
}

type_init(virtio_register_types)

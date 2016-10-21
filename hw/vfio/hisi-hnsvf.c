/*
 * Hisilicon HNS Virtual Function VFIO device
 *
 * Copyright Huawei Limited, 2016
 *
 * Authors:
 *  Rick Song <songwenjun@huawei.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-hisi-hnsvf.h"

static void hisi_hnsvf_realize(DeviceState *dev, Error **errp)
{
    VFIOPlatformDevice *vdev = VFIO_PLATFORM_DEVICE(dev);
    VFIOHisiHnsvfDeviceClass *k = VFIO_HISI_HNSVF_DEVICE_GET_CLASS(dev);

    vdev->compat = g_strdup("hisilicon,hnsvf-v2");

    k->parent_realize(dev, errp);
}

static const VMStateDescription vfio_platform_hisi_hnsvf_vmstate = {
    .name = TYPE_VFIO_HISI_HNSVF,
    .unmigratable = 1,
};

static void vfio_hisi_hnsvf_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VFIOHisiHnsvfDeviceClass *vcxc =
        VFIO_HISI_HNSVF_DEVICE_CLASS(klass);
    vcxc->parent_realize = dc->realize;
    dc->realize = hisi_hnsvf_realize;
    dc->desc = "VFIO HISI HNSVF";
    dc->vmsd = &vfio_platform_hisi_hnsvf_vmstate;
}

static const TypeInfo vfio_hisi_hnsvf_dev_info = {
    .name = TYPE_VFIO_HISI_HNSVF,
    .parent = TYPE_VFIO_PLATFORM,
    .instance_size = sizeof(VFIOHisiHnsvfDevice),
    .class_init = vfio_hisi_hnsvf_class_init,
    .class_size = sizeof(VFIOHisiHnsvfDeviceClass),
};

static void register_hisi_hnsvf_dev_type(void)
{
    type_register_static(&vfio_hisi_hnsvf_dev_info);
}

type_init(register_hisi_hnsvf_dev_type)

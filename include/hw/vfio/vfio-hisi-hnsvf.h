/*
 * VFIO Hisilicon HNS Virtual Function device
 *
 * Copyright Hisilicon Limited, 2016
 *
 * Authors:
 *  Rick Song <songwenjun@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef HW_VFIO_VFIO_HISI_HNSVF_H
#define HW_VFIO_VFIO_HISI_HNSVF_H

#include "hw/vfio/vfio-platform.h"

#define TYPE_VFIO_HISI_HNSVF "vfio-hisi-hnsvf"

/**
 * This device exposes:
 * - 5 MMIO regions: MAC, PCS, SerDes Rx/Tx regs,
     SerDes Integration Registers 1/2 & 2/2
 * - 2 level sensitive IRQs and optional DMA channel IRQs
 */
struct VFIOHisiHnsvfDevice {
    VFIOPlatformDevice vdev;
};

typedef struct VFIOHisiHnsvfDevice VFIOHisiHnsvfDevice;

struct VFIOHisiHnsvfDeviceClass {
    /*< private >*/
    VFIOPlatformDeviceClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
};

typedef struct VFIOHisiHnsvfDeviceClass VFIOHisiHnsvfDeviceClass;

#define VFIO_HISI_HNSVF_DEVICE(obj) \
     OBJECT_CHECK(VFIOHisiHnsvfDevice, (obj), TYPE_VFIO_HISI_HNSVF)
#define VFIO_HISI_HNSVF_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VFIOHisiHnsvfDeviceClass, (klass), \
                        TYPE_VFIO_HISI_HNSVF)
#define VFIO_HISI_HNSVF_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VFIOHisiHnsvfDeviceClass, (obj), \
                      TYPE_VFIO_HISI_HNSVF)

#endif

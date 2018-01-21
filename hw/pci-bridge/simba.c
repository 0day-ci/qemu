/*
 * QEMU Simba PCI bridge
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2012,2013 Artyom Tarasenko
 * Copyright (c) 2018 Mark Cave-Ayland
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-bridge/simba.h"

/*
 * Chipset docs:
 * APB: "Advanced PCI Bridge (APB) User's Manual",
 * http://www.sun.com/processors/manuals/805-1251.pdf
 */

static void apb_pci_bridge_realize(PCIDevice *dev, Error **errp)
{
    /*
     * command register:
     * According to PCI bridge spec, after reset
     *   bus master bit is off
     *   memory space enable bit is off
     * According to manual (805-1251.pdf).
     *   the reset value should be zero unless the boot pin is tied high
     *   (which is true) and thus it should be PCI_COMMAND_MEMORY.
     */
    PBMPCIBridge *br = PBM_PCI_BRIDGE(dev);

    pci_bridge_initfn(dev, TYPE_PCI_BUS);

    pci_set_word(dev->config + PCI_COMMAND, PCI_COMMAND_MEMORY);
    pci_set_word(dev->config + PCI_STATUS,
                 PCI_STATUS_FAST_BACK | PCI_STATUS_66MHZ |
                 PCI_STATUS_DEVSEL_MEDIUM);

    /* Allow 32-bit IO addresses */
    pci_set_word(dev->config + PCI_IO_BASE, PCI_IO_RANGE_TYPE_32);
    pci_set_word(dev->config + PCI_IO_LIMIT, PCI_IO_RANGE_TYPE_32);
    pci_set_word(dev->wmask + PCI_IO_BASE_UPPER16, 0xffff);
    pci_set_word(dev->wmask + PCI_IO_LIMIT_UPPER16, 0xffff);

    pci_bridge_update_mappings(PCI_BRIDGE(br));
}

static void pbm_pci_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = apb_pci_bridge_realize;
    k->exit = pci_bridge_exitfn;
    k->vendor_id = PCI_VENDOR_ID_SUN;
    k->device_id = PCI_DEVICE_ID_SUN_SIMBA;
    k->revision = 0x11;
    k->config_write = pci_bridge_write_config;
    k->is_bridge = 1;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->reset = pci_bridge_reset;
    dc->vmsd = &vmstate_pci_device;
}

static const TypeInfo pbm_pci_bridge_info = {
    .name          = TYPE_PBM_PCI_BRIDGE,
    .parent        = TYPE_PCI_BRIDGE,
    .class_init    = pbm_pci_bridge_class_init,
    .instance_size = sizeof(PBMPCIBridge),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pbm_register_types(void)
{
    type_register_static(&pbm_pci_bridge_info);
}

type_init(pbm_register_types)

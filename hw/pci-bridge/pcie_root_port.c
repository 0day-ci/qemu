/*
 * Generic PCI Express Root Port emulation
 *
 * Copyright (C) 2016 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * Most of the code was migrated from hw/pci-bridge/ioh3420.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci/msi.h"

#define PCIE_ROOT_PORT_MSI_VECTORS              2
#define PCIE_ROOT_PORT_MSI_SUPPORTED_FLAGS      PCI_MSI_FLAGS_MASKBIT
#define PCIE_ROOT_PORT_AER_OFFSET               0x100

#define TYPE_PCIE_ROOT_PORT_DEV                 "pcie-root-port"


/*
 * If two MSI vector are allocated, Advanced Error Interrupt Message Number
 * is 1. otherwise 0.
 * 17.12.5.10 RPERRSTS,  32:27 bit Advanced Error Interrupt Message Number.
 */
static uint8_t rp_aer_vector(const PCIDevice *d)
{
    switch (msi_nr_vectors_allocated(d)) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
    case 8:
    case 16:
    case 32:
    default:
        break;
    }
    abort();
    return 0;
}

static void rp_aer_vector_update(PCIDevice *d)
{
    pcie_aer_root_set_vector(d, rp_aer_vector(d));
}

static void rp_write_config(PCIDevice *d, uint32_t address,
                            uint32_t val, int len)
{
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);

    pci_bridge_write_config(d, address, val, len);
    rp_aer_vector_update(d);
    pcie_cap_slot_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);
}

static void rp_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);

    rp_aer_vector_update(d);
    pcie_cap_root_reset(d);
    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_cap_arifwd_reset(d);
    pcie_aer_root_reset(d);
    pci_bridge_reset(qdev);
    pci_bridge_disable_base_limit(d);
}

static void rp_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    PCIESlot *s = PCIE_SLOT(d);
    PCIDeviceClass *dc = PCI_DEVICE_GET_CLASS(d);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);
    int rc;
    Error *local_err = NULL;

    pci_config_set_interrupt_pin(d->config, 1);
    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = pci_bridge_ssvid_init(d, rpc->ssvid_offset, dc->vendor_id, rpc->ssid);
    if (rc < 0) {
        error_setg(errp, "Can't init SSV ID, error %d", rc);
        goto err_bridge;
    }

    rc = msi_init(d, rpc->msi_offset, PCIE_ROOT_PORT_MSI_VECTORS,
                  PCIE_ROOT_PORT_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  PCIE_ROOT_PORT_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT,
                  &local_err);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
        error_propagate(errp, local_err);
        goto err_bridge;
    }

    rc = pcie_cap_init(d, rpc->exp_offset, PCI_EXP_TYPE_ROOT_PORT, p->port);
    if (rc < 0) {
        error_setg(errp, "Can't add Root Port capability, error %d", rc);
        goto err_msi;
    }

    pcie_cap_arifwd_init(d);
    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s->slot);
    pcie_cap_root_init(d);

    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        error_setg(errp, "Can't add chassis slot, error %d", rc);
        goto err_pcie_cap;
    }

    rc = pcie_aer_init(d, rpc->aer_offset, PCI_ERR_SIZEOF);
    if (rc < 0) {
        error_setg(errp, "Can't init AER, error %d", rc);
        goto err;
    }
    pcie_aer_root_init(d);
    rp_aer_vector_update(d);

    return;

err:
    pcie_chassis_del_slot(s);
err_pcie_cap:
    pcie_cap_exit(d);
err_msi:
    msi_uninit(d);
err_bridge:
    pci_bridge_exitfn(d);
}

static void rp_exit(PCIDevice *d)
{
    PCIESlot *s = PCIE_SLOT(d);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    msi_uninit(d);
    pci_bridge_exitfn(d);
}

static Property rp_props[] = {
    DEFINE_PROP_BIT(COMPAT_PROP_PCP, PCIDevice, cap_present,
                    QEMU_PCIE_SLTCAP_PCP_BITNR, true),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    k->is_express = 1;
    k->is_bridge = 1;
    k->config_write = rp_write_config;
    k->realize = rp_realize;
    k->exit = rp_exit;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->reset = rp_reset;
    dc->props = rp_props;
    rpc->aer_offset = PCIE_ROOT_PORT_AER_OFFSET;
}

static const TypeInfo rp_info = {
    .name          = TYPE_PCIE_ROOT_PORT,
    .parent        = TYPE_PCIE_SLOT,
    .class_init    = rp_class_init,
    .abstract      = true,
    .class_size = sizeof(PCIERootPortClass),
};

static const VMStateDescription vmstate_rp_dev = {
    .name = "pcie-root-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCIE_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static void rp_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_RP;
    dc->desc = "PCI Express Root Port";
    dc->vmsd = &vmstate_rp_dev;
}

static const TypeInfo rp_dev_info = {
    .name          = TYPE_PCIE_ROOT_PORT_DEV,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .class_init    = rp_dev_class_init,
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
    type_register_static(&rp_dev_info);
}

type_init(rp_register_types)

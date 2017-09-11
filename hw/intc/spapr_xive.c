/*
 * QEMU PowerPC sPAPR XIVE model
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/dma.h"
#include "monitor/monitor.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/spapr_xive.h"

#include "xive-internal.h"

/*
 * Main XIVE object
 */

void spapr_xive_reset(void *dev)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    int i;

    /* SBEs are initialized to 0b01 which corresponds to "ints off" */
    memset(xive->sbe, 0x55, xive->sbe_size);

    /* Validate all available IVEs in the IRQ number space. It would
     * be more correct to validate only the allocated IRQs but this
     * would require some callback routine from the spapr machine into
     * XIVE. To be done later.
     */
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];
        ive->w = IVE_VALID | IVE_MASKED;
    }

    /* clear all EQs */
    memset(xive->eqt, 0, xive->nr_eqs * sizeof(XiveEQ));
}

static void spapr_xive_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    Object *obj;
    Error *err = NULL;
    int i;

    if (!xive->nr_targets) {
        error_setg(errp, "Number of interrupt targets needs to be greater 0");
        return;
    }

    /* We need to be able to allocate at least the IPIs */
    if (!xive->nr_irqs || xive->nr_irqs < xive->nr_targets) {
        error_setg(errp, "Number of interrupts too small");
        return;
    }

    /* Retrieve SPAPR ICS source to share the IRQ number allocator */
    obj = object_property_get_link(OBJECT(dev), "ics", &err);
    if (!obj) {
        error_setg(errp, "%s: required link 'ics' not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    xive->ics = ICS_BASE(obj);

    /* Allocate the last IRQ numbers for the IPIs */
    for (i = xive->nr_irqs - xive->nr_targets; i < xive->nr_irqs; i++) {
        ics_set_irq_type(xive->ics, i, false);
    }

    /* Allocate SBEs (State Bit Entry). 2 bits, so 4 entries per byte */
    xive->sbe_size = DIV_ROUND_UP(xive->nr_irqs, 4);
    xive->sbe = g_malloc0(xive->sbe_size);

    /* Allocate the IVT (Interrupt Virtualization Table) */
    xive->ivt = g_malloc0(xive->nr_irqs * sizeof(XiveIVE));

    /* Allocate the EQDT (Event Queue Descriptor Table), 8 priorities
     * for each thread in the system */
    xive->nr_eqs = xive->nr_targets * XIVE_EQ_PRIORITY_COUNT;
    xive->eqt = g_malloc0(xive->nr_eqs * sizeof(XiveEQ));

    qemu_register_reset(spapr_xive_reset, dev);
}

static const VMStateDescription vmstate_spapr_xive_ive = {
    .name = "xive/ive",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT64(w, XiveIVE),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_xive_eq = {
    .name = "xive/eq",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(w0, XiveEQ),
        VMSTATE_UINT32(w1, XiveEQ),
        VMSTATE_UINT32(w2, XiveEQ),
        VMSTATE_UINT32(w3, XiveEQ),
        VMSTATE_UINT32(w4, XiveEQ),
        VMSTATE_UINT32(w5, XiveEQ),
        VMSTATE_UINT32(w6, XiveEQ),
        VMSTATE_UINT32(w7, XiveEQ),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_xive = {
    .name = "xive",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32_ALLOC(sbe, sPAPRXive, sbe_size, 0,
                                    vmstate_info_uint8, uint8_t),
        VMSTATE_STRUCT_VARRAY_UINT32_ALLOC(ivt, sPAPRXive, nr_irqs, 0,
                                    vmstate_spapr_xive_ive, XiveIVE),
        VMSTATE_STRUCT_VARRAY_UINT32_ALLOC(eqt, sPAPRXive, nr_eqs, 0,
                                    vmstate_spapr_xive_eq, XiveEQ),
        VMSTATE_END_OF_LIST()
    },
};

static Property spapr_xive_properties[] = {
    DEFINE_PROP_UINT32("nr-irqs", sPAPRXive, nr_irqs, 0),
    DEFINE_PROP_UINT32("nr-targets", sPAPRXive, nr_targets, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = spapr_xive_realize;
    dc->props = spapr_xive_properties;
    dc->desc = "sPAPR XIVE interrupt controller";
    dc->vmsd = &vmstate_xive;
}

static const TypeInfo spapr_xive_info = {
    .name = TYPE_SPAPR_XIVE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_class_init,
};

static void spapr_xive_register_types(void)
{
    type_register_static(&spapr_xive_info);
}

type_init(spapr_xive_register_types)

XiveIVE *spapr_xive_get_ive(sPAPRXive *xive, uint32_t idx)
{
    return idx < xive->nr_irqs ? &xive->ivt[idx] : NULL;
}

XiveEQ *spapr_xive_get_eq(sPAPRXive *xive, uint32_t idx)
{
    return idx < xive->nr_eqs ? &xive->eqt[idx] : NULL;
}

/* TODO: improve EQ indexing. This is very simple and relies on the
 * fact that target (CPU) numbers start at 0 and are contiguous. It
 * should be OK for sPAPR.
 */
bool spapr_xive_eq_for_target(sPAPRXive *xive, uint32_t target,
                              uint8_t priority, uint32_t *out_eq_idx)
{
    if (priority > XIVE_PRIORITY_MAX || target >= xive->nr_targets) {
        return false;
    }

    if (out_eq_idx) {
        *out_eq_idx = target + priority;
    }

    return true;
}

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


/*
 * Main XIVE object
 */

static void spapr_xive_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);

    if (!xive->nr_targets) {
        error_setg(errp, "Number of interrupt targets needs to be greater 0");
        return;
    }

    /* We need to be able to allocate at least the IPIs */
    if (!xive->nr_irqs || xive->nr_irqs < xive->nr_targets) {
        error_setg(errp, "Number of interrupts too small");
        return;
    }
}

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

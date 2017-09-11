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

#ifndef PPC_SPAPR_XIVE_H
#define PPC_SPAPR_XIVE_H

#include <hw/sysbus.h>

typedef struct sPAPRXive sPAPRXive;
typedef struct XiveIVE XiveIVE;
typedef struct XiveEQ XiveEQ;
typedef struct ICSState ICSState;

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(sPAPRXive, (obj), TYPE_SPAPR_XIVE)

struct sPAPRXive {
    SysBusDevice parent;

    /* Properties */
    uint32_t     nr_targets;
    uint32_t     nr_irqs;

    /* IRQ */
    ICSState     *ics;  /* XICS source inherited from the SPAPR machine */
    qemu_irq     *qirqs;

    /* Interrupt source flags */
#define XIVE_SRC_H_INT_ESB     (1ull << (63 - 60))
#define XIVE_SRC_LSI           (1ull << (63 - 61))
#define XIVE_SRC_TRIGGER       (1ull << (63 - 62))
#define XIVE_SRC_STORE_EOI     (1ull << (63 - 63))
    uint32_t     flags;

    /* XIVE internal tables */
    uint8_t      *sbe;
    uint32_t     sbe_size;
    XiveIVE      *ivt;
    XiveEQ       *eqt;
    uint32_t     nr_eqs;

    /* ESB memory region */
    uint32_t     esb_shift;
    hwaddr       esb_base;
    MemoryRegion esb_mr;
    MemoryRegion esb_iomem;

    /* TIMA memory region */
    uint32_t     tm_shift;
    hwaddr       tm_base;
    MemoryRegion tm_iomem;
};

typedef struct sPAPRMachineState sPAPRMachineState;

void spapr_xive_hcall_init(sPAPRMachineState *spapr);
void spapr_xive_populate(sPAPRXive *xive, void *fdt, uint32_t phandle);
void spapr_xive_mmio_map(sPAPRXive *xive);

#endif /* PPC_SPAPR_XIVE_H */

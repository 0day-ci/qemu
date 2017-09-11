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

    /* XIVE internal tables */
    uint8_t      *sbe;
    uint32_t     sbe_size;
    XiveIVE      *ivt;
    XiveEQ       *eqt;
    uint32_t     nr_eqs;
};

#endif /* PPC_SPAPR_XIVE_H */

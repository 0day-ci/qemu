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


static uint64_t spapr_xive_icp_accept(ICPState *icp)
{
    return 0;
}

static void spapr_xive_icp_set_cppr(ICPState *icp, uint8_t cppr)
{
    if (cppr > XIVE_PRIORITY_MAX) {
        cppr = 0xff;
    }

    icp->tima_os[TM_CPPR] = cppr;
}

/*
 * Thread Interrupt Management Area MMIO
 */
static uint64_t spapr_xive_tm_read_special(ICPState *icp, hwaddr offset,
                                     unsigned size)
{
    uint64_t ret = -1;

    if (offset == TM_SPC_ACK_OS_REG && size == 2) {
        ret = spapr_xive_icp_accept(icp);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA read @%"
                      HWADDR_PRIx" size %d\n", offset, size);
    }

    return ret;
}

static uint64_t spapr_xive_tm_read(void *opaque, hwaddr offset, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    ICPState *icp = ICP(cpu->intc);
    uint64_t ret = -1;
    int i;

    if (offset >= TM_SPC_ACK_EBB) {
        return spapr_xive_tm_read_special(icp, offset, size);
    }

    if (offset & TM_QW1_OS) {
        switch (size) {
        case 1:
        case 2:
        case 4:
        case 8:
            if (QEMU_IS_ALIGNED(offset, size)) {
                ret = 0;
                for (i = 0; i < size; i++) {
                    ret |= icp->tima[offset + i] << (8 * i);
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "XIVE: invalid TIMA read alignment @%"
                              HWADDR_PRIx" size %d\n", offset, size);
            }
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: does handle non-OS TIMA ring @%"
                      HWADDR_PRIx"\n", offset);
    }

    return ret;
}

static bool spapr_xive_tm_is_readonly(uint8_t index)
{
    /* Let's be optimistic and prepare ground for HV mode support */
    switch (index) {
    case TM_QW1_OS + TM_CPPR:
        return false;
    default:
        return true;
    }
}

static void spapr_xive_tm_write_special(ICPState *icp, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    /* TODO: support TM_SPC_SET_OS_PENDING */

    /* TODO: support TM_SPC_ACK_OS_EL */
}

static void spapr_xive_tm_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    ICPState *icp = ICP(cpu->intc);
    int i;

    if (offset >= TM_SPC_ACK_EBB) {
        spapr_xive_tm_write_special(icp, offset, value, size);
        return;
    }

    if (offset & TM_QW1_OS) {
        switch (size) {
        case 1:
            if (offset == TM_QW1_OS + TM_CPPR) {
                spapr_xive_icp_set_cppr(icp, value & 0xff);
            }
            break;
        case 4:
        case 8:
            if (QEMU_IS_ALIGNED(offset, size)) {
                for (i = 0; i < size; i++) {
                    if (!spapr_xive_tm_is_readonly(offset + i)) {
                        icp->tima[offset + i] = (value >> (8 * i)) & 0xff;
                    }
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA write @%"
                              HWADDR_PRIx" size %d\n", offset, size);
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA write @%"
                          HWADDR_PRIx" size %d\n", offset, size);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: does handle non-OS TIMA ring @%"
                      HWADDR_PRIx"\n", offset);
    }
}


static const MemoryRegionOps spapr_xive_tm_ops = {
    .read = spapr_xive_tm_read,
    .write = spapr_xive_tm_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void spapr_xive_eq_push(XiveEQ *eq, uint32_t data)
{
    uint64_t qaddr_base = (((uint64_t)(eq->w2 & 0x0fffffff)) << 32) | eq->w3;
    uint32_t qsize = GETFIELD(EQ_W0_QSIZE, eq->w0);
    uint32_t qindex = GETFIELD(EQ_W1_PAGE_OFF, eq->w1);
    uint32_t qgen = GETFIELD(EQ_W1_GENERATION, eq->w1);

    uint64_t qaddr = qaddr_base + (qindex << 2);
    uint32_t qdata = cpu_to_be32((qgen << 31) | (data & 0x7fffffff));
    uint32_t qentries = 1 << (qsize + 10);

    if (dma_memory_write(&address_space_memory, qaddr, &qdata, sizeof(qdata))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed to write EQ data @0x%"
                      HWADDR_PRIx "\n", __func__, qaddr);
        return;
    }

    qindex = (qindex + 1) % qentries;
    if (qindex == 0) {
        qgen ^= 1;
        eq->w1 = SETFIELD(EQ_W1_GENERATION, eq->w1, qgen);
    }
    eq->w1 = SETFIELD(EQ_W1_PAGE_OFF, eq->w1, qindex);
}

static void spapr_xive_irq(sPAPRXive *xive, int srcno)
{
    XiveIVE *ive;
    XiveEQ *eq;
    uint32_t eq_idx;
    uint32_t priority;

    ive = spapr_xive_get_ive(xive, srcno);
    if (!ive || !(ive->w & IVE_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", srcno);
        return;
    }

    if (ive->w & IVE_MASKED) {
        return;
    }

    /* Find our XiveEQ */
    eq_idx = GETFIELD(IVE_EQ_INDEX, ive->w);
    eq = spapr_xive_get_eq(xive, eq_idx);
    if (!eq) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No EQ for LISN %d\n", srcno);
        return;
    }

    if (eq->w0 & EQ_W0_ENQUEUE) {
        spapr_xive_eq_push(eq, GETFIELD(IVE_EQ_DATA, ive->w));
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: !ENQUEUE not implemented\n");
    }

    if (!(eq->w0 & EQ_W0_UCOND_NOTIFY)) {
        qemu_log_mask(LOG_UNIMP, "XIVE: !UCOND_NOTIFY not implemented\n");
    }

    if (GETFIELD(EQ_W6_FORMAT_BIT, eq->w6) == 0) {
        priority = GETFIELD(EQ_W7_F0_PRIORITY, eq->w7);

        /* The EQ is masked. Can this happen ?  */
        if (priority == 0xff) {
            return;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: w7 format1 not implemented\n");
    }
}

/*
 * "magic" Event State Buffer (ESB) MMIO offsets.
 *
 * Each interrupt source has a 2-bit state machine called ESB
 * which can be controlled by MMIO. It's made of 2 bits, P and
 * Q. P indicates that an interrupt is pending (has been sent
 * to a queue and is waiting for an EOI). Q indicates that the
 * interrupt has been triggered while pending.
 *
 * This acts as a coalescing mechanism in order to guarantee
 * that a given interrupt only occurs at most once in a queue.
 *
 * When doing an EOI, the Q bit will indicate if the interrupt
 * needs to be re-triggered.
 *
 * The following offsets into the ESB MMIO allow to read or
 * manipulate the PQ bits. They must be used with an 8-bytes
 * load instruction. They all return the previous state of the
 * interrupt (atomically).
 *
 * Additionally, some ESB pages support doing an EOI via a
 * store at 0 and some ESBs support doing a trigger via a
 * separate trigger page.
 */
#define XIVE_ESB_GET            0x800
#define XIVE_ESB_SET_PQ_00      0xc00
#define XIVE_ESB_SET_PQ_01      0xd00
#define XIVE_ESB_SET_PQ_10      0xe00
#define XIVE_ESB_SET_PQ_11      0xf00

#define XIVE_ESB_VAL_P          0x2
#define XIVE_ESB_VAL_Q          0x1

#define XIVE_ESB_RESET          0x0
#define XIVE_ESB_PENDING        XIVE_ESB_VAL_P
#define XIVE_ESB_QUEUED         (XIVE_ESB_VAL_P | XIVE_ESB_VAL_Q)
#define XIVE_ESB_OFF            XIVE_ESB_VAL_Q

static uint8_t spapr_xive_pq_get(sPAPRXive *xive, uint32_t idx)
{
    uint32_t byte = idx / 4;
    uint32_t bit  = (idx % 4) * 2;

    assert(byte < xive->sbe_size);

    return (xive->sbe[byte] >> bit) & 0x3;
}

static uint8_t spapr_xive_pq_set(sPAPRXive *xive, uint32_t idx, uint8_t pq)
{
    uint32_t byte = idx / 4;
    uint32_t bit  = (idx % 4) * 2;
    uint8_t old, new;

    assert(byte < xive->sbe_size);

    old = xive->sbe[byte];

    new = xive->sbe[byte] & ~(0x3 << bit);
    new |= (pq & 0x3) << bit;

    xive->sbe[byte] = new;

    return (old >> bit) & 0x3;
}

static bool spapr_xive_pq_eoi(sPAPRXive *xive, uint32_t srcno)
{
    uint8_t old_pq = spapr_xive_pq_get(xive, srcno);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_PENDING:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_QUEUED:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_OFF:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

static bool spapr_xive_pq_trigger(sPAPRXive *xive, uint32_t srcno)
{
    uint8_t old_pq = spapr_xive_pq_get(xive, srcno);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_PENDING:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_QUEUED);
        return true;
    case XIVE_ESB_QUEUED:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_QUEUED);
        return true;
    case XIVE_ESB_OFF:
        spapr_xive_pq_set(xive, srcno, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

/*
 * XIVE Interrupt Source MMIOs
 */
static void spapr_xive_source_eoi(sPAPRXive *xive, uint32_t srcno)
{
    ICSIRQState *irq = &xive->ics->irqs[srcno];

    if (irq->flags & XICS_FLAGS_IRQ_LSI) {
        irq->status &= ~XICS_STATUS_SENT;
    }
}

/* TODO: handle second page
 *
 * Some HW use a separate page for trigger. We only support the case
 * in which the trigger can be done in the same page as the EOI.
 */
static uint64_t spapr_xive_esb_read(void *opaque, hwaddr addr, unsigned size)
{
    sPAPRXive *xive = SPAPR_XIVE(opaque);
    uint32_t offset = addr & 0xF00;
    uint32_t srcno = addr >> xive->esb_shift;
    XiveIVE *ive;
    uint64_t ret = -1;

    ive = spapr_xive_get_ive(xive, srcno);
    if (!ive || !(ive->w & IVE_VALID))  {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", srcno);
        goto out;
    }

    switch (offset) {
    case 0:
        spapr_xive_source_eoi(xive, srcno);

        /* return TRUE or FALSE depending on PQ value */
        ret = spapr_xive_pq_eoi(xive, srcno);
        break;

    case XIVE_ESB_GET:
        ret = spapr_xive_pq_get(xive, srcno);
        break;

    case XIVE_ESB_SET_PQ_00:
    case XIVE_ESB_SET_PQ_01:
    case XIVE_ESB_SET_PQ_10:
    case XIVE_ESB_SET_PQ_11:
        ret = spapr_xive_pq_set(xive, srcno, (offset >> 8) & 0x3);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB addr %d\n", offset);
    }

out:
    return ret;
}

static void spapr_xive_esb_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    sPAPRXive *xive = SPAPR_XIVE(opaque);
    uint32_t offset = addr & 0xF00;
    uint32_t srcno = addr >> xive->esb_shift;
    XiveIVE *ive;
    bool notify = false;

    ive = spapr_xive_get_ive(xive, srcno);
    if (!ive || !(ive->w & IVE_VALID))  {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", srcno);
        return;
    }

    switch (offset) {
    case 0:
        /* TODO: should we trigger even if the IVE is masked ? */
        notify = spapr_xive_pq_trigger(xive, srcno);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB write addr %d\n",
                      offset);
        return;
    }

    if (notify && !(ive->w & IVE_MASKED)) {
        qemu_irq_pulse(xive->qirqs[srcno]);
    }
}

static const MemoryRegionOps spapr_xive_esb_ops = {
    .read = spapr_xive_esb_read,
    .write = spapr_xive_esb_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * XIVE Interrupt Source
 */
static void spapr_xive_source_set_irq_msi(sPAPRXive *xive, int srcno, int val)
{
    if (val) {
        spapr_xive_irq(xive, srcno);
    }
}

static void spapr_xive_source_set_irq_lsi(sPAPRXive *xive, int srcno, int val)
{
    ICSIRQState *irq = &xive->ics->irqs[srcno];

    if (val) {
        irq->status |= XICS_STATUS_ASSERTED;
    } else {
        irq->status &= ~XICS_STATUS_ASSERTED;
    }

    if (irq->status & XICS_STATUS_ASSERTED
        && !(irq->status & XICS_STATUS_SENT)) {
        irq->status |= XICS_STATUS_SENT;
        spapr_xive_irq(xive, srcno);
    }
}

static void spapr_xive_source_set_irq(void *opaque, int srcno, int val)
{
    sPAPRXive *xive = SPAPR_XIVE(opaque);
    ICSIRQState *irq = &xive->ics->irqs[srcno];

    if (irq->flags & XICS_FLAGS_IRQ_LSI) {
        spapr_xive_source_set_irq_lsi(xive, srcno, val);
    } else {
        spapr_xive_source_set_irq_msi(xive, srcno, val);
    }
}

/*
 * Main XIVE object
 */
#define P9_MMIO_BASE     0x006000000000000ull

/* VC BAR contains set translations for the ESBs and the EQs. */
#define VC_BAR_DEFAULT   0x10000000000ull
#define VC_BAR_SIZE      0x08000000000ull
#define ESB_SHIFT        16 /* One 64k page. OPAL has two */

/* Thread Interrupt Management Area MMIO */
#define TM_BAR_DEFAULT   0x30203180000ull
#define TM_SHIFT         16
#define TM_BAR_SIZE      (XIVE_TM_RING_COUNT * (1 << TM_SHIFT))

static uint64_t spapr_xive_esb_default_read(void *p, hwaddr offset,
                                            unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " [%u]\n",
                  __func__, offset, size);
    return 0;
}

static void spapr_xive_esb_default_write(void *opaque, hwaddr offset,
                                         uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " <- 0x%" PRIx64 " [%u]\n",
                  __func__, offset, value, size);
}

static const MemoryRegionOps spapr_xive_esb_default_ops = {
    .read = spapr_xive_esb_default_read,
    .write = spapr_xive_esb_default_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

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
    xive->qirqs = qemu_allocate_irqs(spapr_xive_source_set_irq, xive,
                                     xive->nr_irqs);

    /* Allocate the last IRQ numbers for the IPIs */
    for (i = xive->nr_irqs - xive->nr_targets; i < xive->nr_irqs; i++) {
        ics_set_irq_type(xive->ics, i, false);
    }

    /* All sources are emulated under the XIVE object and share the
     * same characteristic */
    xive->flags = XIVE_SRC_TRIGGER;

    /* Allocate SBEs (State Bit Entry). 2 bits, so 4 entries per byte */
    xive->sbe_size = DIV_ROUND_UP(xive->nr_irqs, 4);
    xive->sbe = g_malloc0(xive->sbe_size);

    /* Allocate the IVT (Interrupt Virtualization Table) */
    xive->ivt = g_malloc0(xive->nr_irqs * sizeof(XiveIVE));

    /* Allocate the EQDT (Event Queue Descriptor Table), 8 priorities
     * for each thread in the system */
    xive->nr_eqs = xive->nr_targets * XIVE_EQ_PRIORITY_COUNT;
    xive->eqt = g_malloc0(xive->nr_eqs * sizeof(XiveEQ));

    /* VC BAR. That's the full window but we will only map the
     * subregions in use. */
    xive->esb_base = (P9_MMIO_BASE | VC_BAR_DEFAULT);
    xive->esb_shift = ESB_SHIFT;

    /* Install default memory region handlers to log bogus access */
    memory_region_init_io(&xive->esb_mr, NULL, &spapr_xive_esb_default_ops,
                          NULL, "xive.esb.full", VC_BAR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->esb_mr);

    /* Install the ESB memory region in the overall one */
    memory_region_init_io(&xive->esb_iomem, OBJECT(xive), &spapr_xive_esb_ops,
                          xive, "xive.esb",
                          (1ull << xive->esb_shift) * xive->nr_irqs);
    memory_region_add_subregion(&xive->esb_mr, 0, &xive->esb_iomem);

    /* TM BAR. Same address for each chip */
    xive->tm_base = (P9_MMIO_BASE | TM_BAR_DEFAULT);
    xive->tm_shift = TM_SHIFT;

    memory_region_init_io(&xive->tm_iomem, OBJECT(xive), &spapr_xive_tm_ops,
                          xive, "xive.tm", TM_BAR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->tm_iomem);

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

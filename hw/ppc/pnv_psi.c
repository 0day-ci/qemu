/*
 * QEMU PowerNV PowerPC PSI interface
 *
 * Copyright (c) 2016, IBM Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "target-ppc/cpu.h"
#include "qemu/log.h"
#include "qapi/error.h"

#include "exec/address-spaces.h"

#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_psi.h"

#include <libfdt.h>

#define PSIHB_XSCOM_FIR_RW      0x00
#define PSIHB_XSCOM_FIR_AND     0x01
#define PSIHB_XSCOM_FIR_OR      0x02
#define PSIHB_XSCOM_FIRMASK_RW  0x03
#define PSIHB_XSCOM_FIRMASK_AND 0x04
#define PSIHB_XSCOM_FIRMASK_OR  0x05
#define PSIHB_XSCOM_FIRACT0     0x06
#define PSIHB_XSCOM_FIRACT1     0x07
#define PSIHB_XSCOM_BAR         0x0a
#define   PSIHB_BAR_EN                  0x0000000000000001ull
#define PSIHB_XSCOM_FSPBAR      0x0b
#define PSIHB_XSCOM_CR          0x0e
#define   PSIHB_CR_FSP_CMD_ENABLE       0x8000000000000000ull
#define   PSIHB_CR_FSP_MMIO_ENABLE      0x4000000000000000ull
#define   PSIHB_CR_FSP_IRQ_ENABLE       0x1000000000000000ull
#define   PSIHB_CR_FSP_ERR_RSP_ENABLE   0x0800000000000000ull
#define   PSIHB_CR_PSI_LINK_ENABLE      0x0400000000000000ull
#define   PSIHB_CR_FSP_RESET            0x0200000000000000ull
#define   PSIHB_CR_PSIHB_RESET          0x0100000000000000ull
#define   PSIHB_CR_PSI_IRQ              0x0000800000000000ull
#define   PSIHB_CR_FSP_IRQ              0x0000400000000000ull
#define   PSIHB_CR_FSP_LINK_ACTIVE      0x0000200000000000ull
          /* and more ... */
#define PSIHB_XSCOM_SEMR        0x0f
#define PSIHB_XSCOM_XIVR_PSI    0x10
#define   PSIHB_XIVR_SERVER_SH  40
#define   PSIHB_XIVR_SERVER_MSK (0xffffull << PSIHB_XIVR_SERVER_SH)
#define   PSIHB_XIVR_PRIO_SH    32
#define   PSIHB_XIVR_PRIO_MSK   (0xffull << PSIHB_XIVR_PRIO_SH)
#define   PSIHB_XIVR_SRC_SH             29
#define   PSIHB_XIVR_SRC_MSK    (0x7ull << PSIHB_XIVR_SRC_SH)
#define   PSIHB_XIVR_PENDING    0x01000000ull
#define PSIHB_XSCOM_SCR         0x12
#define PSIHB_XSCOM_CCR         0x13
#define PSIHB_XSCOM_DMA_UPADD   0x14
#define PSIHB_XSCOM_IRQ_STAT    0x15
#define  PSIHB_IRQ_STAT_OCC             0x0000001000000000ull
#define  PSIHB_IRQ_STAT_FSI             0x0000000800000000ull
#define  PSIHB_IRQ_STAT_LPCI2C          0x0000000400000000ull
#define  PSIHB_IRQ_STAT_LOCERR          0x0000000200000000ull
#define  PSIHB_IRQ_STAT_EXT             0x0000000100000000ull
#define PSIHB_XSCOM_XIVR_OCC    0x16
#define PSIHB_XSCOM_XIVR_FSI    0x17
#define PSIHB_XSCOM_XIVR_LPCI2C 0x18
#define PSIHB_XSCOM_XIVR_LOCERR 0x19
#define PSIHB_XSCOM_XIVR_EXT    0x1a
#define PSIHB_XSCOM_IRSN        0x1b
#define   PSIHB_IRSN_COMP_SH            45
#define   PSIHB_IRSN_COMP_MSK           (0x7ffffull << PSIHB_IRSN_COMP_SH)
#define   PSIHB_IRSN_IRQ_MUX            0x0000000800000000ull
#define   PSIHB_IRSN_IRQ_RESET          0x0000000400000000ull
#define   PSIHB_IRSN_DOWNSTREAM_EN      0x0000000200000000ull
#define   PSIHB_IRSN_UPSTREAM_EN        0x0000000100000000ull
#define   PSIHB_IRSN_COMPMASK_SH        13
#define   PSIHB_IRSN_COMPMASK_MSK       (0x7ffffull << PSIHB_IRSN_COMPMASK_SH)

#define PSIHB_MMIO_BAR          0x00
#define PSIHB_MMIO_FSPBAR       0x08
#define PSIHB_MMIO_CR           0x20
#define PSIHB_MMIO_SEMR         0x28
#define PSIHB_MMIO_XIVR_PSI     0x30
#define PSIHB_MMIO_SCR          0x40
#define PSIHB_MMIO_CCR          0x48
#define PSIHB_MMIO_DMA_UPADD    0x50
#define PSIHB_MMIO_IRQ_STAT     0x58
#define PSIHB_MMIO_XIVR_OCC     0x60
#define PSIHB_MMIO_XIVR_FSI     0x68
#define PSIHB_MMIO_XIVR_LPCI2C  0x70
#define PSIHB_MMIO_XIVR_LOCERR  0x78
#define PSIHB_MMIO_XIVR_EXT     0x80
#define PSIHB_MMIO_IRSN         0x88
#define PSIHB_MMIO_MAX          0x100

static const uint32_t psi_mmio_to_xscom[PSIHB_MMIO_MAX / 8] = {
        [PSIHB_MMIO_BAR / 8]         = PSIHB_XSCOM_BAR,
        [PSIHB_MMIO_FSPBAR / 8]      = PSIHB_XSCOM_FSPBAR,
        [PSIHB_MMIO_CR / 8]          = PSIHB_XSCOM_CR,
        [PSIHB_MMIO_SCR / 8]         = PSIHB_XSCOM_SCR,
        [PSIHB_MMIO_CCR / 8]         = PSIHB_XSCOM_CCR,
        [PSIHB_MMIO_SEMR / 8]        = PSIHB_XSCOM_SEMR,
        [PSIHB_MMIO_XIVR_PSI / 8]    = PSIHB_XSCOM_XIVR_PSI,
        [PSIHB_MMIO_XIVR_OCC / 8]    = PSIHB_XSCOM_XIVR_OCC,
        [PSIHB_MMIO_XIVR_FSI / 8]    = PSIHB_XSCOM_XIVR_FSI,
        [PSIHB_MMIO_XIVR_LPCI2C / 8] = PSIHB_XSCOM_XIVR_LPCI2C,
        [PSIHB_MMIO_XIVR_LOCERR / 8] = PSIHB_XSCOM_XIVR_LOCERR,
        [PSIHB_MMIO_XIVR_EXT / 8]    = PSIHB_XSCOM_XIVR_EXT,
        [PSIHB_MMIO_IRQ_STAT / 8]    = PSIHB_XSCOM_IRQ_STAT,
        [PSIHB_MMIO_DMA_UPADD / 8]   = PSIHB_XSCOM_DMA_UPADD,
        [PSIHB_MMIO_IRSN / 8]        = PSIHB_XSCOM_IRSN,
};

static void pnv_psi_set_bar(PnvPsiController *psi, uint64_t bar)
{
    MemoryRegion *sysmem = get_system_memory();
    uint64_t old = psi->regs[PSIHB_XSCOM_BAR];

    psi->regs[PSIHB_XSCOM_BAR] = bar & 0x0003fffffff00001;

    /* Update MR, always remove it first */
    if (old & PSIHB_BAR_EN) {
        memory_region_del_subregion(sysmem, &psi->regs_mr);
    }
    /* Then add it back if needed */
    if (bar & PSIHB_BAR_EN) {
        uint64_t addr = bar & 0x0003fffffff00000;
        memory_region_add_subregion(sysmem, addr, &psi->regs_mr);
    }
}

static void pnv_psi_update_fsp_mr(PnvPsiController *psi)
{
    /* XXX Update FSP MR if/when we support FSP BAR */
}

static void pnv_psi_set_cr(PnvPsiController *psi, uint64_t cr)
{
    uint64_t old = psi->regs[PSIHB_XSCOM_CR];

    psi->regs[PSIHB_XSCOM_CR] = cr & 0x0003ffff00000000;

    /* Check some bit changes */
    if ((old ^ psi->regs[PSIHB_XSCOM_CR]) & PSIHB_CR_FSP_MMIO_ENABLE) {
        pnv_psi_update_fsp_mr(psi);
    }
}

static void pnv_psi_set_irsn(PnvPsiController *psi, uint64_t val)
{
    uint32_t offset;
    ICSState *ics = &psi->ics;

    /* In this model we ignore the up/down enable bits for now
     * as SW doesn't use them (other than setting them at boot).
     * We ignore IRQ_MUX, its meaning isn't clear and we don't use
     * it and finally we ignore reset (XXX fix that ?)
     */
    psi->regs[PSIHB_XSCOM_IRSN] = val & (PSIHB_IRSN_COMP_MSK |
                                         PSIHB_IRSN_IRQ_MUX |
                                         PSIHB_IRSN_DOWNSTREAM_EN |
                                         PSIHB_IRSN_DOWNSTREAM_EN |
                                         PSIHB_IRSN_DOWNSTREAM_EN);

    /* We ignore the compare mask as well, our ICS emulation is too
     * simplistic to make any use if it, and we extract the offset
     * from the compare value
     */
    offset = (val & PSIHB_IRSN_COMP_MSK) >> PSIHB_IRSN_COMP_SH;
    ics->offset = offset;
}

static bool pnv_psi_irq_bits(PnvPsiController *psi, PnvPsiIrq irq,
                             uint32_t *out_xivr_reg,
                             uint32_t *out_stat_reg,
                             uint64_t *out_stat_bit)
{
    switch (irq) {
    case PSIHB_IRQ_PSI:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_PSI;
        *out_stat_reg = PSIHB_XSCOM_CR;
        *out_stat_bit = PSIHB_CR_PSI_IRQ;
        break;
    case PSIHB_IRQ_FSP:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_PSI;
        *out_stat_reg = PSIHB_XSCOM_CR;
        *out_stat_bit = PSIHB_CR_FSP_IRQ;
        break;
    case PSIHB_IRQ_OCC:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_OCC;
        *out_stat_reg = PSIHB_XSCOM_IRQ_STAT;
        *out_stat_bit = PSIHB_IRQ_STAT_OCC;
        break;
    case PSIHB_IRQ_FSI:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_FSI;
        *out_stat_reg = PSIHB_XSCOM_IRQ_STAT;
        *out_stat_bit = PSIHB_IRQ_STAT_FSI;
        break;
    case PSIHB_IRQ_LPC_I2C:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_LPCI2C;
        *out_stat_reg = PSIHB_XSCOM_IRQ_STAT;
        *out_stat_bit = PSIHB_IRQ_STAT_LPCI2C;
        break;
    case PSIHB_IRQ_LOCAL_ERR:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_LOCERR;
        *out_stat_reg = PSIHB_XSCOM_IRQ_STAT;
        *out_stat_bit = PSIHB_IRQ_STAT_LOCERR;
        break;
    case PSIHB_IRQ_EXTERNAL:
        *out_xivr_reg = PSIHB_XSCOM_XIVR_EXT;
        *out_stat_reg = PSIHB_XSCOM_IRQ_STAT;
        *out_stat_bit = PSIHB_IRQ_STAT_EXT;
        break;
    default:
        return false;
    }
    return true;
}

void pnv_psi_irq_set(PnvPsiController *psi, PnvPsiIrq irq, bool state)
{
    ICSState *ics = &psi->ics;
    uint32_t xivr_reg;
    uint32_t stat_reg;
    uint64_t stat_bit;
    uint32_t src;
    bool masked;

    if (!pnv_psi_irq_bits(psi, irq, &xivr_reg, &stat_reg, &stat_bit)) {
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: Unsupported irq %d\n", irq);
        return;
    }

    src = (psi->regs[xivr_reg] & PSIHB_XIVR_SRC_MSK) >> PSIHB_XIVR_SRC_SH;
    masked = (psi->regs[xivr_reg] & PSIHB_XIVR_PRIO_MSK) == PSIHB_XIVR_PRIO_MSK;
    if (state) {
        psi->regs[stat_reg] |= stat_bit;
        /* XXX optimization: check mask here. That means re-evaluating
         * when unmasking, thus TODO
         */
        qemu_irq_raise(ics->qirqs[src]);
    } else {
        psi->regs[stat_reg] &= ~stat_bit;

        /* FSP and PSI are muxed so don't lower if either still set */
        if (stat_reg != PSIHB_XSCOM_CR ||
            !(psi->regs[stat_reg] & (PSIHB_CR_PSI_IRQ | PSIHB_CR_FSP_IRQ))) {
            qemu_irq_lower(ics->qirqs[src]);
        } else {
            state = true;
        }
    }

    /* XXX Note about the emulation of the pending bit: This isn't
     * entirely correct. The pending bit should be cleared when the
     * EOI has been received. However, we don't have callbacks on
     * EOI (especially not under KVM) so no way to emulate that
     * properly, so instead we just set that bit as the logical
     * "output" of the XIVR (ie pending & !masked)
     * XXX TODO: Also update it on set_xivr
     */
    if (state && !masked) {
        psi->regs[xivr_reg] |= PSIHB_XIVR_PENDING;
    } else {
        psi->regs[xivr_reg] &= ~PSIHB_XIVR_PENDING;
    }
}

static void pnv_psi_set_xivr(PnvPsiController *psi, uint32_t reg, uint64_t val)
{
    ICSState *ics = &psi->ics;
    uint16_t server;
    uint8_t prio;
    uint8_t src;

    psi->regs[reg] = (psi->regs[reg] & PSIHB_XIVR_PENDING) |
            (val & (PSIHB_XIVR_SERVER_MSK |
                    PSIHB_XIVR_PRIO_MSK |
                    PSIHB_XIVR_SRC_MSK));
    val = psi->regs[reg];
    server = (val & PSIHB_XIVR_SERVER_MSK) >> PSIHB_XIVR_SERVER_SH;
    prio = (val & PSIHB_XIVR_PRIO_MSK) >> PSIHB_XIVR_PRIO_SH;
    src = (val & PSIHB_XIVR_SRC_MSK) >> PSIHB_XIVR_SRC_SH;
    if (src > PSIHB_IRQ_EXTERNAL) {
        /* XXX Generate error ? */
        return;
    }

    /*
     * Linux fills the irq xivr with the hw processor id plus the link
     * bits. shift back to get something valid.
     */
    server >>= 2;

    /* Now because of source remapping, weird things can happen
     * if you change the source number dynamically, our simple ICS
     * doesn't deal with remapping. So we just poke a different
     * ICS entry based on what source number was written. This will
     * do for now but a more accurate implementation would instead
     * use a fixed server/prio and a remapper of the generated irq.
     */
    ics_simple_write_xive(ics, src, server, prio, prio);
}

static bool pnv_psi_reg_read(PnvPsiController *psi, uint32_t offset,
                             uint64_t *out_val, bool mmio)
{
    switch (offset) {
    case PSIHB_XSCOM_FIR_RW:
    case PSIHB_XSCOM_FIRACT0:
    case PSIHB_XSCOM_FIRACT1:
    case PSIHB_XSCOM_BAR:
    case PSIHB_XSCOM_FSPBAR:
    case PSIHB_XSCOM_CR:
    case PSIHB_XSCOM_XIVR_PSI:
    case PSIHB_XSCOM_XIVR_OCC:
    case PSIHB_XSCOM_XIVR_FSI:
    case PSIHB_XSCOM_XIVR_LPCI2C:
    case PSIHB_XSCOM_XIVR_LOCERR:
    case PSIHB_XSCOM_XIVR_EXT:
    case PSIHB_XSCOM_IRQ_STAT:
    case PSIHB_XSCOM_SEMR:
    case PSIHB_XSCOM_DMA_UPADD:
    case PSIHB_XSCOM_IRSN:
        *out_val = psi->regs[offset];
        return true;
    }
    return false;
}

static bool pnv_psi_reg_write(PnvPsiController *psi, uint32_t offset,
                              uint64_t val, bool mmio)
{
    switch (offset) {
    case PSIHB_XSCOM_FIR_RW:
    case PSIHB_XSCOM_FIRACT0:
    case PSIHB_XSCOM_FIRACT1:
    case PSIHB_XSCOM_SEMR:
    case PSIHB_XSCOM_DMA_UPADD:
        psi->regs[offset] = val;
        return true;
    case PSIHB_XSCOM_FIR_OR:
        psi->regs[PSIHB_XSCOM_FIR_RW] |= val;
        return true;
    case PSIHB_XSCOM_FIR_AND:
        psi->regs[PSIHB_XSCOM_FIR_RW] &= val;
        return true;
    case PSIHB_XSCOM_BAR:
        /* Only XSCOM can write this one */
        if (!mmio) {
            pnv_psi_set_bar(psi, val);
        }
        return true;
    case PSIHB_XSCOM_FSPBAR:
        psi->regs[PSIHB_XSCOM_BAR] = val & 0x0003ffff00000000;
        pnv_psi_update_fsp_mr(psi);
        return true;
    case PSIHB_XSCOM_CR:
        pnv_psi_set_cr(psi, val);
        return true;
    case PSIHB_XSCOM_SCR:
        pnv_psi_set_cr(psi, psi->regs[PSIHB_XSCOM_CR] | val);
        return true;
    case PSIHB_XSCOM_CCR:
        pnv_psi_set_cr(psi, psi->regs[PSIHB_XSCOM_CR] & ~val);
        return true;
    case PSIHB_XSCOM_XIVR_PSI:
    case PSIHB_XSCOM_XIVR_OCC:
    case PSIHB_XSCOM_XIVR_FSI:
    case PSIHB_XSCOM_XIVR_LPCI2C:
    case PSIHB_XSCOM_XIVR_LOCERR:
    case PSIHB_XSCOM_XIVR_EXT:
        pnv_psi_set_xivr(psi, offset, val);
        return true;
    case PSIHB_XSCOM_IRQ_STAT:
        /* Read only, should we generate an error ? */
        return true;
    case PSIHB_XSCOM_IRSN:
        pnv_psi_set_irsn(psi, val);
        return true;
    }
    return false;
}

static uint64_t pnv_psi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPsiController *psi = opaque;
    uint32_t xscom_off;
    uint64_t val;

    if (size != 8) {
        goto fail;
    }

    addr &= (PNV_PSIHB_BAR_SIZE - 1);

    if (addr >= PSIHB_MMIO_MAX) {
        goto fail;
    }
    xscom_off = psi_mmio_to_xscom[addr / 8];
    if (xscom_off == 0) {
        goto fail;
    }
    if (pnv_psi_reg_read(psi, xscom_off, &val, true)) {
        return val;
    }
 fail:
    return 0xffffffffffffffffull;
}

static void pnv_psi_mmio_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PnvPsiController *psi = opaque;
    uint32_t xscom_off;

    if (size != 8) {
        return;
    }

    addr &= (PNV_PSIHB_BAR_SIZE - 1);

    if (addr >= PSIHB_MMIO_MAX) {
        return;
    }
    xscom_off = psi_mmio_to_xscom[addr / 8];
    if (xscom_off == 0) {
        return;
    }
    pnv_psi_reg_write(psi, xscom_off, val, true);
}

static const MemoryRegionOps psi_mmio_ops = {
    .read = pnv_psi_mmio_read,
    .write = pnv_psi_mmio_write,
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

static uint64_t pnv_psi_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPsiController *psi = PNV_PSI(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    pnv_psi_reg_read(psi, offset, &val, false);

    return val;
}

static void pnv_psi_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PnvPsiController *psi = PNV_PSI(opaque);
    uint32_t offset = addr >> 3;

    pnv_psi_reg_write(psi, offset, val, false);
}

static const MemoryRegionOps pnv_psi_xscom_ops = {
    .read = pnv_psi_xscom_read,
    .write = pnv_psi_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};


static void pnv_psi_init(Object *obj)
{
    PnvPsiController *psi = PNV_PSI(obj);

    object_initialize(&psi->ics, sizeof(psi->ics), TYPE_ICS_SIMPLE);
    object_property_add_child(obj, "ics-psi", OBJECT(&psi->ics), NULL);
}

static void pnv_psi_realize(DeviceState *dev, Error **errp)
{
    PnvPsiController *psi = PNV_PSI(dev);
    ICSState *ics = &psi->ics;
    Error *error = NULL;
    unsigned int i;

    /* Initialize MMIO region */
    memory_region_init_io(&psi->regs_mr, OBJECT(dev), &psi_mmio_ops, psi,
                          "psihb", PNV_PSIHB_BAR_SIZE);

    /* Default BAR. Use object properties ? */
    pnv_psi_set_bar(psi, PNV_PSIHB_BAR | PSIHB_BAR_EN);

    /* Default sources in XIVR */
    psi->regs[PSIHB_XSCOM_XIVR_PSI] = PSIHB_XIVR_PRIO_MSK |
            (0ull << PSIHB_XIVR_SRC_SH);
    psi->regs[PSIHB_XSCOM_XIVR_OCC] = PSIHB_XIVR_PRIO_MSK |
            (1ull << PSIHB_XIVR_SRC_SH);
    psi->regs[PSIHB_XSCOM_XIVR_FSI] = PSIHB_XIVR_PRIO_MSK |
            (2ull << PSIHB_XIVR_SRC_SH);
    psi->regs[PSIHB_XSCOM_XIVR_LPCI2C] = PSIHB_XIVR_PRIO_MSK |
            (3ull << PSIHB_XIVR_SRC_SH);
    psi->regs[PSIHB_XSCOM_XIVR_LOCERR] = PSIHB_XIVR_PRIO_MSK |
            (4ull << PSIHB_XIVR_SRC_SH);
    psi->regs[PSIHB_XSCOM_XIVR_EXT] = PSIHB_XIVR_PRIO_MSK |
            (5ull << PSIHB_XIVR_SRC_SH);

    /*
     * PSI interrupt control source
     * FIXME: nr_irqs should a property
     */
    ics->nr_irqs = PSI_NUM_INTERRUPTS;
    object_property_set_bool(OBJECT(ics), true, "realized", &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    for (i = 0; i < ics->nr_irqs; i++) {
        ics_set_irq_type(ics, i, true);
    }

    /* XScom region for PSI registers */
    memory_region_init_io(&psi->xscom_regs, OBJECT(dev), &pnv_psi_xscom_ops,
                psi, "xscom-psi", PNV_XSCOM_PSI_SIZE << 3);
}

static int pnv_psi_populate(PnvXScomInterface *dev, void *fdt, int xscom_offset)
{
    const char compat[] = "ibm,power8-psihb-x\0ibm,psihb-x";
    char *name;
    int offset;
    uint32_t lpc_pcba = PNV_XSCOM_PSI_BASE;
    uint32_t reg[] = {
        cpu_to_be32(lpc_pcba),
        cpu_to_be32(PNV_XSCOM_PSI_SIZE)
    };

    name = g_strdup_printf("psihb@%x", lpc_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));

    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 2)));
    _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat,
                      sizeof(compat))));
    return 0;
}


static void pnv_psi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xdc->populate = pnv_psi_populate;

    dc->realize = pnv_psi_realize;
}

static const TypeInfo pnv_psi_info = {
    .name          = TYPE_PNV_PSI,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPsiController),
    .instance_init = pnv_psi_init,
    .class_init    = pnv_psi_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_psi_register_types(void)
{
    type_register_static(&pnv_psi_info);
}

type_init(pnv_psi_register_types)

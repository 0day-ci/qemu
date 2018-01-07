/*
 *  RDMA device: Definitions of Backend Device structures
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RDMA_BACKEND_DEFS_H
#define RDMA_BACKEND_DEFS_H

#include <infiniband/verbs.h>
#include <qemu/thread.h>

typedef struct RdmaDeviceResources RdmaDeviceResources;

typedef struct RdmaBackendThread {
    QemuThread thread;
    QemuMutex mutex;
    bool run;
} RdmaBackendThread;

typedef struct RdmaBackendDev {
    PCIDevice *dev;
    RdmaBackendThread comp_thread;
    struct ibv_device *ib_dev;
    uint8_t port_num;
    struct ibv_context *context;
    struct ibv_comp_channel *channel;
    union ibv_gid gid;
    struct ibv_device_attr dev_attr;
    uint8_t backend_gid_idx;
    RdmaDeviceResources *rdma_dev_res;
} RdmaBackendDev;

typedef struct RdmaBackendPD {
    struct ibv_pd *ibpd;
} RdmaBackendPD;

typedef struct RdmaBackendMR {
    struct ibv_pd *ibpd;
    struct ibv_mr *ibmr;
} RdmaBackendMR;

typedef struct RdmaBackendCQ {
    RdmaBackendDev *backend_dev;
    struct ibv_cq *ibcq;
} RdmaBackendCQ;

typedef struct RdmaBackendQP {
    struct ibv_pd *ibpd;
    struct ibv_qp *ibqp;
} RdmaBackendQP;

#endif

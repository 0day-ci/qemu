/*
 * QTest testcase for loading, saving and deleting snapshots
 *
 * Copyright (c) 2017 Richard Palethorpe <richiejp@f-m.fm>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/libqos.h"

#define SS_EXEC_FMT "{ 'execute': '%s', 'arguments': { 'name': '%s' } }"

static void ss_op(const char *op, const char *name)
{
    char *qmp_cmd = NULL;

    qmp_cmd = g_strdup_printf(SS_EXEC_FMT, op, name);
    qmp_async(qmp_cmd);
    g_free(qmp_cmd);
}

static void save_snapshot(void)
{
    QDict *rsp;

    ss_op("save-snapshot", "test");
    qmp_eventwait("STOP");
    qmp_eventwait("RESUME");
    rsp = qmp_receive();
    g_assert(!qdict_haskey(rsp, "error"));
    QDECREF(rsp);
}

static void load_snapshot(void)
{
    QDict *rsp;

    ss_op("load-snapshot", "test");
    qmp_eventwait("STOP");
    qmp_eventwait("RESUME");
    rsp = qmp_receive();
    g_assert(!qdict_haskey(rsp, "error"));
    QDECREF(rsp);

    ss_op("load-snapshot", "does-not-exist");
    qmp_eventwait("STOP");
    rsp = qmp_receive();
    g_assert(qdict_haskey(rsp, "error"));
    QDECREF(rsp);
}

static void delete_snapshot(void)
{
    QDict *rsp;

    ss_op("delete-snapshot", "test");
    rsp = qmp_receive();
    g_assert(!qdict_haskey(rsp, "error"));
    QDECREF(rsp);
}

int main(int argc, char **argv)
{
    char timg[] = "/tmp/qtest-snapshot.XXXXXX";
    int ret, fd;

    if (!have_qemu_img()) {
        g_test_message("QTEST_QEMU_IMG not set or qemu-img missing");
        return 0;
    }

    fd = mkstemp(timg);
    g_assert(fd >= 0);
    mkqcow2(timg, 11);
    close(fd);

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/snapshot/save", save_snapshot);
    qtest_add_func("/snapshot/load", load_snapshot);
    qtest_add_func("/snapshot/delete", delete_snapshot);

    global_qtest = qtest_start(timg);
    ret = g_test_run();

    qtest_end();

    unlink(timg);

    return ret;
}

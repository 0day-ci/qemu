/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qjson.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"

static QDict *qmp_dispatch_check_obj(const QObject *request, Error **errp)
{
    const QDictEntry *ent;
    const char *arg_name;
    const QObject *arg_obj;
    bool has_exec_key = false;
    QDict *dict = NULL;

    if (qobject_type(request) != QTYPE_QDICT) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT,
                   "request is not a dictionary");
        return NULL;
    }

    dict = qobject_to_qdict(request);

    for (ent = qdict_first(dict); ent;
         ent = qdict_next(dict, ent)) {
        arg_name = qdict_entry_key(ent);
        arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT_MEMBER, "execute",
                           "string");
                return NULL;
            }
            has_exec_key = true;
        } else if (strcmp(arg_name, "arguments")) {
            error_setg(errp, QERR_QMP_EXTRA_MEMBER, arg_name);
            return NULL;
        }
    }

    if (!has_exec_key) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT, "execute");
        return NULL;
    }

    return dict;
}

static QObject *do_qmp_dispatch(QObject *request, QmpReturn *qret, Error **errp)
{
    Error *local_err = NULL;
    const char *command;
    QDict *args, *dict;
    QmpCommand *cmd;
    QObject *ret = NULL;

    dict = qmp_dispatch_check_obj(request, errp);
    if (!dict) {
        return NULL;
    }

    command = qdict_get_str(dict, "execute");
    cmd = qmp_find_command(command);
    if (cmd == NULL) {
        error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "The command %s has not been found", command);
        return NULL;
    }
    if (!cmd->enabled) {
        error_setg(errp, "The command %s has been disabled for this instance",
                   command);
        return NULL;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
        QINCREF(args);
    }

    cmd->fn(args, &ret, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    } else if (cmd->options & QCO_NO_SUCCESS_RESP) {
        g_assert(!ret);
    } else if (!ret) {
        ret = QOBJECT(qdict_new());
    }

    QDECREF(args);

    return ret;
}

QObject *qmp_build_error_object(Error *err)
{
    return qobject_from_jsonf("{ 'class': %s, 'desc': %s }",
                              QapiErrorClass_lookup[error_get_class(err)],
                              error_get_pretty(err));
}

static void qmp_return_free(QmpReturn *qret)
{
    QDict *rsp = qret->rsp;

    qobject_decref(QOBJECT(rsp));
    g_free(qret);
}

static void do_qmp_return(QmpReturn *qret)
{
    QDict *rsp = qret->rsp;

    qret->return_cb(QOBJECT(rsp), qret->opaque);

    qmp_return_free(qret);
}

void qmp_return(QmpReturn *qret, QObject *cmd_rsp)
{
    qdict_put_obj(qret->rsp, "return", cmd_rsp ?: QOBJECT(qdict_new()));

    do_qmp_return(qret);
}

void qmp_return_error(QmpReturn *qret, Error *err)
{
    qdict_put_obj(qret->rsp, "error", qmp_build_error_object(err));
    error_free(err);

    do_qmp_return(qret);
}

void qmp_dispatch(QObject *request, QDict *rsp,
                  QmpDispatchReturn *return_cb, void *opaque)
{
    Error *err = NULL;
    QmpReturn *qret = g_new0(QmpReturn, 1);
    QObject *ret;

    assert(return_cb);

    qret->rsp = rsp ?: qdict_new();
    qret->return_cb = return_cb;
    qret->opaque = opaque;

    ret = do_qmp_dispatch(request, qret, &err);

    if (err) {
        assert(!ret);
        qmp_return_error(qret, err);
    } else if (ret) {
        qmp_return(qret, ret);
    }
}

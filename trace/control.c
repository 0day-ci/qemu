/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "trace/control.h"
#include "qemu/help_option.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#ifdef CONFIG_TRACE_FTRACE
#include "trace/ftrace.h"
#endif
#ifdef CONFIG_TRACE_LOG
#include "qemu/log.h"
#endif
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "monitor/monitor.h"

int trace_events_enabled_count;

typedef struct TraceEventGroup {
    TraceEvent *events;
    size_t nevents;
    /*
     * Interpretation depends on wether the event has the 'vcpu' property:
     * - false: Boolean value indicating whether the event is active.
     * - true : Integral counting the number of vCPUs that have this event
     *          enabled.
     */
    uint16_t *dstate;
    /* Marks events for late vCPU state init */
    bool *dstate_init;
} TraceEventGroup;

static bool have_vcpu_events;
static TraceEventGroup *event_groups;
static size_t nevent_groups;

static bool pattern_glob(const char *pat, const char *ev);

QemuOptsList qemu_trace_opts = {
    .name = "trace",
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_trace_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "events",
            .type = QEMU_OPT_STRING,
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};


void trace_event_register_group(TraceEvent *events,
                                size_t nevents,
                                uint16_t *dstate,
                                bool *dstate_init)
{
    size_t nvcpuevents = 0;
    for (size_t i = 0; i < nevents; i++) {
        if (events[i].vcpu_id != TRACE_VCPU_EVENT_COUNT) {
            nvcpuevents++;
        }
    }

    if (nvcpuevents) {
        /* We only support 1 group having vcpu events */
        assert(!have_vcpu_events);
        assert(nvcpuevents < TRACE_MAX_VCPU_EVENT);
        have_vcpu_events = true;
    }

    event_groups = g_renew(TraceEventGroup, event_groups, nevent_groups + 1);
    event_groups[nevent_groups].events = events;
    event_groups[nevent_groups].nevents = nevents;
    event_groups[nevent_groups].dstate = dstate;
    event_groups[nevent_groups].dstate_init = dstate_init;
    nevent_groups++;
}


TraceEvent *trace_event_name(const char *name)
{
    assert(name != NULL);

    TraceEventIter iter;
    TraceEvent *ev;
    trace_event_iter_init(&iter, NULL);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        if (strcmp(trace_event_get_name(ev), name) == 0) {
            return ev;
        }
    }
    return NULL;
}

static bool pattern_glob(const char *pat, const char *ev)
{
    while (*pat != '\0' && *ev != '\0') {
        if (*pat == *ev) {
            pat++;
            ev++;
        }
        else if (*pat == '*') {
            if (pattern_glob(pat, ev+1)) {
                return true;
            } else if (pattern_glob(pat+1, ev)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    while (*pat == '*') {
        pat++;
    }

    if (*pat == '\0' && *ev == '\0') {
        return true;
    } else {
        return false;
    }
}


void trace_event_iter_init(TraceEventIter *iter, const char *pattern)
{
    iter->event = 0;
    iter->group = 0;
    iter->pattern = pattern;
}

TraceEvent *trace_event_iter_next(TraceEventIter *iter)
{
    return trace_event_iter_next_full(iter, NULL, NULL);
}


TraceEvent *trace_event_iter_next_full(TraceEventIter *iter,
                                       uint16_t **dstate,
                                       bool **dstate_init)
{
    TraceEvent *ev;

    if (iter->group >= nevent_groups ||
        iter->event >= event_groups[iter->group].nevents) {
        return NULL;
    }

    ev = &(event_groups[iter->group].events[iter->event]);
    if (dstate) {
        *dstate = event_groups[iter->group].dstate;
    }
    if (dstate_init) {
        *dstate_init = event_groups[iter->group].dstate_init;
    }

    do {
        iter->event++;
        if (iter->event >= event_groups[iter->group].nevents) {
            iter->event = 0;
            iter->group++;
        }
    } while (iter->group < nevent_groups &&
             iter->event < event_groups[iter->group].nevents &&
             iter->pattern &&
             !pattern_glob(
                 iter->pattern,
                 trace_event_get_name(
                     &(event_groups[iter->group].events[iter->event]))));

    return ev;
}

void trace_list_events(void)
{
    TraceEventIter iter;
    TraceEvent *ev;
    trace_event_iter_init(&iter, NULL);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        fprintf(stderr, "%s\n", trace_event_get_name(ev));
    }
}

static void do_trace_enable_events(const char *line_buf)
{
    const bool enable = ('-' != line_buf[0]);
    const char *line_ptr = enable ? line_buf : line_buf + 1;
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(line_ptr);
    uint16_t *dstate;
    bool *dstate_init;

    trace_event_iter_init(&iter, is_pattern ? line_ptr : NULL);
    while ((ev = trace_event_iter_next_full(&iter, &dstate, &dstate_init)) !=
           NULL) {
        bool match = false;
        if (is_pattern) {
            if (trace_event_get_state_static(ev)) {
                match = true;
            }
        } else {
            if (g_str_equal(trace_event_get_name(ev),
                            line_ptr)) {
                if (!trace_event_get_state_static(ev)) {
                    error_report("WARNING: trace event '%s' is not traceable",
                                 line_ptr);
                    return;
                }
                match = true;
            }
        }
        if (match) {
            /* start tracing */
            trace_event_set_state_dynamic(dstate, ev, enable);
            /* mark for late vCPU init */
            dstate_init[ev->id] = true;
            if (!is_pattern) {
                return;
            }
        }
    }

    if (!is_pattern) {
        error_report("WARNING: trace event '%s' does not exist",
                     line_ptr);
    }
}

void trace_enable_events(const char *line_buf)
{
    if (is_help_option(line_buf)) {
        trace_list_events();
        if (cur_mon == NULL) {
            exit(0);
        }
    } else {
        do_trace_enable_events(line_buf);
    }
}

static void trace_init_events(const char *fname)
{
    Location loc;
    FILE *fp;
    char line_buf[1024];
    size_t line_idx = 0;

    if (fname == NULL) {
        return;
    }

    loc_push_none(&loc);
    loc_set_file(fname, 0);
    fp = fopen(fname, "r");
    if (!fp) {
        error_report("%s", strerror(errno));
        exit(1);
    }
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        loc_set_file(fname, ++line_idx);
        size_t len = strlen(line_buf);
        if (len > 1) {              /* skip empty lines */
            line_buf[len - 1] = '\0';
            if ('#' == line_buf[0]) { /* skip commented lines */
                continue;
            }
            trace_enable_events(line_buf);
        }
    }
    if (fclose(fp) != 0) {
        loc_set_file(fname, 0);
        error_report("%s", strerror(errno));
        exit(1);
    }
    loc_pop(&loc);
}

void trace_init_file(const char *file)
{
#ifdef CONFIG_TRACE_SIMPLE
    st_set_trace_file(file);
#elif defined CONFIG_TRACE_LOG
    /* If both the simple and the log backends are enabled, "-trace file"
     * only applies to the simple backend; use "-D" for the log backend.
     */
    if (file) {
        qemu_set_log_filename(file, &error_fatal);
    }
#else
    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backends\n");
        exit(1);
    }
#endif
}

bool trace_init_backends(void)
{
#ifdef CONFIG_TRACE_SIMPLE
    if (!st_init()) {
        fprintf(stderr, "failed to initialize simple tracing backend.\n");
        return false;
    }
#endif

#ifdef CONFIG_TRACE_FTRACE
    if (!ftrace_init()) {
        fprintf(stderr, "failed to initialize ftrace backend.\n");
        return false;
    }
#endif

    return true;
}

char *trace_opt_parse(const char *optarg)
{
    char *trace_file;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("trace"),
                                             optarg, true);
    if (!opts) {
        exit(1);
    }
    if (qemu_opt_get(opts, "enable")) {
        trace_enable_events(qemu_opt_get(opts, "enable"));
    }
    trace_init_events(qemu_opt_get(opts, "events"));
    trace_file = g_strdup(qemu_opt_get(opts, "file"));
    qemu_opts_del(opts);

    return trace_file;
}

void trace_init_vcpu_events(void)
{
    TraceEventIter iter;
    TraceEvent *ev;
    uint16_t *dstate;
    bool *dstate_init;
    trace_event_iter_init(&iter, NULL);
    while ((ev = trace_event_iter_next_full(&iter, &dstate, &dstate_init)) !=
           NULL) {
        if (trace_event_is_vcpu(ev) &&
            trace_event_get_state_static(ev) &&
            dstate_init[ev->id]) {
            trace_event_set_state_dynamic(dstate, ev, true);
        }
    }
}

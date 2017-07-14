/* Support for legacy -cpu cpu,features CLI option with +-feat syntax,
 * used by x86/sparc targets
 *
 * Author: Andreas Färber <afaerber@suse.de>
 * Author: Andre Przywara <andre.przywara@amd.com>
 * Author: Eduardo Habkost <ehabkost@redhat.com>
 * Author: Igor Mammedov <imammedo@redhat.com>
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 * Author: Markus Armbruster <armbru@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qom/cpu.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"

/* DO NOT USE WITH NEW CODE */
int cpu_legacy_apply_features(Object *obj, GList *features, bool enable,
                              Error **errp)
{
    GList *l;
    Error *local_err = NULL;

    for (l = features; l; l = l->next) {
        const char *prop = l->data;
        object_property_set_bool(obj, enable, prop, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return 1;
        }
    }
    return 0;
}

static inline void feat2prop(char *s)
{
    while ((s = strchr(s, '_'))) {
        *s = '-';
    }
}

static gint compare_string(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(a, b);
}

/* DO NOT USE WITH NEW CODE
 * Parse "+feature,-feature,feature=foo" CPU feature string
 */
void cpu_legacy_parse_featurestr(const char *typename, char *features,
                                 GList **plus_features, GList **minus_features,
                                 Error **errp)
{
    char *featurestr; /* Single 'key=value" string being parsed */
    static bool cpu_globals_initialized;
    bool ambiguous = false;

    if (cpu_globals_initialized) {
        return;
    }
    cpu_globals_initialized = true;

    if (!features) {
        return;
    }

    for (featurestr = strtok(features, ",");
         featurestr;
         featurestr = strtok(NULL, ",")) {
        const char *name;
        const char *val = NULL;
        char *eq = NULL;
        char num[32];
        GlobalProperty *prop;

        /* Compatibility syntax: */
        if (featurestr[0] == '+') {
            *plus_features = g_list_append(*plus_features,
                                           g_strdup(featurestr + 1));
            continue;
        } else if (featurestr[0] == '-') {
            *minus_features = g_list_append(*minus_features,
                                            g_strdup(featurestr + 1));
            continue;
        }

        eq = strchr(featurestr, '=');
        if (eq) {
            *eq++ = 0;
            val = eq;
        } else {
            val = "on";
        }

        feat2prop(featurestr);
        name = featurestr;

        if (g_list_find_custom(*plus_features, name, compare_string)) {
            error_report("warning: Ambiguous CPU model string. "
                         "Don't mix both \"+%s\" and \"%s=%s\"",
                         name, name, val);
            ambiguous = true;
        }
        if (g_list_find_custom(*minus_features, name, compare_string)) {
            error_report("warning: Ambiguous CPU model string. "
                         "Don't mix both \"-%s\" and \"%s=%s\"",
                         name, name, val);
            ambiguous = true;
        }

        /* Special case: */
        if (!strcmp(name, "tsc-freq")) {
            int ret;
            uint64_t tsc_freq;

            ret = qemu_strtosz_metric(val, NULL, &tsc_freq);
            if (ret < 0 || tsc_freq > INT64_MAX) {
                error_setg(errp, "bad numerical value %s", val);
                return;
            }
            snprintf(num, sizeof(num), "%" PRId64, tsc_freq);
            val = num;
            name = "tsc-frequency";
        }

        prop = g_new0(typeof(*prop), 1);
        prop->driver = typename;
        prop->property = g_strdup(name);
        prop->value = g_strdup(val);
        prop->errp = &error_fatal;
        qdev_prop_register_global(prop);
    }

    if (ambiguous) {
        error_report("warning: Compatibility of ambiguous CPU model "
                     "strings won't be kept on future QEMU versions");
    }
}

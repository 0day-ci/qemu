#include "qemu/osdep.h"
#include "hw/misc/measurements.h"
#include "qmp-commands.h"

MeasurementList *qmp_query_measurements(Error **errp)
{
    return NULL;
}

void measurements_extend_data(int pcrnum, uint8_t *data, size_t len, char *description)
{
    return;
}

void measurements_set_log(gchar *log)
{
    return;
}

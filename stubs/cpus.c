#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qom/cpu.h"
#include "trace.h"

void cpu_resume(CPUState *cpu)
{
}

void qemu_init_vcpu(CPUState *cpu)
{
    trace_guest_cpu_init(cpu);
}

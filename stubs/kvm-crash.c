#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/kvm.h"

void kvm_arch_log_crash(CPUState *cs)
{
    return;
}

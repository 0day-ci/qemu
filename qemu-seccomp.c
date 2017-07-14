/*
 * QEMU seccomp mode 2 support with libseccomp
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Eduardo Otubo    <eotubo@br.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "qemu/osdep.h"
#include <seccomp.h>
#include "sysemu/seccomp.h"

/* For some architectures (notably ARM) cacheflush is not supported until
 * libseccomp 2.2.3, but configure enforces that we are using a more recent
 * version on those hosts, so it is OK for this check to be less strict.
 */
#if SCMP_VER_MAJOR >= 3
  #define HAVE_CACHEFLUSH
#elif SCMP_VER_MAJOR == 2 && SCMP_VER_MINOR >= 2
  #define HAVE_CACHEFLUSH
#endif

struct QemuSeccompSyscall {
    int32_t num;
    uint8_t priority;
};

static const struct QemuSeccompSyscall blacklist[] = {
    { SCMP_SYS(reboot), 255 },
    { SCMP_SYS(swapon), 255 },
    { SCMP_SYS(swapoff), 255 },
    { SCMP_SYS(syslog), 255 },
    { SCMP_SYS(mount), 255 },
    { SCMP_SYS(umount), 255 },
    { SCMP_SYS(kexec_load), 255 },
    { SCMP_SYS(afs_syscall), 255 },
    { SCMP_SYS(break), 255 },
    { SCMP_SYS(ftime), 255 },
    { SCMP_SYS(getpmsg), 255 },
    { SCMP_SYS(gtty), 255 },
    { SCMP_SYS(lock), 255 },
    { SCMP_SYS(mpx), 255 },
    { SCMP_SYS(prof), 255 },
    { SCMP_SYS(profil), 255 },
    { SCMP_SYS(putpmsg), 255 },
    { SCMP_SYS(security), 255 },
    { SCMP_SYS(stty), 255 },
    { SCMP_SYS(tuxcall), 255 },
    { SCMP_SYS(ulimit), 255 },
    { SCMP_SYS(vserver), 255 },
};

int seccomp_start(void)
{
    int rc = 0;
    unsigned int i = 0;
    scmp_filter_ctx ctx;

    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        rc = -1;
        goto seccomp_return;
    }

    for (i = 0; i < ARRAY_SIZE(blacklist); i++) {
        rc = seccomp_rule_add(ctx, SCMP_ACT_KILL, blacklist[i].num, 0);
        if (rc < 0) {
            goto seccomp_return;
        }
        rc = seccomp_syscall_priority(ctx, blacklist[i].num,
                                      blacklist[i].priority);
        if (rc < 0) {
            goto seccomp_return;
        }
    }

    rc = seccomp_load(ctx);

  seccomp_return:
    seccomp_release(ctx);
    return rc;
}

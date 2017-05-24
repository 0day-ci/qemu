/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_H
#define QEMU_MIGRATION_H

#include "qapi/qmp/qdict.h"
#include "qemu-common.h"
#include "qemu/thread.h"
#include "qemu/notify.h"
#include "migration/vmstate.h"
#include "qapi-types.h"
#include "exec/cpu-common.h"
#include "qemu/coroutine_int.h"

#define QEMU_VM_FILE_MAGIC           0x5145564d
#define QEMU_VM_FILE_VERSION_COMPAT  0x00000002
#define QEMU_VM_FILE_VERSION         0x00000003

#define QEMU_VM_EOF                  0x00
#define QEMU_VM_SECTION_START        0x01
#define QEMU_VM_SECTION_PART         0x02
#define QEMU_VM_SECTION_END          0x03
#define QEMU_VM_SECTION_FULL         0x04
#define QEMU_VM_SUBSECTION           0x05
#define QEMU_VM_VMDESCRIPTION        0x06
#define QEMU_VM_CONFIGURATION        0x07
#define QEMU_VM_COMMAND              0x08
#define QEMU_VM_SECTION_FOOTER       0x7e

struct MigrationParams {
    bool blk;
    bool shared;
};

/* Messages sent on the return path from destination to source */
enum mig_rp_message_type {
    MIG_RP_MSG_INVALID = 0,  /* Must be 0 */
    MIG_RP_MSG_SHUT,         /* sibling will not send any more RP messages */
    MIG_RP_MSG_PONG,         /* Response to a PING; data (seq: be32 ) */

    MIG_RP_MSG_REQ_PAGES_ID, /* data (start: be64, len: be32, id: string) */
    MIG_RP_MSG_REQ_PAGES,    /* data (start: be64, len: be32) */

    MIG_RP_MSG_MAX
};

/* State for the incoming migration */
struct MigrationIncomingState {
    QEMUFile *from_src_file;

    /*
     * Free at the start of the main state load, set as the main thread finishes
     * loading state.
     */
    QemuEvent main_thread_load_event;

    size_t         largest_page_size;
    bool           have_fault_thread;
    QemuThread     fault_thread;
    QemuSemaphore  fault_thread_sem;

    bool           have_listen_thread;
    QemuThread     listen_thread;
    QemuSemaphore  listen_thread_sem;

    /* For the kernel to send us notifications */
    int       userfault_fd;
    /* To tell the fault_thread to quit */
    int       userfault_quit_fd;
    QEMUFile *to_src_file;
    QemuMutex rp_mutex;    /* We send replies from multiple threads */
    void     *postcopy_tmp_page;
    void     *postcopy_tmp_zero_page;

    QEMUBH *bh;

    int state;

    bool have_colo_incoming_thread;
    QemuThread colo_incoming_thread;
    /* The coroutine we should enter (back) after failover */
    Coroutine *migration_incoming_co;
    QemuSemaphore colo_incoming_sem;
};

MigrationIncomingState *migration_incoming_get_current(void);
void migration_incoming_state_destroy(void);

struct MigrationState
{
    size_t bytes_xfer;
    size_t xfer_limit;
    QemuThread thread;
    QEMUBH *cleanup_bh;
    QEMUFile *to_dst_file;

    /* New style params from 'migrate-set-parameters' */
    MigrationParameters parameters;

    int state;
    /* Old style params from 'migrate' command */
    MigrationParams params;

    /* State related to return path */
    struct {
        QEMUFile     *from_dst_file;
        QemuThread    rp_thread;
        bool          error;
    } rp_state;

    double mbps;
    int64_t total_time;
    int64_t downtime;
    int64_t expected_downtime;
    bool enabled_capabilities[MIGRATION_CAPABILITY__MAX];
    int64_t xbzrle_cache_size;
    int64_t setup_time;

    /* Flag set once the migration has been asked to enter postcopy */
    bool start_postcopy;
    /* Flag set after postcopy has sent the device state */
    bool postcopy_after_devices;

    /* Flag set once the migration thread is running (and needs joining) */
    bool migration_thread_running;

    /* Flag set once the migration thread called bdrv_inactivate_all */
    bool block_inactive;

    /* The semaphore is used to notify COLO thread that failover is finished */
    QemuSemaphore colo_exit_sem;

    /* The semaphore is used to notify COLO thread to do checkpoint */
    QemuSemaphore colo_checkpoint_sem;
    int64_t colo_checkpoint_time;
    QEMUTimer *colo_delay_timer;

    /* The last error that occurred */
    Error *error;
};

void migrate_set_state(int *state, int old_state, int new_state);

void migration_fd_process_incoming(QEMUFile *f);

void qemu_start_incoming_migration(const char *uri, Error **errp);

void migration_channel_process_incoming(MigrationState *s,
                                        QIOChannel *ioc);

void migration_tls_channel_process_incoming(MigrationState *s,
                                            QIOChannel *ioc,
                                            Error **errp);

void migration_channel_connect(MigrationState *s,
                               QIOChannel *ioc,
                               const char *hostname);

void migration_tls_channel_connect(MigrationState *s,
                                   QIOChannel *ioc,
                                   const char *hostname,
                                   Error **errp);

uint64_t migrate_max_downtime(void);

void exec_start_incoming_migration(const char *host_port, Error **errp);

void exec_start_outgoing_migration(MigrationState *s, const char *host_port, Error **errp);

void tcp_start_incoming_migration(const char *host_port, Error **errp);

void tcp_start_outgoing_migration(MigrationState *s, const char *host_port, Error **errp);

void unix_start_incoming_migration(const char *path, Error **errp);

void unix_start_outgoing_migration(MigrationState *s, const char *path, Error **errp);

void fd_start_incoming_migration(const char *path, Error **errp);

void fd_start_outgoing_migration(MigrationState *s, const char *fdname, Error **errp);

void rdma_start_outgoing_migration(void *opaque, const char *host_port, Error **errp);

void rdma_start_incoming_migration(const char *host_port, Error **errp);

void migrate_fd_error(MigrationState *s, const Error *error);

void migrate_fd_connect(MigrationState *s);

void add_migration_state_change_notifier(Notifier *notify);
void remove_migration_state_change_notifier(Notifier *notify);
MigrationState *migrate_init(const MigrationParams *params);
bool migration_is_blocked(Error **errp);
bool migration_in_setup(MigrationState *);
bool migration_is_idle(void);
bool migration_has_finished(MigrationState *);
bool migration_has_failed(MigrationState *);
/* True if outgoing migration has entered postcopy phase */
bool migration_in_postcopy(void);
/* ...and after the device transmission */
bool migration_in_postcopy_after_devices(MigrationState *);
MigrationState *migrate_get_current(void);

void migrate_compress_threads_create(void);
void migrate_compress_threads_join(void);
void migrate_decompress_threads_create(void);
void migrate_decompress_threads_join(void);
uint64_t ram_bytes_remaining(void);
uint64_t ram_bytes_transferred(void);
uint64_t ram_bytes_total(void);
uint64_t ram_dirty_sync_count(void);
uint64_t ram_dirty_pages_rate(void);
uint64_t ram_postcopy_requests(void);
void free_xbzrle_decoded_buf(void);

void acct_update_position(QEMUFile *f, size_t size, bool zero);

uint64_t dup_mig_pages_transferred(void);
uint64_t norm_mig_pages_transferred(void);
uint64_t xbzrle_mig_bytes_transferred(void);
uint64_t xbzrle_mig_pages_transferred(void);
uint64_t xbzrle_mig_pages_overflow(void);
uint64_t xbzrle_mig_pages_cache_miss(void);
double xbzrle_mig_cache_miss_rate(void);

void ram_handle_compressed(void *host, uint8_t ch, uint64_t size);
void ram_debug_dump_bitmap(unsigned long *todump, bool expected,
                           unsigned long pages);
/* For outgoing discard bitmap */
int ram_postcopy_send_discard_bitmap(MigrationState *ms);
/* For incoming postcopy discard */
int ram_discard_range(const char *block_name, uint64_t start, size_t length);
int ram_postcopy_incoming_init(MigrationIncomingState *mis);
void ram_postcopy_migrated_memory_release(MigrationState *ms);

bool migrate_release_ram(void);
bool migrate_postcopy_ram(void);
bool migrate_zero_blocks(void);

bool migrate_auto_converge(void);

int xbzrle_encode_buffer(uint8_t *old_buf, uint8_t *new_buf, int slen,
                         uint8_t *dst, int dlen);
int xbzrle_decode_buffer(uint8_t *src, int slen, uint8_t *dst, int dlen);

int migrate_use_xbzrle(void);
int64_t migrate_xbzrle_cache_size(void);
bool migrate_colo_enabled(void);

int64_t xbzrle_cache_resize(int64_t new_size);

bool migrate_use_compression(void);
int migrate_compress_level(void);
int migrate_compress_threads(void);
int migrate_decompress_threads(void);
bool migrate_use_events(void);

/* Sending on the return path - generic and then for each message type */
void migrate_send_rp_message(MigrationIncomingState *mis,
                             enum mig_rp_message_type message_type,
                             uint16_t len, void *data);
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_req_pages(MigrationIncomingState *mis, const char* rbname,
                              ram_addr_t start, size_t len);

void ram_control_before_iterate(QEMUFile *f, uint64_t flags);
void ram_control_after_iterate(QEMUFile *f, uint64_t flags);
void ram_control_load_hook(QEMUFile *f, uint64_t flags, void *data);

/* Whenever this is found in the data stream, the flags
 * will be passed to ram_control_load_hook in the incoming-migration
 * side. This lets before_ram_iterate/after_ram_iterate add
 * transport-specific sections to the RAM migration data.
 */
#define RAM_SAVE_FLAG_HOOK     0x80

#define RAM_SAVE_CONTROL_NOT_SUPP -1000
#define RAM_SAVE_CONTROL_DELAYED  -2000

size_t ram_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                             ram_addr_t offset, size_t size,
                             uint64_t *bytes_sent);

void ram_mig_init(void);
void savevm_skip_section_footers(void);
void register_global_state(void);
void global_state_set_optional(void);
void savevm_skip_configuration(void);
int global_state_store(void);
void global_state_store_running(void);

void migration_page_queue_free(void);
int ram_save_queue_pages(const char *rbname, ram_addr_t start, ram_addr_t len);
uint64_t ram_pagesize_summary(void);
#endif

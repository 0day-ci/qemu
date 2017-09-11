/*
 * QTest
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *  Andreas Färber    <afaerber@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef LIBQTEST_H
#define LIBQTEST_H

#include "qapi/qmp/qdict.h"

typedef struct QTestState QTestState;

extern QTestState *global_qtest;

/**
 * qtest_start_without_qmp_handshake:
 * @extra_args: other arguments to pass to QEMU.
 *
 * Returns: #QTestState instance, handshaking not yet completed.
 */
QTestState *qtest_start_without_qmp_handshake(const char *extra_args);

/**
 * qtest_start:
 * @args: other arguments to pass to QEMU
 *
 * Returns: #QTestState instance, handshaking completed.
 */
QTestState *qtest_start(const char *args);

/**
 * qtest_startf:
 * @fmt...: Format for creating other arguments to pass to QEMU, formatted
 * like sprintf().
 *
 * Returns: #QTestState instance, handshaking completed.
 */
QTestState *qtest_startf(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

/**
 * qtest_vstartf:
 * @fmt: Format for creating other arguments to pass to QEMU, formatted
 * like vsprintf().
 * @ap: Format arguments.
 *
 * Returns: #QTestState instance, handshaking completed.
 */
QTestState *qtest_vstartf(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);

/**
 * qtest_quit:
 * @s: #QTestState instance to operate on.
 *
 * Shut down the QEMU process associated to @s.
 */
void qtest_quit(QTestState *s);

/**
 * qtest_qmp_discard_response:
 * @s: #QTestState instance to operate on.
 *
 * Read and discard a QMP response, typically after qtest_async_qmp().
 */
void qtest_qmp_discard_response(QTestState *s);

/**
 * qtest_qmp:
 * @s: #QTestState instance to operate on.
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qtest_qmp(QTestState *s, const char *fmt, ...);

/**
 * qtest_async_qmp:
 * @s: #QTestState instance to operate on.
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_async_qmp(QTestState *s, const char *fmt, ...);

/**
 * qtest_qmpv:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to QEMU
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qtest_qmpv(QTestState *s, const char *fmt, va_list ap);

/**
 * qtest_async_qmpv:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to QEMU
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_async_qmpv(QTestState *s, const char *fmt, va_list ap);

/**
 * qtest_receive:
 * @s: #QTestState instance to operate on.
 *
 * Reads a QMP message from QEMU and returns the response.
 */
QDict *qtest_qmp_receive(QTestState *s);

/**
 * qtest_qmp_eventwait:
 * @s: #QTestState instance to operate on.
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 */
void qtest_qmp_eventwait(QTestState *s, const char *event);

/**
 * qtest_qmp_eventwait_ref:
 * @s: #QTestState instance to operate on.
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 * Returns a copy of the event for further investigation.
 */
QDict *qtest_qmp_eventwait_ref(QTestState *s, const char *event);

/**
 * qtest_hmp:
 * @s: #QTestState instance to operate on.
 * @fmt...: HMP command to send to QEMU, formats arguments like sprintf().
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 * QMP events are discarded.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *qtest_hmp(QTestState *s, const char *fmt, ...) GCC_FMT_ATTR(2, 3);

/**
 * qtest_hmpv:
 * @s: #QTestState instance to operate on.
 * @fmt: HMP command to send to QEMU
 * @ap: HMP command arguments
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 * QMP events are discarded.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *qtest_hmpv(QTestState *s, const char *fmt, va_list ap);

/**
 * get_irq:
 * @s: #QTestState instance to operate on.
 * @num: Interrupt to observe.
 *
 * Returns: The level of the @num interrupt.
 */
bool get_irq(QTestState *s, int num);

/**
 * irq_intercept_in:
 * @s: #QTestState instance to operate on.
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-in pins of the device
 * whose path is specified by @string.
 */
void irq_intercept_in(QTestState *s, const char *string);

/**
 * irq_intercept_out:
 * @s: #QTestState instance to operate on.
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-out pins of the device
 * whose path is specified by @string.
 */
void irq_intercept_out(QTestState *s, const char *string);

/**
 * outb:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write an 8-bit value to an I/O port.
 */
void outb(QTestState *s, uint16_t addr, uint8_t value);

/**
 * outw:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 16-bit value to an I/O port.
 */
void outw(QTestState *s, uint16_t addr, uint16_t value);

/**
 * outl:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 32-bit value to an I/O port.
 */
void outl(QTestState *s, uint16_t addr, uint32_t value);

/**
 * inb:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to read from.
 *
 * Returns an 8-bit value from an I/O port.
 */
uint8_t inb(QTestState *s, uint16_t addr);

/**
 * inw:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to read from.
 *
 * Returns a 16-bit value from an I/O port.
 */
uint16_t inw(QTestState *s, uint16_t addr);

/**
 * inl:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to read from.
 *
 * Returns a 32-bit value from an I/O port.
 */
uint32_t inl(QTestState *s, uint16_t addr);

/**
 * writeb:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes an 8-bit value to memory.
 */
void writeb(QTestState *s, uint64_t addr, uint8_t value);

/**
 * writew:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 16-bit value to memory.
 */
void writew(QTestState *s, uint64_t addr, uint16_t value);

/**
 * writel:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 32-bit value to memory.
 */
void writel(QTestState *s, uint64_t addr, uint32_t value);

/**
 * writeq:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 64-bit value to memory.
 */
void writeq(QTestState *s, uint64_t addr, uint64_t value);

/**
 * readb:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads an 8-bit value from memory.
 *
 * Returns: Value read.
 */
uint8_t readb(QTestState *s, uint64_t addr);

/**
 * readw:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads a 16-bit value from memory.
 *
 * Returns: Value read.
 */
uint16_t readw(QTestState *s, uint64_t addr);

/**
 * readl:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads a 32-bit value from memory.
 *
 * Returns: Value read.
 */
uint32_t readl(QTestState *s, uint64_t addr);

/**
 * readq:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads a 64-bit value from memory.
 *
 * Returns: Value read.
 */
uint64_t readq(QTestState *s, uint64_t addr);

/**
 * memread:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer.
 */
void memread(QTestState *s, uint64_t addr, void *data, size_t size);

/**
 * qtest_rtas_call:
 * @s: #QTestState instance to operate on.
 * @name: name of the command to call.
 * @nargs: Number of args.
 * @args: Guest address to read args from.
 * @nret: Number of return value.
 * @ret: Guest address to write return values to.
 *
 * Call an RTAS function
 */
uint64_t qtest_rtas_call(QTestState *s, const char *name,
                         uint32_t nargs, uint64_t args,
                         uint32_t nret, uint64_t ret);

/**
 * bufread:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer and receive using a base64 encoding.
 */
void bufread(QTestState *s, uint64_t addr, void *data, size_t size);

/**
 * memwrite:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory.
 */
void memwrite(QTestState *s, uint64_t addr, const void *data, size_t size);

/**
 * bufwrite:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory and transmit using a base64 encoding.
 */
void bufwrite(QTestState *s, uint64_t addr, const void *data, size_t size);

/**
 * qmemset:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @patt: Byte pattern to fill the guest memory region with.
 * @size: Number of bytes to write.
 *
 * Write a pattern to guest memory.
 */
void qmemset(QTestState *s, uint64_t addr, uint8_t patt, size_t size);

/**
 * clock_step_next:
 * @s: #QTestState instance to operate on.
 *
 * Advance the QEMU_CLOCK_VIRTUAL to the next deadline.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t clock_step_next(QTestState *s);

/**
 * clock_step:
 * @s: QTestState instance to operate on.
 * @step: Number of nanoseconds to advance the clock by.
 *
 * Advance the QEMU_CLOCK_VIRTUAL by @step nanoseconds.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t clock_step(QTestState *s, int64_t step);

/**
 * clock_set:
 * @s: QTestState instance to operate on.
 * @val: Nanoseconds value to advance the clock to.
 *
 * Advance the QEMU_CLOCK_VIRTUAL to @val nanoseconds since the VM was launched.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t clock_set(QTestState *s, int64_t val);

/**
 * qtest_big_endian:
 * @s: QTestState instance to operate on.
 *
 * Returns: True if the architecture under test has a big endian configuration.
 */
bool qtest_big_endian(QTestState *s);

/**
 * qtest_get_arch:
 *
 * Returns: The architecture for the QEMU executable under test.
 */
const char *qtest_get_arch(void);

/**
 * qtest_add_func:
 * @str: Test case path.
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
void qtest_add_func(const char *str, void (*fn)(void));

/**
 * qtest_add_data_func:
 * @str: Test case path.
 * @data: Test case data
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name, data and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
void qtest_add_data_func(const char *str, const void *data,
                         void (*fn)(const void *));

/**
 * qtest_add_data_func_full:
 * @str: Test case path.
 * @data: Test case data
 * @fn: Test case function
 * @data_free_func: GDestroyNotify for data
 *
 * Add a GTester testcase with the given name, data and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 *
 * @data is passed to @data_free_func() on test completion.
 */
void qtest_add_data_func_full(const char *str, void *data,
                              void (*fn)(const void *),
                              GDestroyNotify data_free_func);

/**
 * qtest_add:
 * @testpath: Test case path
 * @Fixture: Fixture type
 * @tdata: Test case data
 * @fsetup: Test case setup function
 * @ftest: Test case function
 * @fteardown: Test case teardown function
 *
 * Add a GTester testcase with the given name, data and functions.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
#define qtest_add(testpath, Fixture, tdata, fsetup, ftest, fteardown) \
    do { \
        char *path = g_strdup_printf("/%s/%s", qtest_get_arch(), testpath); \
        g_test_add(path, Fixture, tdata, fsetup, ftest, fteardown); \
        g_free(path); \
    } while (0)

void qtest_add_abrt_handler(GHookFunc fn, const void *data);

/**
 * qmp:
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qmp(const char *fmt, ...);

/**
 * qmp_async:
 * @fmt...: QMP message to send to qemu
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qmp_async(const char *fmt, ...);

/**
 * qmp_discard_response:
 *
 * Read and discard a QMP response, typically after qmp_async().
 */
void qmp_discard_response(void);

/**
 * qmp_receive:
 *
 * Reads a QMP message from QEMU and returns the response.
 */
static inline QDict *qmp_receive(void)
{
    return qtest_qmp_receive(global_qtest);
}

/**
 * qmp_eventwait:
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 */
static inline void qmp_eventwait(const char *event)
{
    return qtest_qmp_eventwait(global_qtest, event);
}

/**
 * qmp_eventwait_ref:
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 * Returns a copy of the event for further investigation.
 */
static inline QDict *qmp_eventwait_ref(const char *event)
{
    return qtest_qmp_eventwait_ref(global_qtest, event);
}

/**
 * hmp:
 * @fmt...: HMP command to send to QEMU, formats arguments like sprintf().
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *hmp(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

QDict *qmp_fd_receive(int fd);
void qmp_fd_sendv(int fd, const char *fmt, va_list ap);
void qmp_fd_send(int fd, const char *fmt, ...);
QDict *qmp_fdv(int fd, const char *fmt, va_list ap);
QDict *qmp_fd(int fd, const char *fmt, ...);

/**
 * qtest_cb_for_every_machine:
 * @cb: Pointer to the callback function
 *
 *  Call a callback function for every name of all available machines.
 */
void qtest_cb_for_every_machine(void (*cb)(const char *machine));

#endif

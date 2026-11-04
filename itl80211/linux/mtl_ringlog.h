/*
 * mtl_ringlog.h — MTL bring-up aid (thebcd69/itlwm, branch mtl).
 *
 * On the GU605MY hackintosh kext IOLog lines never reach the unified log and the 128 KB msgbuf is
 * overwritten by NVRM output within minutes, so driver logs were lost (hardware tests 1 and 2).
 * Every XYLog line is therefore also kept in a 64 KB ring buffer, which itlwm publishes as the
 * OSData property "MTL-Log" on the PCI device (CNVW). The PCI nub outlives a failed itlwm start,
 * so the log can be read without sudo:
 *     ioreg -a -r -n CNVW@14,3 -k MTL-Log | plutil -extract 0.MTL-Log raw - | base64 -D
 */
#ifndef MTL_RINGLOG_H
#define MTL_RINGLOG_H

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSData.h>

extern "C" int vsnprintf(char *, size_t, const char *, __builtin_va_list);

#define MTL_RING_SIZE 65536u

struct mtl_ring_t {
    volatile SInt32 pos;     /* total bytes ever written (wraps at 2^31, see mtl_ring_publish) */
    char buf[MTL_RING_SIZE];
};

/* Zero-initialised POD with a trivial constructor: no static-init guard is emitted. */
static inline mtl_ring_t &mtl_ring()
{
    static mtl_ring_t r;
    return r;
}

static inline void mtl_ringlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void mtl_ringlog(const char *fmt, ...)
{
    char line[320];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    __builtin_va_end(ap);
    if (n <= 0)
        return;
    if (n >= (int)sizeof(line))
        n = (int)sizeof(line) - 1;
    mtl_ring_t &r = mtl_ring();
    UInt32 start = (UInt32)OSAddAtomic(n, &r.pos) & 0x7FFFFFFFu;
    for (int i = 0; i < n; i++)
        r.buf[(start + (UInt32)i) % MTL_RING_SIZE] = line[i];
    IOLog("%s", line);
}

/* Publish the ring (oldest first) as "MTL-Log" on `nub`. Safe to call repeatedly. */
static inline void mtl_ring_publish(IOService *nub)
{
    if (!nub)
        return;
    mtl_ring_t &r = mtl_ring();
    UInt32 total = (UInt32)r.pos & 0x7FFFFFFFu;
    UInt32 len = total < MTL_RING_SIZE ? total : MTL_RING_SIZE;
    if (len == 0)
        return;
    char *tmp = (char *)IOMalloc(len);
    if (!tmp)
        return;
    UInt32 first = total - len;
    for (UInt32 i = 0; i < len; i++)
        tmp[i] = r.buf[(first + i) % MTL_RING_SIZE];
    OSData *d = OSData::withBytes(tmp, len);
    IOFree(tmp, len);
    if (d) {
        nub->setProperty("MTL-Log", d);
        d->release();
    }
}

#endif /* MTL_RINGLOG_H */

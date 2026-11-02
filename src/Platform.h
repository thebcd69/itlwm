// Platform shim: lets the amp/DSP core build both as a kext (KERNEL) and
// as a host test binary (tests/host_test.cpp) without changes.
#pragma once

#ifdef KERNEL
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <kern/clock.h>
#include <stdint.h>
#include <stddef.h>
#define CS_LOG(fmt, ...) IOLog("CS35L56: " fmt "\n", ##__VA_ARGS__)
static inline void cs_sleep_ms(uint32_t ms) { IOSleep(ms); }
static inline void cs_delay_us(uint32_t us) { IODelay(us); }
static inline void *cs_alloc(size_t n) { return IOMallocZero((vm_size_t)n); }
static inline void cs_free(void *p, size_t n) { if (p) IOFree(p, (vm_size_t)n); }
static inline uint64_t cs_now_us() {
    uint64_t abs = mach_absolute_time(), ns = 0;
    absolutetime_to_nanoseconds(abs, &ns);
    return ns / 1000;
}
static inline uint32_t cs_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
#else
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CS_LOG(fmt, ...) printf("CS35L56: " fmt "\n", ##__VA_ARGS__)
void cs_sleep_ms(uint32_t ms);           // provided by the host test
void cs_delay_us(uint32_t us);
uint64_t cs_now_us();
static inline void *cs_alloc(size_t n) { return calloc(1, n); }
static inline void cs_free(void *p, size_t) { free(p); }
static inline uint32_t cs_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
#endif

static inline uint32_t cs_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t cs_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Result codes (negative = error), kept tiny on purpose.
enum : int {
    CS_OK = 0,
    CS_EIO = -5,
    CS_ENOMEM = -12,
    CS_EINVAL = -22,
    CS_ENODEV = -19,
    CS_ETIMEDOUT = -110,
    CS_EOVERFLOW = -75,
    CS_EACCES = -13,
};

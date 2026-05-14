/*
 * Cross-board compatibility shims for AP_HAL_Zephyr.
 * Force-included from boards.py before per-board hwdef.h, so board
 * headers can override with #ifndef guards.
 */
#pragma once

// pull toolchain <stdio.h> before Zephyr's posix <unistd.h> shadows SEEK_*
#include <stdio.h>

#include <strings.h>

// picolibc doesn't ship memmem()
#ifdef __PICOLIBC__
#include <string.h>
static inline void *memmem(const void *haystack, size_t hlen,
                            const void *needle,   size_t nlen)
{
    if (nlen == 0) { return (void *)haystack; }
    if (nlen > hlen) { return nullptr; }
    const char *h = static_cast<const char *>(haystack);
    const char *n = static_cast<const char *>(needle);
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) {
            return (void *)(h + i);
        }
    }
    return nullptr;
}
#endif

// force C linkage on arch_inlines.h before arch_interface.h declares it
// extern "C" (Zephyr include-order bug)
#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/arch/arch_inlines.h>
#ifdef __cplusplus
}
#endif

// Zephyr's struct dirent has no d_type — replace it with the POSIX layout
#ifndef ZEPHYR_INCLUDE_POSIX_SYS_DIRENT_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_DIRENT_H_
typedef void DIR;
struct dirent {
    unsigned int  d_ino;
    char          d_name[256];
    unsigned char d_type;
};
#ifndef DT_REG
#define DT_REG  0
#define DT_DIR  1
#define DT_LNK  10
#endif
#endif

// renamed entry point so Zephyr's weak main() loses to app/main.cpp's strong one
#ifndef AP_MAIN
#define AP_MAIN ardupilot_entry
#endif

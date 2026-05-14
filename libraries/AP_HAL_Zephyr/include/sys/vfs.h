// Linux statfs() shim — AP_Filesystem_posix.cpp wants it but neither newlib
// nor picolibc ship this header on Zephyr. Maps to fs_statvfs().
#pragma once

#include <zephyr/fs/fs.h>

struct statfs {
    unsigned long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
};

static inline int statfs(const char *path, struct statfs *buf)
{
    struct fs_statvfs st;
    int rc = fs_statvfs(path, &st);
    if (rc < 0) {
        return -1;
    }
    buf->f_bsize  = st.f_bsize;
    buf->f_blocks = st.f_blocks;
    buf->f_bfree  = st.f_bfree;
    buf->f_bavail = st.f_bfree;  // LittleFS doesn't distinguish bavail
    return 0;
}

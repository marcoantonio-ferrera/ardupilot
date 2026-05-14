#include "Storage.h"

#include <algorithm>
#include <cstring>
#include <errno.h>

#include <zephyr/sys/printk.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/devicetree/fixed-partitions.h>

#ifdef CONFIG_FAT_FILESYSTEM_ELM
#include <zephyr/fs/fs.h>

// ff.h isn't on waf's include path, so stand in with a raw aligned buffer
// big enough for FATFS state + sector window + LFN buffer.
static uint8_t           s_fat_fs_buf[2048] __attribute__((aligned(4)));
static struct fs_mount_t s_fat_mount = {
    .type      = FS_FATFS,
    .mnt_point = "/SD:",
    .fs_data   = s_fat_fs_buf,
};
static const char SD_FILE_PATH[] = "/SD:/ardupilot.bin";

// keep the file open across write_block() calls — closing/reopening re-reads
// and rewrites the FAT directory entry on every param update.
static struct fs_file_t  s_fat_file;
static bool              s_fat_file_open = false;
#endif

using namespace Zephyr;

#define AP_STORAGE_PARTITION storage_partition

Storage::Storage() :
    _storage{},
    _healthy(false),
    _zms_ok(false),
    _file_ok(false)
#ifdef CONFIG_ZMS
    , _zms{}
#endif
{
}

void Storage::init()
{
    _storage.fill(0);
    _zms_ok  = false;
    _healthy = false;

#ifdef CONFIG_ZMS
    // skip when the parent flash node is disabled in the overlay —
    // FIXED_PARTITION_DEVICE would otherwise pull in a dead symbol.
#if DT_NODE_HAS_STATUS(DT_MTD_FROM_FIXED_PARTITION(DT_NODELABEL(AP_STORAGE_PARTITION)), okay)
    const struct device *flash_dev = FIXED_PARTITION_DEVICE(AP_STORAGE_PARTITION);

    if (flash_dev == nullptr) {
        printk("AP_HAL_Zephyr Storage: flash device is NULL\n");
    } else if (!device_is_ready(flash_dev)) {
        printk("AP_HAL_Zephyr Storage: flash device '%s' not ready\n", flash_dev->name);
    } else {
        _zms.flash_device  = flash_dev;
        _zms.offset        = FIXED_PARTITION_OFFSET(AP_STORAGE_PARTITION);
        _zms.sector_size   = ZMS_SECTOR_SIZE;
        _zms.sector_count  = ZMS_SECTOR_COUNT;

        printk("AP_HAL_Zephyr Storage: mounting ZMS on '%s' offset=0x%lx "
               "sector_size=%u sector_count=%u\n",
               flash_dev->name, (unsigned long)_zms.offset,
               _zms.sector_size, _zms.sector_count);

        int64_t t0 = k_uptime_get();
        int rc = zms_mount(&_zms);
        int64_t mount_ms = k_uptime_get() - t0;

        if (rc < 0) {
            printk("AP_HAL_Zephyr Storage: zms_mount() failed (%d) after %lld ms\n",
                   rc, (long long)mount_ms);
        } else {
            printk("AP_HAL_Zephyr Storage: ZMS mounted OK in %lld ms\n", (long long)mount_ms);
            _zms_ok = true;

            int64_t tread = k_uptime_get();
            for (uint16_t i = 0; i < NUM_CHUNKS; i++) {
                uint8_t *chunk = _storage.data() + i * CHUNK_SIZE;
                ssize_t  n     = zms_read(&_zms, (zms_id_t)(i + 1), chunk, CHUNK_SIZE);
                (void)n;  // -ENOENT is fine on first boot
            }
            printk("AP_HAL_Zephyr Storage: read %u chunks in %lld ms\n",
                   NUM_CHUNKS, (long long)(k_uptime_get() - tread));
        }
    }
#else
    printk("AP_HAL_Zephyr Storage: flash device disabled in DTS\n");
#endif
#endif

#ifdef CONFIG_FAT_FILESYSTEM_ELM
    _try_file_mount();
#endif

    if (!_zms_ok && !_file_ok) {
        printk("AP_HAL_Zephyr Storage: no persistent storage -- RAM-only\n");
    }
    _healthy = true;
}

bool Storage::erase()
{
    _storage.fill(0);

#ifdef CONFIG_ZMS
    if (_zms_ok) {
        zms_clear(&_zms);
        _zms_ok = (zms_mount(&_zms) == 0);
    }
#endif

#ifdef CONFIG_FAT_FILESYSTEM_ELM
    if (_file_ok) {
        _write_file();
    }
#endif

    return true;
}

void Storage::read_block(void *dst, uint16_t src, size_t n)
{
    if (dst == nullptr || src >= (uint16_t)_storage.size()) {
        return;
    }
    const size_t count = std::min<size_t>(n, _storage.size() - src);
    memcpy(dst, _storage.data() + src, count);
}

void Storage::write_block(uint16_t dst, const void *src, size_t n)
{
    if (src == nullptr || dst >= (uint16_t)_storage.size()) {
        return;
    }

    const size_t count = std::min<size_t>(n, _storage.size() - dst);
    memcpy(_storage.data() + dst, src, count);

    const uint16_t first_chunk = dst / CHUNK_SIZE;
    const uint16_t last_chunk  = (uint16_t)((dst + count - 1) / CHUNK_SIZE);
    for (uint16_t i = first_chunk; i <= last_chunk; i++) {
#ifdef CONFIG_ZMS
        if (_zms_ok) {
            _write_chunk(i);
        }
#endif
#ifdef CONFIG_FAT_FILESYSTEM_ELM
        if (_file_ok) {
            _write_file_chunk(i);
        }
#endif
    }
}

bool Storage::healthy()
{
    return _healthy;
}

void Storage::_write_chunk(uint16_t chunk_idx)
{
#ifdef CONFIG_ZMS
    if (chunk_idx >= NUM_CHUNKS) {
        return;
    }
    const uint8_t *chunk = _storage.data() + chunk_idx * CHUNK_SIZE;
    // zms_write is a no-op when data hasn't changed — safe to call on every write_block.
    zms_write(&_zms, (zms_id_t)(chunk_idx + 1), chunk, CHUNK_SIZE);
#endif
}

void Storage::_try_file_mount()
{
#ifdef CONFIG_FAT_FILESYSTEM_ELM
    printk("### SD: fs_mount START ###\n");
    int rc = fs_mount(&s_fat_mount);
    printk("### SD: fs_mount rc=%d %s ###\n", rc, (rc == 0 || rc == -EEXIST) ? "OK" : "FAIL");
    if (rc != 0 && rc != -EEXIST) {
        return;
    }

    fs_file_t_init(&s_fat_file);

    rc = fs_open(&s_fat_file, SD_FILE_PATH, FS_O_RDWR);
    if (rc == 0) {
        ssize_t n = fs_read(&s_fat_file, _storage.data(), _storage.size());
        printk("AP_HAL_Zephyr Storage: loaded %d B from SD card\n", (int)n);
    } else {
        // first boot — create it with the zeroed buffer
        rc = fs_open(&s_fat_file, SD_FILE_PATH, FS_O_CREATE | FS_O_RDWR);
        if (rc != 0) {
            printk("AP_HAL_Zephyr Storage: SD file create failed (%d) -- RAM-only\n", rc);
            return;
        }
        fs_write(&s_fat_file, _storage.data(), _storage.size());
        fs_sync(&s_fat_file);
        printk("AP_HAL_Zephyr Storage: created %s (%u B)\n",
               SD_FILE_PATH, (unsigned)_storage.size());
    }

    s_fat_file_open = true;
    _file_ok        = true;
    printk("AP_HAL_Zephyr Storage: using SD card at %s\n", SD_FILE_PATH);
#endif
}

void Storage::_write_file()
{
#ifdef CONFIG_FAT_FILESYSTEM_ELM
    if (!s_fat_file_open) {
        return;
    }
    fs_seek(&s_fat_file, 0, FS_SEEK_SET);
    fs_write(&s_fat_file, _storage.data(), _storage.size());
    fs_sync(&s_fat_file);
#endif
}

void Storage::_write_file_chunk(uint16_t chunk_idx)
{
#ifdef CONFIG_FAT_FILESYSTEM_ELM
    if (!s_fat_file_open || chunk_idx >= NUM_CHUNKS) {
        return;
    }
    fs_seek(&s_fat_file, (off_t)chunk_idx * CHUNK_SIZE, FS_SEEK_SET);
    fs_write(&s_fat_file, _storage.data() + chunk_idx * CHUNK_SIZE, CHUNK_SIZE);
    fs_sync(&s_fat_file);
#endif
}

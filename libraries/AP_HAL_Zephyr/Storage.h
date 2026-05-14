#pragma once

#include <array>

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"

#ifdef CONFIG_ZMS
#include <zephyr/kvss/zms.h>
#endif

class Zephyr::Storage : public AP_HAL::Storage {
public:
    Storage();

    void init() override;
    bool erase() override;
    void read_block(void *dst, uint16_t src, size_t n) override;
    void write_block(uint16_t dst, const void *src, size_t n) override;
    bool healthy() override;
    bool is_flash_backed() const { return _zms_ok || _file_ok; }
    bool get_storage_ptr(void *&ptr, size_t &size) override {
        ptr  = _storage.data();
        size = _storage.size();
        return true;
    }

private:
    static constexpr uint16_t CHUNK_SIZE  = 256;
    static constexpr uint16_t NUM_CHUNKS  = HAL_STORAGE_SIZE / CHUNK_SIZE;

    // ZMS ID 0 is reserved, so entries use (chunk_index + 1).
#ifndef CONFIG_AP_HAL_STORAGE_SECTOR_SIZE
#define CONFIG_AP_HAL_STORAGE_SECTOR_SIZE 4096
#endif
#ifndef CONFIG_AP_HAL_STORAGE_SECTOR_COUNT
#define CONFIG_AP_HAL_STORAGE_SECTOR_COUNT 64
#endif
    static constexpr uint32_t ZMS_SECTOR_SIZE  = CONFIG_AP_HAL_STORAGE_SECTOR_SIZE;
    static constexpr uint32_t ZMS_SECTOR_COUNT = CONFIG_AP_HAL_STORAGE_SECTOR_COUNT;

    std::array<uint8_t, HAL_STORAGE_SIZE> _storage;

    bool           _healthy;
    bool           _zms_ok;
    bool           _file_ok;

#ifdef CONFIG_ZMS
    struct zms_fs  _zms;
#endif

    void _write_chunk(uint16_t chunk_idx);
    void _try_file_mount();
    void _write_file();
    void _write_file_chunk(uint16_t chunk_idx);
};

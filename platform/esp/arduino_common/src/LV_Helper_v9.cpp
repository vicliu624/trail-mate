/**
 * @file LV_Helper_v9.cpp
 * @brief LVGL v9.x helper functions for T-LoRa-Pager
 *
 * This file provides LVGL initialization and integration functions,
 * including display driver setup, input device registration, and
 * custom memory management for LVGL v9.x.
 */

#include "display/DisplayInterface.h"
#include "input/morse_engine.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "platform/ui/psram_ui_lease.h"
#include "platform/ui/screen_runtime.h"
#include "screen_sleep.h"
#include "sys/clock.h"
#include "ui/LV_Helper.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/support/lvgl_fs_utils.h"
#include "walkie/walkie_service.h"
#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <freertos/task.h>
#include <soc/soc_memory_types.h>
#include <sys/types.h>
#include <unistd.h>

#if LVGL_VERSION_MAJOR == 9

// Test toggles (set to 0 to disable)
#define LV_TEST_FORCE_DMA_BUF 0
#define LV_TEST_FORCE_DMA_FULL_SIZE 0
#define LV_TEST_FLUSH_LOG 0
#define LV_TEST_FLUSH_SAMPLE 0

static lv_display_t* disp_drv;
static lv_draw_buf_t draw_buf;
static lv_indev_t* indev_touch;
static lv_indev_t* indev_encoder;
static lv_indev_t* indev_keyboard;

static lv_color16_t* buf = nullptr;
static lv_color16_t* buf1 = nullptr;

namespace
{
constexpr char kLvglFlashFsLetter = 'F';
constexpr const char* kLvglFlashFsMountPoint = "/fs";
#if defined(ARDUINO_T_DECK)
#ifndef TRAIL_MATE_TDECK_DMA_DRAW_BUFFER_LINES
#define TRAIL_MATE_TDECK_DMA_DRAW_BUFFER_LINES 40
#endif
constexpr size_t kTDeckDmaDrawBufferLines = TRAIL_MATE_TDECK_DMA_DRAW_BUFFER_LINES;
static_assert(kTDeckDmaDrawBufferLines >= 10, "TDeck draw buffers below 10 lines make refresh visibly unstable");

#ifndef TRAIL_MATE_TDECK_USE_DMA_DRAW_BUFFERS
#define TRAIL_MATE_TDECK_USE_DMA_DRAW_BUFFERS 0
#endif
constexpr bool kTDeckUseDmaDrawBuffers = TRAIL_MATE_TDECK_USE_DMA_DRAW_BUFFERS != 0;
#endif
lv_fs_drv_t s_flash_fs_drv;
lv_fs_drv_t s_sd_fs_drv;
bool s_flash_fs_ready = false;
bool s_sd_fs_ready = false;
std::atomic<unsigned> s_external_font_load_fs_depth{0};
std::atomic<bool> s_external_font_fs_busy{false};
TaskHandle_t s_external_font_owner_task = nullptr;

bool external_font_load_fs_scope_active()
{
    return s_external_font_load_fs_depth.load(std::memory_order_relaxed) > 0 &&
           s_external_font_owner_task == xTaskGetCurrentTaskHandle();
}

void mark_external_font_fs_busy()
{
    if (external_font_load_fs_scope_active())
    {
        s_external_font_fs_busy.store(true, std::memory_order_release);
    }
}

inline int flash_fs_fd_from_ptr(void* file_p)
{
    return static_cast<int>(reinterpret_cast<uintptr_t>(file_p) - 1U);
}

inline void* flash_fs_ptr_from_fd(int fd)
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(fd + 1));
}

lv_fs_res_t flash_fs_errno_to_res(int errno_val)
{
    switch (errno_val)
    {
    case 0:
        return LV_FS_RES_OK;
    case EIO:
        return LV_FS_RES_HW_ERR;
    case EFAULT:
        return LV_FS_RES_FS_ERR;
    case ENOENT:
        return LV_FS_RES_NOT_EX;
    case ENOSPC:
        return LV_FS_RES_FULL;
    case EALREADY:
        return LV_FS_RES_LOCKED;
    case EACCES:
        return LV_FS_RES_DENIED;
    case EBUSY:
        return LV_FS_RES_BUSY;
    case ETIMEDOUT:
        return LV_FS_RES_TOUT;
    case ENOSYS:
        return LV_FS_RES_NOT_IMP;
    case ENOMEM:
        return LV_FS_RES_OUT_OF_MEM;
    case EINVAL:
        return LV_FS_RES_INV_PARAM;
    default:
        return LV_FS_RES_UNKNOWN;
    }
}

lv_fs_res_t sd_fs_error_to_res()
{
    return LV_FS_RES_FS_ERR;
}

bool sd_fs_ready_cb(lv_fs_drv_t* drv)
{
    LV_UNUSED(drv);
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

void* sd_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    // This is a semantic filesystem adapter, not a second SPI transaction
    // owner. SdRuntimeFile acquires the coordinator only at its SdFat driver
    // boundary; retaining a token around this whole callback would pin the
    // bus across allocation, path handling, and multi-sector filesystem work.

    const char* open_mode = "r";
    if (mode == LV_FS_MODE_WR)
    {
        open_mode = "w";
    }
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
    {
        open_mode = "r+";
    }

    auto* file = new (std::nothrow)::platform::esp::arduino_common::storage::SdRuntimeFile();
    if (file == nullptr)
    {
        return nullptr;
    }
    if (!file->open(path, open_mode))
    {
        mark_external_font_fs_busy();
        delete file;
        return nullptr;
    }
    return file;
}

lv_fs_res_t sd_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    file->close();
    delete file;
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr || buf == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    const int result = file->read(buf, btr);
    if (result < 0)
    {
        mark_external_font_fs_busy();
        return sd_fs_error_to_res();
    }
    if (br != nullptr)
    {
        *br = static_cast<uint32_t>(result);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_write(lv_fs_drv_t* drv,
                        void* file_p,
                        const void* buf,
                        uint32_t btw,
                        uint32_t* bw)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr || buf == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    const std::size_t result = file->write(buf, btw);
    if (bw != nullptr)
    {
        *bw = static_cast<uint32_t>(result);
    }
    if (result != btw)
    {
        mark_external_font_fs_busy();
        return sd_fs_error_to_res();
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    uint64_t target = pos;
    switch (whence)
    {
    case LV_FS_SEEK_CUR:
        target = file->position() + pos;
        break;
    case LV_FS_SEEK_END:
        target = file->size() + pos;
        break;
    case LV_FS_SEEK_SET:
    default:
        target = pos;
        break;
    }

    if (!file->seek(target))
    {
        mark_external_font_fs_busy();
        return sd_fs_error_to_res();
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr || pos_p == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    *pos_p = static_cast<uint32_t>(file->position());
    return LV_FS_RES_OK;
}

void* sd_fs_dir_open(lv_fs_drv_t* drv, const char* path)
{
    LV_UNUSED(drv);
    auto* dir = new (std::nothrow)::platform::esp::arduino_common::storage::SdRuntimeDir();
    if (dir == nullptr)
    {
        return nullptr;
    }
    if (!dir->open(path))
    {
        mark_external_font_fs_busy();
        delete dir;
        return nullptr;
    }
    return dir;
}

lv_fs_res_t sd_fs_dir_read(lv_fs_drv_t* drv, void* dir_p, char* fn, uint32_t fn_len)
{
    LV_UNUSED(drv);
    auto* dir = static_cast<::platform::esp::arduino_common::storage::SdRuntimeDir*>(dir_p);
    if (dir == nullptr || fn == nullptr || fn_len == 0)
    {
        return LV_FS_RES_INV_PARAM;
    }
    bool is_dir = false;
    if (!dir->read_next(fn, fn_len, &is_dir))
    {
        lv_strlcpy(fn, "", fn_len);
        return LV_FS_RES_OK;
    }
    if (is_dir && fn[0] != '/')
    {
        char tmp[LV_FS_MAX_PATH_LEN];
        lv_snprintf(tmp, sizeof(tmp), "/%s", fn);
        lv_strlcpy(fn, tmp, fn_len);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_dir_close(lv_fs_drv_t* drv, void* dir_p)
{
    LV_UNUSED(drv);
    auto* dir = static_cast<::platform::esp::arduino_common::storage::SdRuntimeDir*>(dir_p);
    if (dir == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    dir->close();
    delete dir;
    return LV_FS_RES_OK;
}

bool flash_fs_ready_for_io()
{
    return ::ui::fs::ensure_flash_storage_ready(false);
}

bool flash_fs_ready_cb(lv_fs_drv_t* drv)
{
    LV_UNUSED(drv);
    return flash_fs_ready_for_io();
}

void flash_fs_make_full_path(const char* path, char* out, size_t out_size)
{
    if (path == nullptr || path[0] == '\0')
    {
        lv_snprintf(out, out_size, "%s", kLvglFlashFsMountPoint);
        return;
    }

    if (path[0] == '/')
    {
        lv_snprintf(out, out_size, "%s%s", kLvglFlashFsMountPoint, path);
        return;
    }

    lv_snprintf(out, out_size, "%s/%s", kLvglFlashFsMountPoint, path);
}

void* flash_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    static uint8_t s_open_fail_log_count = 0;

    if (!flash_fs_ready_for_io())
    {
        if (s_open_fail_log_count < 8)
        {
            Serial.printf("[LVGL][FS] F open blocked: flash not ready path=%s\n",
                          path ? path : "<null>");
            ++s_open_fail_log_count;
        }
        return nullptr;
    }

    int flags = 0;
    if (mode == LV_FS_MODE_WR)
    {
        flags = O_WRONLY | O_CREAT;
    }
    else if (mode == LV_FS_MODE_RD)
    {
        flags = O_RDONLY;
    }
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
    {
        flags = O_RDWR | O_CREAT;
    }

    char full_path[LV_FS_MAX_PATH_LEN];
    flash_fs_make_full_path(path, full_path, sizeof(full_path));

    const int fd = ::open(full_path, flags, 0666);
    if (fd < 0)
    {
        if (s_open_fail_log_count < 8)
        {
            Serial.printf("[LVGL][FS] F open failed path=%s full=%s errno=%d mode=%u\n",
                          path ? path : "<null>",
                          full_path,
                          errno,
                          static_cast<unsigned>(mode));
            ++s_open_fail_log_count;
        }
        return nullptr;
    }

    return flash_fs_ptr_from_fd(fd);
}

lv_fs_res_t flash_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    return ::close(flash_fs_fd_from_ptr(file_p)) < 0 ? flash_fs_errno_to_res(errno)
                                                     : LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    LV_UNUSED(drv);

    const ssize_t result = ::read(flash_fs_fd_from_ptr(file_p), buf, btr);
    if (result < 0)
    {
        return flash_fs_errno_to_res(errno);
    }

    if (br != nullptr)
    {
        *br = static_cast<uint32_t>(result);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_write(lv_fs_drv_t* drv,
                           void* file_p,
                           const void* buf,
                           uint32_t btw,
                           uint32_t* bw)
{
    LV_UNUSED(drv);

    const ssize_t result = ::write(flash_fs_fd_from_ptr(file_p), buf, btw);
    if (result < 0)
    {
        return flash_fs_errno_to_res(errno);
    }

    if (bw != nullptr)
    {
        *bw = static_cast<uint32_t>(result);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);

    int origin = SEEK_SET;
    switch (whence)
    {
    case LV_FS_SEEK_CUR:
        origin = SEEK_CUR;
        break;
    case LV_FS_SEEK_END:
        origin = SEEK_END;
        break;
    case LV_FS_SEEK_SET:
    default:
        origin = SEEK_SET;
        break;
    }

    return ::lseek(flash_fs_fd_from_ptr(file_p), static_cast<off_t>(pos), origin) < 0
               ? flash_fs_errno_to_res(errno)
               : LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p)
{
    LV_UNUSED(drv);

    const off_t offset = ::lseek(flash_fs_fd_from_ptr(file_p), 0, SEEK_CUR);
    if (offset < 0)
    {
        return flash_fs_errno_to_res(errno);
    }

    if (pos_p != nullptr)
    {
        *pos_p = static_cast<uint32_t>(offset);
    }
    return LV_FS_RES_OK;
}

void* flash_fs_dir_open(lv_fs_drv_t* drv, const char* path)
{
    LV_UNUSED(drv);
    static uint8_t s_dir_fail_log_count = 0;

    if (!flash_fs_ready_for_io())
    {
        if (s_dir_fail_log_count < 8)
        {
            Serial.printf("[LVGL][FS] F dir_open blocked: flash not ready path=%s\n",
                          path ? path : "<null>");
            ++s_dir_fail_log_count;
        }
        return nullptr;
    }

    char full_path[LV_FS_MAX_PATH_LEN];
    flash_fs_make_full_path(path, full_path, sizeof(full_path));
    DIR* dir = ::opendir(full_path);
    if (!dir && s_dir_fail_log_count < 8)
    {
        Serial.printf("[LVGL][FS] F dir_open failed path=%s full=%s errno=%d\n",
                      path ? path : "<null>",
                      full_path,
                      errno);
        ++s_dir_fail_log_count;
    }
    return dir;
}

lv_fs_res_t flash_fs_dir_read(lv_fs_drv_t* drv, void* dir_p, char* fn, uint32_t fn_len)
{
    LV_UNUSED(drv);

    if (fn == nullptr || fn_len == 0)
    {
        return LV_FS_RES_INV_PARAM;
    }

    struct dirent* entry = nullptr;
    do
    {
        entry = ::readdir(static_cast<DIR*>(dir_p));
        if (entry == nullptr)
        {
            lv_strlcpy(fn, "", fn_len);
            return LV_FS_RES_OK;
        }

        if (entry->d_type == DT_DIR)
        {
            lv_snprintf(fn, fn_len, "/%s", entry->d_name);
        }
        else
        {
            lv_strlcpy(fn, entry->d_name, fn_len);
        }
    } while (lv_strcmp(fn, "/.") == 0 || lv_strcmp(fn, "/..") == 0);

    return LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_dir_close(lv_fs_drv_t* drv, void* dir_p)
{
    LV_UNUSED(drv);
    return ::closedir(static_cast<DIR*>(dir_p)) < 0 ? flash_fs_errno_to_res(errno)
                                                    : LV_FS_RES_OK;
}

void init_flash_fs_driver()
{
    if (s_flash_fs_ready)
    {
        return;
    }

    lv_fs_drv_init(&s_flash_fs_drv);
    s_flash_fs_drv.letter = kLvglFlashFsLetter;
    s_flash_fs_drv.ready_cb = flash_fs_ready_cb;
    s_flash_fs_drv.open_cb = flash_fs_open;
    s_flash_fs_drv.close_cb = flash_fs_close;
    s_flash_fs_drv.read_cb = flash_fs_read;
    s_flash_fs_drv.write_cb = flash_fs_write;
    s_flash_fs_drv.seek_cb = flash_fs_seek;
    s_flash_fs_drv.tell_cb = flash_fs_tell;
    s_flash_fs_drv.dir_open_cb = flash_fs_dir_open;
    s_flash_fs_drv.dir_read_cb = flash_fs_dir_read;
    s_flash_fs_drv.dir_close_cb = flash_fs_dir_close;
    lv_fs_drv_register(&s_flash_fs_drv);
    s_flash_fs_ready = true;

    char letters[16];
    letters[0] = '\0';
    lv_fs_get_letters(letters);
    Serial.printf("[LVGL][FS] flash driver registered letter=%c ready=%d letters=%s\n",
                  kLvglFlashFsLetter,
                  lv_fs_is_ready(kLvglFlashFsLetter) ? 1 : 0,
                  letters);
}

void init_sd_fs_driver()
{
    if (s_sd_fs_ready)
    {
        return;
    }

    lv_fs_drv_init(&s_sd_fs_drv);
    s_sd_fs_drv.letter = TRAIL_MATE_LVGL_SD_FS_LETTER;
    s_sd_fs_drv.ready_cb = sd_fs_ready_cb;
    s_sd_fs_drv.open_cb = sd_fs_open;
    s_sd_fs_drv.close_cb = sd_fs_close;
    s_sd_fs_drv.read_cb = sd_fs_read;
    s_sd_fs_drv.write_cb = sd_fs_write;
    s_sd_fs_drv.seek_cb = sd_fs_seek;
    s_sd_fs_drv.tell_cb = sd_fs_tell;
    s_sd_fs_drv.dir_open_cb = sd_fs_dir_open;
    s_sd_fs_drv.dir_read_cb = sd_fs_dir_read;
    s_sd_fs_drv.dir_close_cb = sd_fs_dir_close;
    lv_fs_drv_register(&s_sd_fs_drv);
    s_sd_fs_ready = true;

    char letters[16];
    letters[0] = '\0';
    lv_fs_get_letters(letters);
    Serial.printf("[LVGL][FS] SD driver registered letter=%c ready=%d letters=%s\n",
                  TRAIL_MATE_LVGL_SD_FS_LETTER,
                  lv_fs_is_ready(TRAIL_MATE_LVGL_SD_FS_LETTER) ? 1 : 0,
                  letters);
}
} // namespace

namespace
{

struct PendingDisplayFlush
{
    lv_display_t* display = nullptr;
    LilyGo_Display* plane = nullptr;
    lv_area_t area{};
    uint16_t* pixels = nullptr;
    uint32_t sequence = 0;
    uint32_t deferred_since_ms = 0;
    bool active = false;
};

// This record owns identity only. It must never become a frame copy or grow
// with an LVGL area; the original LVGL buffer remains the transfer payload.
static_assert(sizeof(PendingDisplayFlush) <= 64U,
              "Pending display state must remain a small scalar record");

PendingDisplayFlush s_pending_display_flush{};

const char* display_transfer_result_label(DisplayTransferResult result)
{
    switch (result)
    {
    case DisplayTransferResult::Completed:
        return "completed";
    case DisplayTransferResult::Busy:
        return "busy";
    case DisplayTransferResult::Unavailable:
        return "unavailable";
    case DisplayTransferResult::Failed:
    default:
        return "failed";
    }
}

DisplayTransferResult submit_display_transfer(LilyGo_Display* plane,
                                              const lv_area_t& area,
                                              uint16_t* pixels)
{
    if (plane == nullptr || pixels == nullptr)
    {
        return DisplayTransferResult::Failed;
    }
    return plane->transferPixels(area.x1,
                                 area.y1,
                                 lv_area_get_width(&area),
                                 lv_area_get_height(&area),
                                 pixels);
}

void invalidate_after_display_failure()
{
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
}

void record_display_failure(const PendingDisplayFlush& pending,
                            DisplayTransferResult result,
                            uint32_t wait_ms)
{
    auto& coordinator = platform::esp::common::shared_spi_coordinator();
    coordinator.noteDisplayFrameFailed();
    Serial.printf("[SPI][DISPLAY] flush_failed seq=%lu result=%s area=(%d,%d)-(%d,%d) "
                  "wait_ms=%lu requests=%lu completed=%lu deferred=%lu busy=%lu failed=%lu\n",
                  static_cast<unsigned long>(pending.sequence),
                  display_transfer_result_label(result),
                  static_cast<int>(pending.area.x1),
                  static_cast<int>(pending.area.y1),
                  static_cast<int>(pending.area.x2),
                  static_cast<int>(pending.area.y2),
                  static_cast<unsigned long>(wait_ms),
                  static_cast<unsigned long>(coordinator.displayFrameRequests()),
                  static_cast<unsigned long>(coordinator.displayFrameCompletions()),
                  static_cast<unsigned long>(coordinator.displayFrameDeferrals()),
                  static_cast<unsigned long>(coordinator.displayFrameBusyRetries()),
                  static_cast<unsigned long>(coordinator.displayFrameFailures()));
}

void disp_flush_wait(lv_display_t* disp_drv)
{
    if (!s_pending_display_flush.active || s_pending_display_flush.display != disp_drv)
    {
        Serial.printf("[SPI][DISPLAY] flush_wait invariant_violation display=%p pending=%p active=%d\n",
                      static_cast<void*>(disp_drv),
                      static_cast<void*>(s_pending_display_flush.display),
                      s_pending_display_flush.active ? 1 : 0);
        return;
    }

    for (;;)
    {
        PendingDisplayFlush& pending = s_pending_display_flush;
        const DisplayTransferResult result =
            submit_display_transfer(pending.plane, pending.area, pending.pixels);
        const uint32_t wait_ms =
            static_cast<uint32_t>(sys::millis_now() - pending.deferred_since_ms);
        if (result == DisplayTransferResult::Completed)
        {
            auto& coordinator = platform::esp::common::shared_spi_coordinator();
            coordinator.noteDisplayFrameCompleted(wait_ms);
            Serial.printf("[SPI][DISPLAY] flush_deferred_complete seq=%lu area=(%d,%d)-(%d,%d) "
                          "wait_ms=%lu requests=%lu completed=%lu deferred=%lu busy=%lu failed=%lu\n",
                          static_cast<unsigned long>(pending.sequence),
                          static_cast<int>(pending.area.x1),
                          static_cast<int>(pending.area.y1),
                          static_cast<int>(pending.area.x2),
                          static_cast<int>(pending.area.y2),
                          static_cast<unsigned long>(wait_ms),
                          static_cast<unsigned long>(coordinator.displayFrameRequests()),
                          static_cast<unsigned long>(coordinator.displayFrameCompletions()),
                          static_cast<unsigned long>(coordinator.displayFrameDeferrals()),
                          static_cast<unsigned long>(coordinator.displayFrameBusyRetries()),
                          static_cast<unsigned long>(coordinator.displayFrameFailures()));
            s_pending_display_flush = {};
            // LVGL v9 clears its flushing state immediately after this wait
            // callback returns. Do not call lv_display_flush_ready() here.
            return;
        }

        if (result == DisplayTransferResult::Failed)
        {
            record_display_failure(pending, result, wait_ms);
            s_pending_display_flush = {};
            invalidate_after_display_failure();
            return;
        }

        // Busy/Unavailable keeps the exact LVGL area and buffer pending. The
        // coordinator already accounts for Busy acquisition attempts; yielding
        // one tick avoids a retry spin when an adapter is temporarily unavailable.
        vTaskDelay(1);
    }
}

} // namespace

static void disp_flush(lv_display_t* disp_drv, const lv_area_t* area, uint8_t* color_p)
{
    static uint8_t s_tdeck_pro_flush_log_count = 0;
    static uint32_t s_display_flush_log_count = 0;
#if LV_TEST_FLUSH_LOG
    static uint32_t s_flush_count = 0;
    static uint32_t s_flush_last_ms = 0;
    static lv_area_t s_last_area = {0, 0, 0, 0};
#if LV_TEST_FLUSH_SAMPLE
    static uint32_t s_zero_streak = 0;
#endif
#endif
    size_t len = lv_area_get_size(area);
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    auto* plane = (LilyGo_Display*)lv_display_get_user_data(disp_drv);
    const uint32_t flush_sequence = ++s_display_flush_log_count;
    if (flush_sequence <= 8U)
    {
        uint32_t nonzero = 0;
        if (color_p != nullptr)
        {
            const uint16_t* pixels = reinterpret_cast<const uint16_t*>(color_p);
            for (size_t i = 0; i < len; ++i)
            {
                if (pixels[i] != 0U)
                {
                    ++nonzero;
                }
            }
        }
        Serial.printf("[LVGL][DISPLAY] flush_begin seq=%lu area=(%d,%d)-(%d,%d) "
                      "size=%lux%lu pixels=%lu nonzero=%lu plane=%p\n",
                      static_cast<unsigned long>(flush_sequence),
                      static_cast<int>(area->x1),
                      static_cast<int>(area->y1),
                      static_cast<int>(area->x2),
                      static_cast<int>(area->y2),
                      static_cast<unsigned long>(w),
                      static_cast<unsigned long>(h),
                      static_cast<unsigned long>(len),
                      static_cast<unsigned long>(nonzero),
                      static_cast<void*>(plane));
    }

#if defined(ARDUINO_T_DECK_PRO)
    if (s_tdeck_pro_flush_log_count < 8)
    {
        uint32_t blackish = 0;
        if (color_p != nullptr)
        {
            const uint16_t* pix = reinterpret_cast<const uint16_t*>(color_p);
            for (size_t i = 0; i < len; ++i)
            {
                if (pix[i] == 0x0000)
                {
                    blackish++;
                }
            }
        }
        Serial.printf("[LVGL][TDeckPro] flush #%u area=(%d,%d)-(%d,%d) len=%lu rgb565_black=%lu\n",
                      static_cast<unsigned>(s_tdeck_pro_flush_log_count + 1),
                      static_cast<int>(area->x1),
                      static_cast<int>(area->y1),
                      static_cast<int>(area->x2),
                      static_cast<int>(area->y2),
                      static_cast<unsigned long>(len),
                      static_cast<unsigned long>(blackish));
        s_tdeck_pro_flush_log_count++;
    }
#endif

    const DisplayTransferResult result =
        submit_display_transfer(plane, *area, reinterpret_cast<uint16_t*>(color_p));
    if (result == DisplayTransferResult::Busy || result == DisplayTransferResult::Unavailable)
    {
        auto& coordinator = platform::esp::common::shared_spi_coordinator();
        if (s_pending_display_flush.active)
        {
            Serial.printf("[SPI][DISPLAY] flush_pending_conflict seq=%lu active_seq=%lu\n",
                          static_cast<unsigned long>(flush_sequence),
                          static_cast<unsigned long>(s_pending_display_flush.sequence));
            return;
        }
        s_pending_display_flush.display = disp_drv;
        s_pending_display_flush.plane = plane;
        s_pending_display_flush.area = *area;
        s_pending_display_flush.pixels = reinterpret_cast<uint16_t*>(color_p);
        s_pending_display_flush.sequence = flush_sequence;
        s_pending_display_flush.deferred_since_ms = sys::millis_now();
        s_pending_display_flush.active = true;
        coordinator.noteDisplayFrameDeferred();
        Serial.printf("[SPI][DISPLAY] flush_deferred seq=%lu result=%s area=(%d,%d)-(%d,%d) "
                      "requests=%lu completed=%lu deferred=%lu busy=%lu failed=%lu\n",
                      static_cast<unsigned long>(flush_sequence),
                      display_transfer_result_label(result),
                      static_cast<int>(area->x1),
                      static_cast<int>(area->y1),
                      static_cast<int>(area->x2),
                      static_cast<int>(area->y2),
                      static_cast<unsigned long>(coordinator.displayFrameRequests()),
                      static_cast<unsigned long>(coordinator.displayFrameCompletions()),
                      static_cast<unsigned long>(coordinator.displayFrameDeferrals()),
                      static_cast<unsigned long>(coordinator.displayFrameBusyRetries()),
                      static_cast<unsigned long>(coordinator.displayFrameFailures()));
        return;
    }

    if (result == DisplayTransferResult::Failed)
    {
        PendingDisplayFlush failed{};
        failed.display = disp_drv;
        failed.plane = plane;
        failed.area = *area;
        failed.pixels = reinterpret_cast<uint16_t*>(color_p);
        failed.sequence = flush_sequence;
        record_display_failure(failed, result, 0U);
        lv_display_flush_ready(disp_drv);
        invalidate_after_display_failure();
        return;
    }

    platform::esp::common::shared_spi_coordinator().noteDisplayFrameCompleted(0U);
    lv_display_flush_ready(disp_drv);
    if (flush_sequence <= 8U)
    {
        auto& coordinator = platform::esp::common::shared_spi_coordinator();
        Serial.printf("[LVGL][DISPLAY] flush_complete seq=%lu requests=%lu "
                      "completed=%lu deferred=%lu busy=%lu failed=%lu\n",
                      static_cast<unsigned long>(flush_sequence),
                      static_cast<unsigned long>(coordinator.displayFrameRequests()),
                      static_cast<unsigned long>(coordinator.displayFrameCompletions()),
                      static_cast<unsigned long>(coordinator.displayFrameDeferrals()),
                      static_cast<unsigned long>(coordinator.displayFrameBusyRetries()),
                      static_cast<unsigned long>(coordinator.displayFrameFailures()));
    }

#if LV_TEST_FLUSH_LOG
#if LV_TEST_FLUSH_SAMPLE
    bool all_zero_sample = false;
    if (color_p && len > 0)
    {
        const uint16_t* pix = reinterpret_cast<const uint16_t*>(color_p);
        size_t step = len / 8;
        if (step == 0) step = 1;
        all_zero_sample = true;
        for (size_t i = 0, idx = 0; i < 8 && idx < len; ++i, idx += step)
        {
            if (pix[idx] != 0)
            {
                all_zero_sample = false;
                break;
            }
        }
    }
    if (all_zero_sample)
    {
        s_zero_streak++;
    }
    else
    {
        s_zero_streak = 0;
    }
#endif
    s_flush_count++;
    s_last_area = *area;
    uint32_t now_ms = millis();
    if (now_ms - s_flush_last_ms >= 1000)
    {
        lv_obj_t* scr = lv_screen_active();
        unsigned child_cnt = scr ? (unsigned)lv_obj_get_child_cnt(scr) : 0;
        Serial.printf("[LVGL] flush/s=%lu last_area=%d,%d-%d,%d children=%u",
                      (unsigned long)s_flush_count,
                      (int)s_last_area.x1, (int)s_last_area.y1,
                      (int)s_last_area.x2, (int)s_last_area.y2,
                      child_cnt);
#if LV_TEST_FLUSH_SAMPLE
        Serial.printf(" zero_streak=%lu", (unsigned long)s_zero_streak);
#endif
        Serial.printf("\n");
        s_flush_count = 0;
        s_flush_last_ms = now_ms;
    }
#endif
}

#ifdef USING_INPUT_DEV_TOUCHPAD

#if defined(ARDUINO_WIO_TRACKER_L2)
constexpr int16_t kScreenResumeSwipeMinY = 60;
constexpr int16_t kScreenResumeSwipeMaxX = 80;
#endif

static void touchpad_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    static int16_t x = 0;
    static int16_t y = 0;
    static bool was_touched = false;

#if defined(ARDUINO_WIO_TRACKER_L2)
    static bool resume_swipe_tracking = false;
    static int16_t resume_start_x = 0;
    static int16_t resume_start_y = 0;
    static int16_t resume_last_x = 0;
    static int16_t resume_last_y = 0;
#endif

#if defined(ARDUINO_T_DECK_PRO)
    static bool debug_touch_active = false;
#endif

    auto* plane =
        static_cast<LilyGo_Display*>(lv_indev_get_user_data(drv));

    const uint8_t touched =
        plane->getPoint(&x, &y, 1);

    if (touched)
    {
#if defined(ARDUINO_T_DECK_PRO)
        if (!debug_touch_active)
        {
            Serial.printf(
                "[DEBUG-touch-a7682e] down lvgl=%d,%d\n",
                x,
                y);

            debug_touch_active = true;
        }
#endif

        const bool touch_started = !was_touched;
        was_touched = true;

        input::MorseEngine::notifyTouch();

        const bool screen_power_active =
            ::platform::ui::screen::is_sleeping() ||
            ::platform::ui::screen::is_saver_active();

        if (screen_power_active)
        {
#if defined(ARDUINO_WIO_TRACKER_L2)
            if (touch_started)
            {
                resume_swipe_tracking = true;

                resume_start_x = x;
                resume_start_y = y;

                resume_last_x = x;
                resume_last_y = y;
            }
            else if (resume_swipe_tracking)
            {
                resume_last_x = x;
                resume_last_y = y;
            }
#endif

            // Any touch wakes Sleeping -> WakePreview and refreshes the
            // preview timeout. The touch is consumed by the screen-power
            // layer and must not reach the underlying UI.
            ::platform::ui::screen::handle_input();

            data->state = LV_INDEV_STATE_REL;
            return;
        }

#if defined(ARDUINO_WIO_TRACKER_L2)
        // If the screen left the saver state while the finger is still down,
        // discard any unfinished resume gesture so it cannot leak into the
        // normal UI path.
        resume_swipe_tracking = false;
#endif

        ::platform::ui::screen::record_activity();

        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }

    if (was_touched)
    {
        was_touched = false;

#if defined(ARDUINO_WIO_TRACKER_L2)
        bool resumed = false;

        if (resume_swipe_tracking)
        {
            const int16_t delta_y =
                static_cast<int16_t>(
                    resume_start_y - resume_last_y);

            const int16_t delta_x =
                static_cast<int16_t>(
                    std::abs(
                        static_cast<int>(
                            resume_last_x - resume_start_x)));

            const bool swipe_up =
                delta_y >= kScreenResumeSwipeMinY &&
                delta_x <= kScreenResumeSwipeMaxX;

            if (swipe_up)
            {
                Serial.printf(
                    "[ScreenPower][Touch] resume swipe "
                    "start=(%d,%d) end=(%d,%d) dx=%d dy=%d\n",
                    resume_start_x,
                    resume_start_y,
                    resume_last_x,
                    resume_last_y,
                    delta_x,
                    delta_y);

                ::platform::ui::screen::request_resume();
                resumed = true;
            }

            resume_swipe_tracking = false;
        }

        if (!resumed)
        {
            ::platform::ui::screen::handle_input_release();
        }
#else
        ::platform::ui::screen::handle_input_release();
#endif
    }

#if defined(ARDUINO_T_DECK_PRO)
    if (debug_touch_active)
    {
        Serial.printf(
            "[DEBUG-touch-a7682e] up lvgl=%d,%d\n",
            x,
            y);

        debug_touch_active = false;
    }
#endif
    data->state = LV_INDEV_STATE_REL;
}
#endif

#ifdef USING_INPUT_DEV_ROTARY
// Forward declaration from GPS UI runtime

extern bool isGPSLoadingTiles();

static void lv_encoder_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    RotaryMsg_t msg = plane->getRotary();
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;
#endif

    // Screen power transitions consume the input event before normal UI
    // navigation. Keyboard Space is the explicit resume action.
    if (::platform::ui::screen::is_sleeping() ||
        ::platform::ui::screen::is_saver_active())
    {
        if (msg.dir != ROTARY_DIR_NONE || msg.centerBtnPressed)
        {
            ::platform::ui::screen::handle_input();
        }
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
        // T-Deck path already reset above.
#else
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED; // Don't pass input to UI
#endif
        return;
    }

    // If GPS is loading tiles, ignore input
    if (isGPSLoadingTiles())
    {
#if !defined(ARDUINO_T_DECK) && !defined(ARDUINO_T_DECK_PRO)
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
#endif
        return;
    }

    // Screen is awake, process input normally
    if (msg.dir != ROTARY_DIR_NONE || msg.centerBtnPressed)
    {
        ::platform::ui::screen::record_activity();
    }

    switch (msg.dir)
    {
    case ROTARY_DIR_UP:
        data->enc_diff = 1;
        break;
    case ROTARY_DIR_DOWN:
        data->enc_diff = -1;
        break;
    default:
#if !defined(ARDUINO_T_DECK) && !defined(ARDUINO_T_DECK_PRO)
        data->state = LV_INDEV_STATE_RELEASED;
#endif
        break;
    }
    if (msg.centerBtnPressed)
    {
        data->state = LV_INDEV_STATE_PRESSED;
    }
    plane->feedback((void*)drv);
}
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
static void keypad_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    char c = '\0';
    uint32_t key = 0;
    bool from_nav = false;
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    int state = plane->getNavKey(&key);
    if (state >= 0)
    {
        from_nav = true;
        switch (key)
        {
        case INPUT_NAV_KEY_LEFT:
            key = LV_KEY_LEFT;
            break;
        case INPUT_NAV_KEY_RIGHT:
            key = LV_KEY_RIGHT;
            break;
        case INPUT_NAV_KEY_UP:
            key = LV_KEY_UP;
            break;
        case INPUT_NAV_KEY_DOWN:
            key = LV_KEY_DOWN;
            break;
        case INPUT_NAV_KEY_ENTER:
            key = LV_KEY_ENTER;
            break;
        default:
            state = -1;
            break;
        }
    }
    else
    {
        state = plane->getKeyChar(&c);
        if (state >= 0)
        {
            key = c == '\n' || c == '\r' ? LV_KEY_ENTER : static_cast<uint8_t>(c);
        }
    }

    // Screen-power input is consumed before normal keyboard dispatch.
    // Space is the keyboard resume gesture and restores the page/focus
    // that was active before sleep.
    if (::platform::ui::screen::is_sleeping() ||
        ::platform::ui::screen::is_saver_active())
    {
        if (state == KEYBOARD_PRESSED)
        {
            if (!from_nav && key == ' ')
            {
                ::platform::ui::screen::request_resume();
            }
            else
            {
                ::platform::ui::screen::handle_input();
            }
        }
        else if (state == KEYBOARD_RELEASED)
        {
            ::platform::ui::screen::handle_input_release();
        }
        data->state = LV_INDEV_STATE_REL; // Don't pass key to UI
        return;
    }

    // Screen is awake, process input normally
    if (!from_nav && (state == KEYBOARD_PRESSED || state == KEYBOARD_RELEASED))
    {
        AppScreen* active_app = ui_get_active_app();
        const bool on_menu = active_app == nullptr;
        const bool on_walkie_page =
            active_app != nullptr && active_app->stable_id() != nullptr &&
            std::strcmp(active_app->stable_id(), "walkie_talkie") == 0;

#if defined(ARDUINO_T_DECK_PRO)
        // The Pro keyboard has no dedicated cursor keys.  Translate its
        // physical W/S keys only while the text-shell menu owns focus; text
        // fields on adapted pages must continue to accept literal w and s.
        if (on_menu && (c == 'w' || c == 'W'))
        {
            key = LV_KEY_PREV;
            from_nav = true;
        }
        else if (on_menu && (c == 's' || c == 'S'))
        {
            key = LV_KEY_NEXT;
            from_nav = true;
        }
#endif

        if (on_menu && ui::menu_runtime::handleWalkieKey(c, state))
        {
            ::platform::ui::screen::record_activity();
            plane->feedback((void*)drv);
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        if (on_menu && ui::menu_runtime::handleShortcutKey(c, state))
        {
            ::platform::ui::screen::record_activity();
            plane->feedback((void*)drv);
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        if (on_walkie_page)
        {
            walkie::on_key_event(c, state);
        }
    }

#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    if (!from_nav && state == KEYBOARD_PRESSED && c == ' ' && ui_get_active_app() == nullptr)
    {
        ::platform::ui::screen::record_activity();
        menu_show();
        plane->feedback((void*)drv);
        data->state = LV_INDEV_STATE_REL;
        return;
    }
#endif

    if (state == KEYBOARD_PRESSED)
    {
        ::platform::ui::screen::record_activity();
        data->key = key;
        data->state = LV_INDEV_STATE_PR;
        plane->feedback((void*)drv);
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}
#endif

static uint32_t lv_tick_get_callback(void)
{
    return millis();
}

static void lv_rounder_cb(lv_event_t* e)
{
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    if (!(area->x2 & 1))
        area->x2++;
    if (area->y1 & 1)
        area->y1--;
    if (!(area->y2 & 1))
        area->y2++;
}

static void lv_res_changed_cb(lv_event_t* e)
{
    auto* plane = (LilyGo_Display*)lv_event_get_user_data(e);
    plane->setRotation(lv_display_get_rotation(NULL));
}

void beginLvglHelper(LilyGo_Display& board, bool debug)
{
    Serial.println("[LVGL] init");
    lv_init();
    Serial.println("[LVGL] init done");

    init_sd_fs_driver();
    init_flash_fs_driver();

#if LV_USE_LOG
    if (debug)
    {
        lv_log_register_print_cb(lv_log_print_g_cb);
    }
#endif

    // Allocate display buffers. T-Deck retains DMA capability for the display
    // transport, while its large LVGL draw buffers default to PSRAM to protect
    // internal SRAM for task stacks, radio, and peripheral DMA allocations.
    bool use_dma_draw_buffers = board.useDMA();
#if defined(ARDUINO_T_DECK)
    use_dma_draw_buffers = kTDeckUseDmaDrawBuffers;
#endif
#if LV_TEST_FORCE_DMA_BUF
    use_dma_draw_buffers = true;
#endif
    Serial.printf("[LVGL] buffer alloc start (use_dma_draw_buffers=%d)\n", use_dma_draw_buffers ? 1 : 0);
    Serial.printf("[LVGL] free heap internal=%u dma=%u psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
    size_t lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);
    size_t full_screen_size = lv_buffer_size;

    if (use_dma_draw_buffers)
    {
        // For DMA, keep internal RAM pressure low to avoid starving other subsystems.
#if LV_TEST_FORCE_DMA_FULL_SIZE
        // keep full size for testing if memory allows
#else
#if defined(ARDUINO_T_DECK)
        lv_buffer_size = board.width() * kTDeckDmaDrawBufferLines * sizeof(lv_color16_t);
#else
        lv_buffer_size = (board.width() * board.height() / 6) * sizeof(lv_color16_t);
#endif
#endif
        buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        log_d("Using DMA buffers, size: %d bytes each", lv_buffer_size);

        if (!buf || !buf1)
        {
            Serial.println("[LVGL] DMA buffer alloc failed, fallback to PSRAM");
            if (buf)
            {
                free(buf);
            }
            if (buf1)
            {
                free(buf1);
            }
            buf = nullptr;
            buf1 = nullptr;
            use_dma_draw_buffers = false;
        }
    }
    if (!use_dma_draw_buffers)
    {
        lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);
#if HAS_PSRAM
        // For non-DMA, use full screen buffer in PSRAM
        buf = (lv_color16_t*)ps_malloc(lv_buffer_size);
        buf1 = (lv_color16_t*)ps_malloc(lv_buffer_size);
        log_d("Using PSRAM buffers, size: %d bytes each", lv_buffer_size);
        if (!buf || !buf1)
        {
            if (buf)
            {
                free(buf);
            }
            if (buf1)
            {
                free(buf1);
            }
            buf = nullptr;
            buf1 = nullptr;

            // Avoid grabbing huge internal buffers if PSRAM is unavailable.
            lv_buffer_size = (board.width() * board.height() / 8) * sizeof(lv_color16_t);
            if (lv_buffer_size < (board.width() * 20U * sizeof(lv_color16_t)))
            {
                lv_buffer_size = board.width() * 20U * sizeof(lv_color16_t);
            }
            buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
            buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
            if (!buf || !buf1)
            {
                if (buf)
                {
                    free(buf);
                }
                if (buf1)
                {
                    free(buf1);
                }
                buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
                buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
            }
            Serial.printf("[LVGL] PSRAM alloc failed, fallback to small internal buffers (size=%u)\n",
                          (unsigned)lv_buffer_size);
        }
#else
        // For boards without PSRAM, use heap
        buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
        buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
        Serial.println("[LVGL] PSRAM disabled, using heap buffers");
#endif
    }

    if (!buf || !buf1)
    {
        Serial.println("[LVGL] Failed to allocate display buffers");
        log_e("Failed to allocate LVGL display buffers!");
        return;
    }

    Serial.printf("[LVGL] buffers ready (size=%u)\n", static_cast<unsigned>(lv_buffer_size));
    disp_drv = lv_display_create(board.width(), board.height());

    if (board.needFullRefresh())
    {
        lv_display_render_mode_t mode = LV_DISPLAY_RENDER_MODE_FULL;
        if (lv_buffer_size < full_screen_size)
        {
            mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
            Serial.println("[LVGL] full-refresh downgraded to partial due buffer size");
        }
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, mode);
    }
    else
    {
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(disp_drv, lv_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    }
    Serial.println("[LVGL] display buffers set");
    Serial.printf("[LVGL] free heap after alloc internal=%u dma=%u psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
    lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp_drv, disp_flush);
    lv_display_set_flush_wait_cb(disp_drv, disp_flush_wait);
    lv_display_set_user_data(disp_drv, &board);

    lv_display_set_resolution(disp_drv, board.width(), board.height());
    lv_display_set_rotation(disp_drv, (lv_display_rotation_t)board.getRotation());
    lv_display_add_event_cb(disp_drv, lv_res_changed_cb, LV_EVENT_RESOLUTION_CHANGED, &board);

    lv_group_t* default_group = lv_group_get_default();
    if (default_group == nullptr)
    {
        default_group = lv_group_create();
        lv_group_set_default(default_group);
    }

#ifdef USING_INPUT_DEV_TOUCHPAD

    if (board.hasTouch())
    {
        indev_touch = lv_indev_create();
        lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_touch, touchpad_read);
        lv_indev_set_user_data(indev_touch, &board);
        lv_indev_enable(indev_touch, true);
        lv_indev_set_display(indev_touch, disp_drv);
        lv_indev_set_group(indev_touch, default_group);
    }
#endif

#ifdef USING_INPUT_DEV_ROTARY
    if (board.hasEncoder())
    {
        indev_encoder = lv_indev_create();
        lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(indev_encoder, lv_encoder_read);
        lv_indev_set_user_data(indev_encoder, &board);
        lv_indev_enable(indev_encoder, true);
        lv_indev_set_display(indev_encoder, disp_drv);
        lv_indev_set_group(indev_encoder, default_group);
    }
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
    if (board.hasKeyboard() || board.hasNavKeys())
    {
        indev_keyboard = lv_indev_create();
        lv_indev_set_type(indev_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev_keyboard, keypad_read);
        lv_indev_set_user_data(indev_keyboard, &board);
        lv_indev_enable(indev_keyboard, true);
        lv_indev_set_display(indev_keyboard, disp_drv);
        lv_indev_set_group(indev_keyboard, default_group);
    }
#endif

    lv_tick_set_cb(lv_tick_get_callback);
    lv_set_default_group(default_group);
}

#if defined(ARDUINO_T_DECK_PRO)
void serviceLvglDisplay(uint32_t now_ms)
{
    if (disp_drv == nullptr)
    {
        return;
    }

    auto* plane = static_cast<LilyGo_Display*>(lv_display_get_user_data(disp_drv));
    if (plane != nullptr)
    {
        plane->serviceDisplay(now_ms);
    }
}
#endif

void requestLvglFullRefresh()
{
    if (disp_drv == nullptr)
    {
        return;
    }

    auto* plane = static_cast<LilyGo_Display*>(lv_display_get_user_data(disp_drv));
    if (plane != nullptr)
    {
        plane->requestFullRefresh();
    }
}

void lv_set_default_group(lv_group_t* group)
{
    lv_indev_t* cur_drv = NULL;
    for (;;)
    {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv)
        {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}

lv_indev_t* lv_get_touch_indev()
{
    return indev_touch;
}

lv_indev_t* lv_get_keyboard_indev()
{
    return indev_keyboard;
}

lv_indev_t* lv_get_encoder_indev()
{
    return indev_encoder;
}

bool lv_begin_external_font_load_fs_scope()
{
    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    const unsigned depth =
        s_external_font_load_fs_depth.load(std::memory_order_acquire);
    if (depth > 0)
    {
        if (s_external_font_owner_task != current_task)
        {
            return false;
        }
        s_external_font_load_fs_depth.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    s_external_font_fs_busy.store(false, std::memory_order_release);
    s_external_font_owner_task = current_task;
    s_external_font_load_fs_depth.store(1, std::memory_order_release);
    return true;
}

void lv_end_external_font_load_fs_scope()
{
    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (s_external_font_owner_task != current_task)
    {
        return;
    }

    unsigned depth =
        s_external_font_load_fs_depth.load(std::memory_order_acquire);
    while (depth > 0)
    {
        const unsigned next_depth = depth - 1U;
        if (!s_external_font_load_fs_depth.compare_exchange_weak(
                depth, next_depth, std::memory_order_acq_rel))
        {
            continue;
        }
        if (next_depth == 0)
        {
            s_external_font_owner_task = nullptr;
        }
        return;
    }
}

bool lv_external_font_load_fs_was_busy()
{
    return s_external_font_fs_busy.exchange(false, std::memory_order_acq_rel);
}

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
namespace
{
constexpr size_t kLvglLargeAllocThresholdBytes = 2048;

bool lvgl_has_psram()
{
#if HAS_PSRAM
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
    return false;
#endif
}

bool lvgl_external_font_load_uses_strict_psram()
{
    // Binfont loading creates many small cmap and glyph-metadata objects.  A
    // size-only routing policy would put those fragments in internal RAM even
    // when the much larger glyph bitmap is in PSRAM.  The existing external
    // font FS scope serializes this exact operation on its owner task, so it
    // is also a safe, narrowly-scoped allocator ownership boundary.
    return lvgl_has_psram() && external_font_load_fs_scope_active();
}

uint32_t lvgl_primary_caps(size_t size)
{
    if (lvgl_has_psram() && size >= kLvglLargeAllocThresholdBytes)
    {
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    }
    return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}

uint32_t lvgl_secondary_caps(size_t size)
{
    if (lvgl_has_psram() && size >= kLvglLargeAllocThresholdBytes)
    {
        return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    }
    return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}
} // namespace

// LVGL 9.x custom memory management functions
extern "C" void lv_mem_init(void)
{
    return; /*Nothing to init*/
}

extern "C" void lv_mem_deinit(void)
{
    return; /*Nothing to deinit*/
}

extern "C" lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes)
{
    /*Not supported*/
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    /*Not supported*/
    LV_UNUSED(pool);
    return;
}

extern "C" void* lv_malloc_core(size_t size)
{
#if HAS_PSRAM
    if (size == 0)
    {
        return nullptr;
    }
    if (::platform::ui::psramUiRequired() || lvgl_external_font_load_uses_strict_psram())
    {
        // Font load must fail cleanly rather than silently consume the Wi-Fi,
        // task-stack, and DMA reserve in internal RAM.
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return heap_caps_malloc_prefer(size, 2, lvgl_primary_caps(size), lvgl_secondary_caps(size));
#else
    return malloc(size);
#endif
}

extern "C" void* lv_realloc_core(void* p, size_t new_size)
{
#if HAS_PSRAM
    if (new_size == 0)
    {
        heap_caps_free(p);
        return nullptr;
    }
    if (::platform::ui::psramUiRequired() || (p && esp_ptr_external_ram(p)) || lvgl_external_font_load_uses_strict_psram())
    {
        return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return heap_caps_realloc_prefer(
        p, new_size, 2, lvgl_primary_caps(new_size), lvgl_secondary_caps(new_size));
#else
    return realloc(p, new_size);
#endif
}

extern "C" void lv_free_core(void* p)
{
#if HAS_PSRAM
    heap_caps_free(p);
#else
    free(p);
#endif
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t* mon_p)
{
    /*Not supported*/
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /*Not supported*/
    return LV_RESULT_OK;
}
#endif

#endif

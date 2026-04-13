#include "platform/nrf52/arduino_common/internal_fs_utils.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

namespace platform::nrf52::arduino_common::internal_fs
{
namespace
{

constexpr const char* kDefaultLogTag = "[nrf52][fs]";

const char* resolveLogTag(const char* log_tag)
{
    return log_tag ? log_tag : kDefaultLogTag;
}

void logLine(const char* log_tag, const char* message)
{
    if (!log_tag || !message)
    {
        return;
    }
    Serial.printf("%s %s\n", resolveLogTag(log_tag), message);
}

void logPath(const char* log_tag, const char* message, const char* path)
{
    if (!log_tag || !message)
    {
        return;
    }
    Serial.printf("%s %s path=%s\n",
                  resolveLogTag(log_tag),
                  message,
                  path ? path : "?");
}

bool recoverByFormat(const char* log_tag)
{
    if (log_tag)
    {
        logLine(log_tag, "fs format recovery start");
    }
    if (!InternalFS.format())
    {
        logLine(log_tag, "fs format failed");
        return false;
    }
    if (!InternalFS.begin())
    {
        logLine(log_tag, "fs remount after format failed");
        return false;
    }
    if (log_tag)
    {
        logLine(log_tag, "fs recovered by format");
    }
    return true;
}

} // namespace

bool ensureMounted(bool allow_format_recovery, const char* log_tag)
{
    if (InternalFS.begin())
    {
        return true;
    }

    logLine(log_tag, "fs begin failed");
    return allow_format_recovery ? recoverByFormat(log_tag) : false;
}

void removeIfExists(const char* path)
{
    if (path && InternalFS.exists(path))
    {
        InternalFS.remove(path);
    }
}

bool openForOverwrite(const char* path,
                      File* out,
                      bool allow_format_recovery,
                      const char* log_tag)
{
    if (!out || !path)
    {
        return false;
    }

    *out = File(InternalFS);
    if (!ensureMounted(allow_format_recovery, log_tag))
    {
        return false;
    }

    *out = InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_WRITE);
    if (*out)
    {
        return true;
    }

    logPath(log_tag, "open for overwrite failed", path);
    if (!allow_format_recovery || !recoverByFormat(log_tag))
    {
        return false;
    }

    *out = InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_WRITE);
    if (!*out)
    {
        logPath(log_tag, "open for overwrite failed after recovery", path);
        return false;
    }
    return true;
}

bool rewindForOverwrite(File& file)
{
    return file && file.seek(0);
}

bool truncateAfterWrite(File& file, uint32_t final_size)
{
    return file && file.truncate(final_size);
}

uint32_t accumulateBytes(File dir)
{
    uint32_t total = 0;
    if (!dir)
    {
        return total;
    }

    dir.rewindDirectory();
    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
        {
            break;
        }

        if (entry.isDirectory())
        {
            total += accumulateBytes(entry);
        }
        else
        {
            total += entry.size();
        }
        entry.close();
    }

    return total;
}

} // namespace platform::nrf52::arduino_common::internal_fs

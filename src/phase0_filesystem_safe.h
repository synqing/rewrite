#ifndef PHASE0_FILESYSTEM_SAFE_H
#define PHASE0_FILESYSTEM_SAFE_H

/**
 * Phase 0: Filesystem Corruption Detection & Safe Operations
 *
 * Purpose: Replace "FUCK THE FILESYSTEM" with proper error handling
 *
 * Features:
 * - CRC32 checksums for config files
 * - Corruption detection
 * - Automatic recovery
 * - Safe mode fallback
 * - Transaction logging
 */

#include <LittleFS.h>
#include <FS.h>

namespace Phase0 {
namespace Filesystem {

// Magic numbers for file validation
constexpr uint32_t CONFIG_MAGIC = 0xC0FF1234;
constexpr uint32_t CONFIG_VERSION = 1;

// File header with integrity checking
struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t data_size;
    uint32_t crc32;
    uint32_t timestamp;
};

//=============================================================================
// CRC32 Calculation
//=============================================================================

class CRC32 {
private:
    static constexpr uint32_t POLYNOMIAL = 0xEDB88320;
    static uint32_t table[256];
    static bool table_initialized;

    static void init_table() {
        if (table_initialized) return;

        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 1) ? POLYNOMIAL : 0);
            }
            table[i] = crc;
        }
        table_initialized = true;
    }

public:
    static uint32_t calculate(const uint8_t* data, size_t length) {
        init_table();

        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; i++) {
            crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFF];
        }
        return ~crc;
    }

    static uint32_t calculate(const void* data, size_t length) {
        return calculate(static_cast<const uint8_t*>(data), length);
    }
};

// Static members
uint32_t CRC32::table[256] = {0};
bool CRC32::table_initialized = false;

//=============================================================================
// Safe File Operations
//=============================================================================

class SafeFile {
private:
    static constexpr size_t MAX_FILE_SIZE = 4096;  // 4KB max
    static constexpr const char* TEMP_SUFFIX = ".tmp";
    static constexpr const char* BACKUP_SUFFIX = ".bak";

    // Error tracking
    static uint32_t last_error_time;
    static uint32_t error_count;

public:
    enum class Status {
        OK,
        FILE_NOT_FOUND,
        CORRUPT_HEADER,
        CORRUPT_DATA,
        WRITE_FAILED,
        READ_FAILED,
        FS_NOT_MOUNTED,
        FILE_TOO_LARGE,
        INVALID_PARAMETER
    };

    struct Result {
        Status status;
        size_t bytes_processed;
        const char* error_message;

        bool ok() const { return status == Status::OK; }
        const char* statusString() const {
            switch (status) {
                case Status::OK: return "OK";
                case Status::FILE_NOT_FOUND: return "File not found";
                case Status::CORRUPT_HEADER: return "Corrupt header";
                case Status::CORRUPT_DATA: return "Corrupt data (CRC mismatch)";
                case Status::WRITE_FAILED: return "Write failed";
                case Status::READ_FAILED: return "Read failed";
                case Status::FS_NOT_MOUNTED: return "Filesystem not mounted";
                case Status::FILE_TOO_LARGE: return "File too large";
                case Status::INVALID_PARAMETER: return "Invalid parameter";
                default: return "Unknown error";
            }
        }
    };

    //=========================================================================
    // Safe Write with Atomic Commit
    //=========================================================================

    static Result write(const char* path, const void* data, size_t size) {
        if (!path || !data || size == 0 || size > MAX_FILE_SIZE) {
            return {Status::INVALID_PARAMETER, 0, "Invalid parameters"};
        }

        if (!LittleFS.begin()) {
            return {Status::FS_NOT_MOUNTED, 0, "Filesystem not mounted"};
        }

        // Create header
        FileHeader header;
        header.magic = CONFIG_MAGIC;
        header.version = CONFIG_VERSION;
        header.data_size = size;
        header.crc32 = CRC32::calculate(data, size);
        header.timestamp = millis();

        // Write to temporary file first
        String temp_path = String(path) + TEMP_SUFFIX;

        File temp_file = LittleFS.open(temp_path.c_str(), FILE_WRITE);
        if (!temp_file) {
            error_count++;
            last_error_time = millis();
            return {Status::WRITE_FAILED, 0, "Failed to open temp file"};
        }

        // Write header
        size_t written = temp_file.write((uint8_t*)&header, sizeof(header));
        if (written != sizeof(header)) {
            temp_file.close();
            LittleFS.remove(temp_path.c_str());
            error_count++;
            last_error_time = millis();
            return {Status::WRITE_FAILED, written, "Failed to write header"};
        }

        // Write data in chunks with watchdog feeding
        const uint8_t* data_ptr = static_cast<const uint8_t*>(data);
        size_t remaining = size;
        size_t total_written = 0;

        while (remaining > 0) {
            size_t chunk_size = (remaining > 64) ? 64 : remaining;
            written = temp_file.write(data_ptr + total_written, chunk_size);

            if (written != chunk_size) {
                temp_file.close();
                LittleFS.remove(temp_path.c_str());
                error_count++;
                last_error_time = millis();
                return {Status::WRITE_FAILED, total_written, "Failed to write data chunk"};
            }

            total_written += written;
            remaining -= written;

            // Feed watchdog every 64 bytes
            yield();
        }

        temp_file.close();

        // Backup existing file if it exists
        if (LittleFS.exists(path)) {
            String backup_path = String(path) + BACKUP_SUFFIX;
            LittleFS.remove(backup_path.c_str());  // Remove old backup
            LittleFS.rename(path, backup_path.c_str());  // Backup current
        }

        // Atomic rename: temp → actual
        if (!LittleFS.rename(temp_path.c_str(), path)) {
            // Failed to rename, try to restore backup
            String backup_path = String(path) + BACKUP_SUFFIX;
            if (LittleFS.exists(backup_path.c_str())) {
                LittleFS.rename(backup_path.c_str(), path);
            }
            error_count++;
            last_error_time = millis();
            return {Status::WRITE_FAILED, total_written, "Failed to commit file"};
        }

        return {Status::OK, size, nullptr};
    }

    //=========================================================================
    // Safe Read with Corruption Detection
    //=========================================================================

    static Result read(const char* path, void* buffer, size_t buffer_size, size_t* bytes_read) {
        if (!path || !buffer || buffer_size == 0) {
            return {Status::INVALID_PARAMETER, 0, "Invalid parameters"};
        }

        if (!LittleFS.begin()) {
            return {Status::FS_NOT_MOUNTED, 0, "Filesystem not mounted"};
        }

        if (!LittleFS.exists(path)) {
            // Try backup
            String backup_path = String(path) + BACKUP_SUFFIX;
            if (LittleFS.exists(backup_path.c_str())) {
                USBSerial.printf("Primary file missing, trying backup: %s\n", backup_path.c_str());
                path = backup_path.c_str();
            } else {
                return {Status::FILE_NOT_FOUND, 0, "File not found"};
            }
        }

        File file = LittleFS.open(path, FILE_READ);
        if (!file) {
            error_count++;
            last_error_time = millis();
            return {Status::READ_FAILED, 0, "Failed to open file"};
        }

        // Read header
        FileHeader header;
        size_t read_size = file.read((uint8_t*)&header, sizeof(header));
        if (read_size != sizeof(header)) {
            file.close();
            error_count++;
            last_error_time = millis();
            return {Status::CORRUPT_HEADER, read_size, "Failed to read header"};
        }

        // Validate magic number
        if (header.magic != CONFIG_MAGIC) {
            file.close();
            error_count++;
            last_error_time = millis();
            USBSerial.printf("Invalid magic: 0x%08X (expected 0x%08X)\n", header.magic, CONFIG_MAGIC);
            return {Status::CORRUPT_HEADER, 0, "Invalid magic number"};
        }

        // Validate data size
        if (header.data_size > buffer_size || header.data_size > MAX_FILE_SIZE) {
            file.close();
            error_count++;
            last_error_time = millis();
            return {Status::FILE_TOO_LARGE, 0, "File too large for buffer"};
        }

        // Read data
        read_size = file.read((uint8_t*)buffer, header.data_size);
        file.close();

        if (read_size != header.data_size) {
            error_count++;
            last_error_time = millis();
            return {Status::READ_FAILED, read_size, "Failed to read complete data"};
        }

        // Verify CRC
        uint32_t calculated_crc = CRC32::calculate(buffer, header.data_size);
        if (calculated_crc != header.crc32) {
            error_count++;
            last_error_time = millis();
            USBSerial.printf("CRC mismatch: calculated=0x%08X, stored=0x%08X\n",
                           calculated_crc, header.crc32);
            return {Status::CORRUPT_DATA, read_size, "CRC checksum mismatch"};
        }

        if (bytes_read) {
            *bytes_read = header.data_size;
        }

        return {Status::OK, header.data_size, nullptr};
    }

    //=========================================================================
    // Safe Filesystem Initialization
    //=========================================================================

    static Result initialize(bool format_on_failure = true) {
        // Try to mount
        if (LittleFS.begin()) {
            USBSerial.println("✅ LittleFS mounted successfully");
            return {Status::OK, 0, nullptr};
        }

        // Mount failed
        USBSerial.println("⚠️  LittleFS mount failed");

        if (format_on_failure) {
            USBSerial.println("Formatting filesystem...");

            if (!LittleFS.format()) {
                error_count++;
                last_error_time = millis();
                return {Status::WRITE_FAILED, 0, "Format failed"};
            }

            USBSerial.println("Filesystem formatted");

            // Try mount again
            if (LittleFS.begin()) {
                USBSerial.println("✅ LittleFS mounted after format");
                return {Status::OK, 0, nullptr};
            } else {
                error_count++;
                last_error_time = millis();
                return {Status::FS_NOT_MOUNTED, 0, "Mount failed after format"};
            }
        }

        error_count++;
        last_error_time = millis();
        return {Status::FS_NOT_MOUNTED, 0, "Mount failed"};
    }

    //=========================================================================
    // Diagnostics
    //=========================================================================

    static void printDiagnostics() {
        if (!LittleFS.begin()) {
            USBSerial.println("⚠️  Filesystem not mounted");
            return;
        }

        size_t total_bytes = LittleFS.totalBytes();
        size_t used_bytes = LittleFS.usedBytes();
        size_t free_bytes = total_bytes - used_bytes;

        USBSerial.println("\n╔═══════════════════════════════════════╗");
        USBSerial.println("║   FILESYSTEM DIAGNOSTICS              ║");
        USBSerial.println("╚═══════════════════════════════════════╝");
        USBSerial.printf("  Total:        %u bytes (%.2f KB)\n", total_bytes, total_bytes / 1024.0f);
        USBSerial.printf("  Used:         %u bytes (%.2f KB)\n", used_bytes, used_bytes / 1024.0f);
        USBSerial.printf("  Free:         %u bytes (%.2f KB)\n", free_bytes, free_bytes / 1024.0f);
        USBSerial.printf("  Usage:        %.1f%%\n", (float)used_bytes / total_bytes * 100.0f);
        USBSerial.printf("  Errors:       %u\n", error_count);
        USBSerial.printf("  Last Error:   %u ms ago\n",
                        error_count > 0 ? (millis() - last_error_time) : 0);
        USBSerial.println();
    }

    static uint32_t getErrorCount() { return error_count; }
    static uint32_t getLastErrorTime() { return last_error_time; }
};

// Static members
uint32_t SafeFile::last_error_time = 0;
uint32_t SafeFile::error_count = 0;

} // namespace Filesystem
} // namespace Phase0

#endif // PHASE0_FILESYSTEM_SAFE_H

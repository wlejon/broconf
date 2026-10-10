#pragma once

#include "broconf/types.h"
#include "broconf/value.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace broconf {

class KeyFile {
public:
    KeyFile() = default;

    void load_string(std::string_view content);
    [[nodiscard]] std::string save_string() const;

    bool load_file(const std::filesystem::path& path);
    bool save_file_atomic(const std::filesystem::path& path) const;

    [[nodiscard]] bool has_section(const std::string& section) const;
    [[nodiscard]] bool has_key(const std::string& section, const std::string& key) const;

    [[nodiscard]] std::optional<std::string> get_raw(const std::string& section, const std::string& key) const;
    [[nodiscard]] std::optional<Value> get_value(const std::string& section,
                                                 const std::string& key,
                                                 std::optional<Type> expected_type = std::nullopt) const;

    void set_raw(std::string section, std::string key, std::string value);
    void set_value(const std::string& section, const std::string& key, const Value& value);

    bool remove_key(const std::string& section, const std::string& key);
    bool remove_section(const std::string& section);
    void clear();

    [[nodiscard]] std::vector<std::string> sections() const;
    [[nodiscard]] std::vector<std::string> keys(const std::string& section) const;

    [[nodiscard]] const std::map<std::string, std::map<std::string, std::string>>& raw_data() const noexcept {
        return sections_;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

struct StorageChange {
    std::string section;
    std::string key;
    std::optional<Value> old_value;
    std::optional<Value> new_value;
};

// The user layer over read-only system layers. Writes are cheap: set() and
// reset() change memory and return; a writer thread persists them later,
// coalesced (everything changed within persist_delay of the first change is
// one write) and atomically (temp file + replace). The write merges the
// pending keys onto the file as it is on disk, so another process's keys are
// not overwritten with stale ones, and reload() lays the still-pending keys
// over what it reads, so a reload never reverts a value not yet written.
// flush() and close() (and the destructor) write what is pending now.
class LayeredStorage {
public:
    // (section, key) pairs a write just persisted.
    using PersistedHandler = std::function<void(const std::vector<std::pair<std::string, std::string>>&)>;

    explicit LayeredStorage(std::filesystem::path user_path,
                            std::vector<std::filesystem::path> system_paths = {},
                            std::chrono::milliseconds persist_delay = std::chrono::milliseconds(50));
    ~LayeredStorage();

    LayeredStorage(const LayeredStorage&) = delete;
    LayeredStorage& operator=(const LayeredStorage&) = delete;

    [[nodiscard]] const std::filesystem::path& user_path() const noexcept { return user_path_; }
    [[nodiscard]] const std::vector<std::filesystem::path>& system_paths() const noexcept { return system_paths_; }

    [[nodiscard]] std::optional<Value> get(const std::string& section,
                                           const std::string& key,
                                           std::optional<Type> expected_type = std::nullopt) const;

    [[nodiscard]] bool is_user_set(const std::string& section, const std::string& key) const;
    [[nodiscard]] bool has(const std::string& section, const std::string& key) const;
    [[nodiscard]] std::vector<std::string> list_keys(const std::string& section) const;
    [[nodiscard]] std::vector<std::string> list_sections() const;

    bool set(const std::string& section, const std::string& key, const Value& value);
    bool reset(const std::string& section, const std::string& key);

    std::vector<StorageChange> reload();

    // Writes everything set or reset before the call, and returns once it is
    // on disk: true, or false when the write failed (the changes stay
    // pending and are retried on the next change or flush).
    bool flush();
    // flush(), then stops the writer; later changes are written synchronously.
    void close();

    // Called on the writer thread after each successful write.
    void set_persisted_handler(PersistedHandler handler);

private:
    mutable std::shared_mutex mutex_;
    std::filesystem::path user_path_;
    std::vector<std::filesystem::path> system_paths_;

    KeyFile user_file_;
    std::vector<KeyFile> system_files_;

    // Persistence. Lock order: mutex_, then persist_mu_.
    struct Pending {
        std::optional<std::string> raw;  // nullopt: removed (reset)
        uint64_t seq{0};
    };
    using PendingMap = std::map<std::pair<std::string, std::string>, Pending>;

    std::mutex persist_mu_;
    std::condition_variable persist_cv_;
    PendingMap pending_;
    uint64_t seq_{0};            // last change queued
    uint64_t attempted_seq_{0};  // last change a write attempt covered
    uint64_t flush_target_{0};   // a flush() waits for this change to be written
    uint64_t write_epoch_{0};    // bumped when a write lands and its keys leave pending_
    bool last_write_ok_{true};
    bool stop_{false};
    bool closed_{false};
    std::thread writer_;
    std::chrono::milliseconds persist_delay_;
    PersistedHandler persisted_handler_;

    // Queues a change for the writer; false once closed (the caller writes it).
    bool queue_change_locked(const std::string& section, const std::string& key,
                             std::optional<std::string> raw);
    void writer_loop();
    bool write_pending(const PendingMap& snapshot);
    static void overlay(KeyFile& file, const PendingMap& pending);
    [[nodiscard]] std::vector<std::string> list_keys_locked(const std::string& section) const;
    [[nodiscard]] std::vector<std::string> list_sections_locked() const;
};

} // namespace broconf

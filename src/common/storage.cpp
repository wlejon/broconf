#include "broconf/storage.h"

#include <algorithm>
#include <set>

namespace broconf {

namespace {

using KeyRef = std::pair<std::string, std::string>;

// The effective raw value of every key: the user layer over the system layers.
// Points into the files, which must outlive the map.
std::map<KeyRef, const std::string*> effective_raw(const KeyFile& user, const std::vector<KeyFile>& system) {
    std::map<KeyRef, const std::string*> out;
    for (const auto& [sec, keys] : user.raw_data()) {
        for (const auto& [key, raw] : keys) out[{sec, key}] = &raw;
    }
    for (const auto& sys : system) {
        for (const auto& [sec, keys] : sys.raw_data()) {
            for (const auto& [key, raw] : keys) out.try_emplace({sec, key}, &raw);
        }
    }
    return out;
}

std::optional<Value> effective_value(const KeyFile& user, const std::vector<KeyFile>& system,
                                     const std::string& sec, const std::string& key) {
    if (user.has_key(sec, key)) return user.get_value(sec, key);
    for (const auto& sys : system) {
        if (sys.has_key(sec, key)) return sys.get_value(sec, key);
    }
    return std::nullopt;
}

} // namespace

LayeredStorage::LayeredStorage(std::filesystem::path user_path,
                               std::vector<std::filesystem::path> system_paths,
                               std::chrono::milliseconds persist_delay)
    : user_path_(std::move(user_path)),
      system_paths_(std::move(system_paths)),
      persist_delay_(persist_delay) {
    user_file_.load_file(user_path_);
    system_files_.reserve(system_paths_.size());
    for (const auto& path : system_paths_) {
        KeyFile kf;
        kf.load_file(path);
        system_files_.push_back(std::move(kf));
    }
}

LayeredStorage::~LayeredStorage() {
    close();
}

std::optional<Value> LayeredStorage::get(const std::string& section,
                                         const std::string& key,
                                         std::optional<Type> expected_type) const {
    std::shared_lock lock(mutex_);

    // 1. User overrides take precedence
    if (user_file_.has_key(section, key)) {
        auto val = user_file_.get_value(section, key, expected_type);
        if (val) return val;
    }

    // 2. System defaults
    for (const auto& sys_file : system_files_) {
        if (sys_file.has_key(section, key)) {
            auto val = sys_file.get_value(section, key, expected_type);
            if (val) return val;
        }
    }

    return std::nullopt;
}

bool LayeredStorage::is_user_set(const std::string& section, const std::string& key) const {
    std::shared_lock lock(mutex_);
    return user_file_.has_key(section, key);
}

bool LayeredStorage::has(const std::string& section, const std::string& key) const {
    std::shared_lock lock(mutex_);
    if (user_file_.has_key(section, key)) return true;
    for (const auto& sys : system_files_) {
        if (sys.has_key(section, key)) return true;
    }
    return false;
}

std::vector<std::string> LayeredStorage::list_keys(const std::string& section) const {
    std::shared_lock lock(mutex_);
    return list_keys_locked(section);
}

std::vector<std::string> LayeredStorage::list_keys_locked(const std::string& section) const {
    std::set<std::string> key_set;
    for (const auto& k : user_file_.keys(section)) {
        key_set.insert(k);
    }
    for (const auto& sys : system_files_) {
        for (const auto& k : sys.keys(section)) {
            key_set.insert(k);
        }
    }
    return std::vector<std::string>(key_set.begin(), key_set.end());
}

std::vector<std::string> LayeredStorage::list_sections() const {
    std::shared_lock lock(mutex_);
    return list_sections_locked();
}

std::vector<std::string> LayeredStorage::list_sections_locked() const {
    std::set<std::string> sec_set;
    for (const auto& s : user_file_.sections()) {
        sec_set.insert(s);
    }
    for (const auto& sys : system_files_) {
        for (const auto& s : sys.sections()) {
            sec_set.insert(s);
        }
    }
    return std::vector<std::string>(sec_set.begin(), sec_set.end());
}

bool LayeredStorage::set(const std::string& section, const std::string& key, const Value& value) {
    std::unique_lock lock(mutex_);
    user_file_.set_value(section, key, value);
    if (!queue_change_locked(section, key, user_file_.get_raw(section, key))) {
        lock.unlock();
        return flush();
    }
    return true;
}

bool LayeredStorage::reset(const std::string& section, const std::string& key) {
    std::unique_lock lock(mutex_);
    if (!user_file_.has_key(section, key)) {
        return true; // Already not set by user
    }
    user_file_.remove_key(section, key);
    if (!queue_change_locked(section, key, std::nullopt)) {
        lock.unlock();
        return flush();
    }
    return true;
}

bool LayeredStorage::queue_change_locked(const std::string& section, const std::string& key,
                                         std::optional<std::string> raw) {
    bool open;
    {
        std::lock_guard plock(persist_mu_);
        pending_[{section, key}] = Pending{std::move(raw), ++seq_};
        open = !closed_;
        if (open && !writer_.joinable()) {
            writer_ = std::thread(&LayeredStorage::writer_loop, this);
        }
    }
    persist_cv_.notify_all();
    return open;
}

void LayeredStorage::overlay(KeyFile& file, const PendingMap& pending) {
    for (const auto& [ref, p] : pending) {
        if (p.raw) {
            file.set_raw(ref.first, ref.second, *p.raw);
        } else {
            file.remove_key(ref.first, ref.second);
        }
    }
}

bool LayeredStorage::write_pending(const PendingMap& snapshot) {
    // Merge onto the file as it is now: keys another process wrote since we
    // last read it stay as it wrote them.
    KeyFile disk;
    std::error_code ec;
    bool exists = std::filesystem::exists(user_path_, ec);
    if (!disk.load_file(user_path_) && exists) {
        // There but unreadable: write what we hold instead of only the pending keys.
        std::shared_lock lock(mutex_);
        disk = user_file_;
    }
    overlay(disk, snapshot);
    return disk.save_file_atomic(user_path_);
}

void LayeredStorage::writer_loop() {
    std::unique_lock lk(persist_mu_);
    for (;;) {
        persist_cv_.wait(lk, [&] { return stop_ || seq_ > attempted_seq_; });
        if (seq_ <= attempted_seq_) {
            if (stop_) break;
            continue;
        }
        // Coalesce: everything changed within persist_delay_ of the first
        // change is one write, unless a flush or shutdown wants it now.
        if (!stop_ && flush_target_ <= attempted_seq_) {
            persist_cv_.wait_for(lk, persist_delay_, [&] { return stop_ || flush_target_ > attempted_seq_; });
        }
        PendingMap snapshot = pending_;
        uint64_t upto = seq_;
        lk.unlock();
        bool ok = write_pending(snapshot);
        lk.lock();

        attempted_seq_ = upto;
        last_write_ok_ = ok;
        std::vector<std::pair<std::string, std::string>> written;
        if (ok) {
            // Keys changed again since the snapshot stay pending for the next write.
            for (const auto& [ref, p] : snapshot) {
                auto it = pending_.find(ref);
                if (it != pending_.end() && it->second.seq <= upto) pending_.erase(it);
                written.push_back(ref);
            }
            ++write_epoch_;
        }
        persist_cv_.notify_all();
        if (ok && persisted_handler_ && !written.empty()) {
            auto handler = persisted_handler_;
            lk.unlock();
            handler(written);
            lk.lock();
        }
    }
}

bool LayeredStorage::flush() {
    std::unique_lock lk(persist_mu_);
    if (pending_.empty()) return true;
    if (seq_ <= attempted_seq_) {
        // A write failed and nothing changed since: try again.
        ++seq_;
    }
    uint64_t target = seq_;
    if (!writer_.joinable() || stop_) {
        // No writer (closed): write here.
        PendingMap snapshot = pending_;
        lk.unlock();
        bool ok = write_pending(snapshot);
        lk.lock();
        attempted_seq_ = std::max(attempted_seq_, target);
        last_write_ok_ = ok;
        if (ok) {
            for (const auto& [ref, p] : snapshot) {
                auto it = pending_.find(ref);
                if (it != pending_.end() && it->second.seq <= target) pending_.erase(it);
            }
            ++write_epoch_;
        }
        return ok;
    }
    flush_target_ = std::max(flush_target_, target);
    persist_cv_.notify_all();
    persist_cv_.wait(lk, [&] { return attempted_seq_ >= target; });
    return last_write_ok_;
}

void LayeredStorage::close() {
    std::thread writer;
    {
        std::lock_guard lk(persist_mu_);
        if (closed_) return;
        closed_ = true;
        stop_ = true;
        writer = std::move(writer_);
    }
    persist_cv_.notify_all();
    // The writer writes what is pending before it exits.
    if (writer.joinable()) writer.join();
    flush(); // anything a failed final write left
}

void LayeredStorage::set_persisted_handler(PersistedHandler handler) {
    std::lock_guard lk(persist_mu_);
    persisted_handler_ = std::move(handler);
}

std::vector<StorageChange> LayeredStorage::reload() {
    for (;;) {
        uint64_t epoch;
        {
            std::lock_guard plock(persist_mu_);
            epoch = write_epoch_;
        }

        // Read and parse the files without holding the lock readers need.
        KeyFile new_user;
        new_user.load_file(user_path_);
        std::vector<KeyFile> new_system;
        new_system.reserve(system_paths_.size());
        for (const auto& path : system_paths_) {
            KeyFile kf;
            kf.load_file(path);
            new_system.push_back(std::move(kf));
        }

        std::unique_lock lock(mutex_);
        {
            std::lock_guard plock(persist_mu_);
            // A write of ours landed while we read: what we read may predate
            // it, and its keys have left pending_, so read again.
            if (write_epoch_ != epoch) continue;
            // Changes not yet written stay as this process set them.
            overlay(new_user, pending_);
        }

        auto before = effective_raw(user_file_, system_files_);
        auto after = effective_raw(new_user, new_system);

        std::vector<StorageChange> changes;
        auto add = [&](const KeyRef& ref) {
            changes.push_back(StorageChange{
                ref.first, ref.second,
                effective_value(user_file_, system_files_, ref.first, ref.second),
                effective_value(new_user, new_system, ref.first, ref.second)});
        };
        for (const auto& [ref, raw] : before) {
            auto it = after.find(ref);
            if (it == after.end() || *it->second != *raw) add(ref);
        }
        for (const auto& [ref, raw] : after) {
            if (!before.count(ref)) add(ref);
        }

        user_file_ = std::move(new_user);
        system_files_ = std::move(new_system);
        return changes;
    }
}

} // namespace broconf

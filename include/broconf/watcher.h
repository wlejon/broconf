#pragma once

#include "broconf/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace broconf {

using ChangeCallback = std::function<void(const std::string& path, const std::string& key, const Value& new_value)>;
using WatcherToken = uint64_t;

class WatcherRegistry {
public:
    WatcherRegistry() = default;

    WatcherToken add_watch(const std::string& path, ChangeCallback callback);
    WatcherToken add_watch(const std::string& path, const std::string& key, ChangeCallback callback);
    bool remove_watch(WatcherToken token);

    void notify(const std::string& path, const std::string& key, const Value& new_value);
    void clear();

private:
    struct WatchEntry {
        WatcherToken token{0};
        std::string path;
        std::string key; // empty means all keys under path
        ChangeCallback callback;
    };

    mutable std::mutex mutex_;
    WatcherToken next_token_{1};
    std::unordered_map<WatcherToken, WatchEntry> watchers_;
};

using ExternalChangeHandler = std::function<void(const std::string& path, const std::string& key)>;

class INotifier {
public:
    virtual ~INotifier() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void notify_changed(const std::string& path, const std::string& key) = 0;
    virtual void set_external_change_handler(ExternalChangeHandler handler) = 0;
};

} // namespace broconf

#include "broconf/watcher.h"

#include <algorithm>

namespace broconf {

namespace {

std::string to_dot_notation(std::string_view p) {
    while (!p.empty() && p.front() == '/') p.remove_prefix(1);
    while (!p.empty() && p.back() == '/') p.remove_suffix(1);
    std::string s(p);
    std::replace(s.begin(), s.end(), '/', '.');
    return s;
}

bool paths_match(const std::string& pattern, const std::string& actual) {
    if (pattern.empty()) return true;
    if (pattern == actual) return true;
    return to_dot_notation(pattern) == to_dot_notation(actual);
}

} // namespace

WatcherToken WatcherRegistry::add_watch(const std::string& path, ChangeCallback callback) {
    return add_watch(path, "", std::move(callback));
}

WatcherToken WatcherRegistry::add_watch(const std::string& path, const std::string& key, ChangeCallback callback) {
    std::lock_guard lock(mutex_);
    WatcherToken token = next_token_++;
    watchers_[token] = WatchEntry{token, path, key, std::move(callback)};
    return token;
}

bool WatcherRegistry::remove_watch(WatcherToken token) {
    std::lock_guard lock(mutex_);
    return watchers_.erase(token) > 0;
}

void WatcherRegistry::notify(const std::string& path, const std::string& key, const Value& new_value) {
    std::vector<ChangeCallback> callbacks_to_invoke;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [token, entry] : watchers_) {
            if (paths_match(entry.path, path)) {
                if (entry.key.empty() || entry.key == key) {
                    callbacks_to_invoke.push_back(entry.callback);
                }
            }
        }
    }

    for (const auto& cb : callbacks_to_invoke) {
        if (cb) {
            cb(path, key, new_value);
        }
    }
}

void WatcherRegistry::clear() {
    std::lock_guard lock(mutex_);
    watchers_.clear();
}

} // namespace broconf

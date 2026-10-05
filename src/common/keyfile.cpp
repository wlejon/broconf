#include "broconf/storage.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace broconf {

namespace {

std::string trim_view(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

} // namespace

void KeyFile::load_string(std::string_view content) {
    sections_.clear();

    std::string current_section;
    std::string line;
    std::istringstream stream{std::string(content)};

    while (std::getline(stream, line)) {
        std::string trimmed = trim_view(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        // Section header
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            current_section = trim_view(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            sections_[current_section]; // ensure section entry exists
            continue;
        }

        // Key = Value
        size_t eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) {
            eq_pos = trimmed.find(':');
        }

        if (eq_pos != std::string::npos) {
            std::string key = trim_view(std::string_view(trimmed).substr(0, eq_pos));
            std::string val = trim_view(std::string_view(trimmed).substr(eq_pos + 1));
            if (!key.empty() && !current_section.empty()) {
                sections_[current_section][key] = val;
            }
        }
    }
}

std::string KeyFile::save_string() const {
    std::ostringstream ss;
    bool first_section = true;

    for (const auto& [sec_name, sec_keys] : sections_) {
        if (!first_section) {
            ss << "\n";
        }
        first_section = false;

        ss << "[" << sec_name << "]\n";
        for (const auto& [k, v] : sec_keys) {
            ss << k << " = " << v << "\n";
        }
    }
    return ss.str();
}

bool KeyFile::load_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        return false;
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    load_string(content);
    return true;
}

bool KeyFile::save_file_atomic(const std::filesystem::path& path) const {
    std::error_code ec;
    auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        if (!std::filesystem::create_directories(parent, ec) && ec) {
            return false;
        }
    }

    static std::mt19937_64 rng{static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count())};
    uint64_t rand_id = rng();

    std::filesystem::path tmp_path = parent / ("." + path.filename().string() +
                                              ".tmp." + std::to_string(rand_id));

    std::string text = save_string();

    FILE* f = std::fopen(tmp_path.string().c_str(), "wb");
    if (!f) {
        return false;
    }

    size_t written = std::fwrite(text.data(), 1, text.size(), f);
    if (written != text.size()) {
        std::fclose(f);
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    std::fflush(f);
#if defined(__unix__) || defined(__APPLE__)
    int fd = fileno(f);
    if (fd >= 0) {
        fsync(fd);
    }
#endif
    std::fclose(f);

    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    return true;
}

bool KeyFile::has_section(const std::string& section) const {
    return sections_.find(section) != sections_.end();
}

bool KeyFile::has_key(const std::string& section, const std::string& key) const {
    auto sec_it = sections_.find(section);
    if (sec_it == sections_.end()) return false;
    return sec_it->second.find(key) != sec_it->second.end();
}

std::optional<std::string> KeyFile::get_raw(const std::string& section, const std::string& key) const {
    auto sec_it = sections_.find(section);
    if (sec_it == sections_.end()) return std::nullopt;
    auto key_it = sec_it->second.find(key);
    if (key_it == sec_it->second.end()) return std::nullopt;
    return key_it->second;
}

std::optional<Value> KeyFile::get_value(const std::string& section,
                                        const std::string& key,
                                        std::optional<Type> expected_type) const {
    auto raw = get_raw(section, key);
    if (!raw) return std::nullopt;

    try {
        if (expected_type) {
            return Value::deserialize(*expected_type, *raw);
        } else {
            return Value::parse_inferred(*raw);
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void KeyFile::set_raw(std::string section, std::string key, std::string value) {
    sections_[std::move(section)][std::move(key)] = std::move(value);
}

void KeyFile::set_value(const std::string& section, const std::string& key, const Value& value) {
    set_raw(section, key, value.serialize());
}

bool KeyFile::remove_key(const std::string& section, const std::string& key) {
    auto sec_it = sections_.find(section);
    if (sec_it == sections_.end()) return false;
    bool erased = sec_it->second.erase(key) > 0;
    if (sec_it->second.empty()) {
        sections_.erase(sec_it);
    }
    return erased;
}

bool KeyFile::remove_section(const std::string& section) {
    return sections_.erase(section) > 0;
}

void KeyFile::clear() {
    sections_.clear();
}

std::vector<std::string> KeyFile::sections() const {
    std::vector<std::string> list;
    list.reserve(sections_.size());
    for (const auto& [sec, _] : sections_) {
        list.push_back(sec);
    }
    return list;
}

std::vector<std::string> KeyFile::keys(const std::string& section) const {
    std::vector<std::string> list;
    auto sec_it = sections_.find(section);
    if (sec_it != sections_.end()) {
        list.reserve(sec_it->second.size());
        for (const auto& [k, _] : sec_it->second) {
            list.push_back(k);
        }
    }
    return list;
}

} // namespace broconf

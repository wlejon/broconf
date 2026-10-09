# broconf

[![CI](https://github.com/wlejon/broconf/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/broconf/actions/workflows/ci.yml)

Desktop settings for a desktop environment built on the
[bro](https://github.com/wlejon/bro) runtime: a typed, schema'd, layered
store whose changes reach every process that has it open, in the spirit of
GSettings/dconf or KConfig. A standalone C++20 library: no dependency on bro
or bronze, no JS binding in the core library, its own CMake and ctest.

## Where it sits

Part of the **[bro](https://github.com/wlejon/bro)** desktop ecosystem (see the
[ecosystem architecture](https://github.com/wlejon/bro/blob/main/docs/ecosystem.md)).
Within the desktop stack, `broconf` provides the unified configuration store:
coordinating settings (accent colours, font sizes, interface themes, shell
layouts, key bindings) across applications, shell components, and background
daemons with cross-process synchronization.

## Model

A `Store` reads a key by looking, in order, at the user's settings file, the
system settings files, and the key's schema default. Writes go to the user
file only, replacing it atomically (temporary file, flush, rename), so a
reader in another process sees the old file or the new one, never half of
one. Schemas declare each key's type, default and constraints (numeric
range, enum values, a custom predicate); a write that breaks one throws
`ValidationError` and changes nothing.

Watchers fire for changes made through the store itself and for changes made
by other processes. Those arrive as a changed file (and on Linux also as a
D-Bus signal); the store reloads, diffs the effective values, and calls the
watchers of each key that actually changed, with its new effective value (the
schema default again after a reset).

```cpp
#include <broconf/broconf.h>

auto store = broconf::Store::create();            // the default paths below
auto iface = std::make_shared<broconf::Schema>("org.bro.desktop.interface");
iface->add_color("accent-color", broconf::Color(53, 132, 228))
     .add_int("font-size", 11, 8, 36)
     .add_enum("clock-format", "24h", {"12h", "24h"});
store->register_schema(iface);

store->watch("org.bro.desktop.interface", "accent-color",
             [](const std::string&, const std::string&, const broconf::Value& v) {
                 apply_accent(v.get_color());   // runs on the notifier thread for others' changes
             });

auto size = store->get_as<int64_t>("org.bro.desktop.interface", "font-size");
store->set("org.bro.desktop.interface", "accent-color", broconf::Value::make_color(255, 64, 128));
store->reset("org.bro.desktop.interface", "accent-color");   // back to the default
```

| Header | Contents |
| :--- | :--- |
| `types.h` | `Type`, `Color`, `Rect`, `EnumValue`; `ConfError`, `TypeError`, `ValidationError` |
| `value.h` | `Value` (bool, int64, double, string, string list, enum, color, rect, dictionary) and `Dictionary`; codecs |
| `schema.h` | `KeySchema`, `Schema` (builder), `SchemaRegistry` |
| `storage.h` | `KeyFile` (INI parser/writer, atomic save), `LayeredStorage` (user over system) |
| `watcher.h` | `WatcherRegistry`, `INotifier` (cross-process notification channels) |
| `store.h` | `Store`, `StoreOptions` |
| `broconf.h` | Master umbrella header |

Watcher callbacks for other processes' changes run on the notifier's thread;
callbacks for the store's own writes run on the writing thread.

## Platforms

| | Linux | Windows | macOS |
|---|---|---|---|
| **User file** | `$XDG_CONFIG_HOME/bro/settings.ini` (`~/.config/bro/...`) | `%APPDATA%\bro\settings.ini` | `~/Library/Preferences/bro/settings.ini` |
| **System files** | Each `$XDG_CONFIG_DIRS/bro/settings.ini` (fallback `/etc/xdg/bro/...`), then `/usr/share/bro/settings.ini` | `%PROGRAMDATA%\bro\settings.ini` | `/Library/Preferences/bro/settings.ini` |
| **Other processes' changes** | `org.bro.Config.Changed(s path, s key)` on session bus (sd-bus), plus inotify on user file directory | Directory watch (`FindFirstChangeNotificationW`) | kqueue `EVFILT_VNODE` on directory |

The file watch alone is sufficient for processes sharing the user file; the
D-Bus signal (Linux) also informs processes that keep their settings elsewhere
which key changed. A missing session bus disables the signal and leaves the
file watch operational. Every path can be overridden in `StoreOptions`, and either
notification channel can be disabled (`enable_dbus`, `enable_file_watcher`).

## Building

### Prerequisites

- **CMake 3.24+** and a **C++20** compiler (MSVC 2022+, GCC 12+, Clang 15+, Apple Clang).
- **Linux**: `libsystemd` (sd-bus >= 246) with pkg-config (`libsystemd-dev` on Debian/Ubuntu, `systemd-libs` on Arch).
  The Linux D-Bus test also uses `dbus-daemon` and `gdbus` when present.
- **Windows / macOS**: No external libraries required.

### Standalone build

```bash
# Linux / macOS
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

# Windows (MSVC)
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake options:
- `BROCONF_BUILD_TESTS`: Build tests (default `ON` when top-level, `OFF` when included via `add_subdirectory`).
- `BROCONF_COVERAGE`: Instrument the build for gcov coverage (GCC/Clang).
- `BROCONF_ENABLE_API`: Build the standalone Bronze JavaScript API (default `ON` when top-level). bronze (with brass) comes from `../bronze` beside the top-level project, else the head of its main branch, fetched at configure (`cmake/bro_deps.cmake`), so a plain `git clone` builds.

### Consuming broconf

Downstream projects consume the `broconf::broconf` CMake target. Ecosystem
consumers declare it with `bro_dependency()` (`cmake/bro_deps.cmake`): a target the
outer project already added wins, else a `../broconf` working tree beside the
top-level project, else the head of its main branch, fetched at configure
(`-DFETCHCONTENT_SOURCE_DIR_BROCONF=<path>` points at another tree):

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)
bro_dependency(broconf)

target_link_libraries(your_target PRIVATE broconf::broconf)
```

## Tests

Test assertions use `tests/check.h` (active in every configuration, no `assert()`).
A test that cannot run in the current environment exits code 77 with the reason
printed, and ctest reports it as skipped. Everything runs in isolated temporary
directories and leaves nothing behind.

| Test | Platform | Target / Environment | Oracle |
|---|---|---|---|
| `test_types`, `test_schema` | everywhere | in-process | Value codecs (exact double round trip, escapes, nan/inf), schema validation |
| `test_keyfile` | everywhere | temporary directory | INI parser/writer and atomic saves (temp file, flush, rename), read back from disk |
| `test_storage_layered` | everywhere | temporary directory | User-over-system layering, reset, external rewrites seen by reload, concurrent readers and writers |
| `test_store_api` | everywhere | temporary directory | End-to-end Store API, persistence across instances |
| `test_file_watcher` | everywhere | second helper process | A second process (the test binary re-executed) sets and resets a key in the shared file; watcher reports both (inotify / directory watch / kqueue) |
| `test_dbus_sync` | Linux | private `dbus-daemon` + `gdbus` | A second process's write, a `gdbus emit` from an independent client, and verification that the store's own signal is not echoed |
| `broconf_test_api` | Linux / Windows (when API enabled) | Bronze runtime | Bronze JavaScript bindings (`broconf_api`) and garbage collection stress testing |

### Test fixtures & CI skipping

- **File watcher tests**: Tests run an external process helper to modify the INI file
  and verify asynchronous notification delivery across process boundaries.
- **Private D-Bus daemon**: `test_dbus_sync` starts an isolated `dbus-daemon` session
  instance so that no test signals or names touch the user's desktop session bus.
  If `dbus-daemon` or `gdbus` is missing, the test exits 77 (skipped).

## License

MIT, see [LICENSE](LICENSE).

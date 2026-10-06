# broconf

[![CI](https://github.com/wlejon/broconf/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/broconf/actions/workflows/ci.yml)

Desktop settings for a desktop environment built on the
[bro](https://github.com/wlejon/bro) runtime: a typed, schema'd, layered
store whose changes reach every process that has it open, in the spirit of
GSettings/dconf or KConfig. A standalone C++20 library: no dependency on bro
or bronze, no JS binding, its own CMake and ctest.

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

```
include/broconf/
  types.h     Type, Color, Rect, EnumValue; ConfError, TypeError, ValidationError
  value.h     Value (bool, int64, double, string, string list, enum, color, rect,
              dictionary) and Dictionary; serialize / deserialize / parse_inferred
  schema.h    KeySchema, Schema (builder), SchemaRegistry
  storage.h   KeyFile (INI parser/writer, atomic save), LayeredStorage (user over system)
  watcher.h   WatcherRegistry, INotifier (the cross-process channel)
  store.h     Store, StoreOptions
  broconf.h   umbrella header
```

Watcher callbacks for other processes' changes run on the notifier's thread;
callbacks for the store's own writes run on the writing thread.

## Platforms

| | Linux | Windows | macOS |
|---|---|---|---|
| User file | `$XDG_CONFIG_HOME/bro/settings.ini` (`~/.config/bro/...`) | `%APPDATA%\bro\settings.ini` | `~/Library/Preferences/bro/settings.ini` |
| System files | each `$XDG_CONFIG_DIRS/bro/settings.ini` (else `/etc/xdg/bro/...`), then `/usr/share/bro/settings.ini` | `%PROGRAMDATA%\bro\settings.ini` | `/Library/Preferences/bro/settings.ini` |
| Other processes' changes | `org.bro.Config.Changed(s path, s key)` on the session bus (sd-bus), and inotify on the user file's directory | directory watch (`FindFirstChangeNotificationW`) | kqueue `EVFILT_VNODE` on the directory |

The file watch alone is enough for processes that share the user file; the
D-Bus signal (Linux) also tells processes that keep their settings elsewhere
which key changed. A missing session bus disables the signal and leaves the
file watch. Every path can be overridden in `StoreOptions`, and either channel
turned off (`enable_dbus`, `enable_file_watcher`).

## Building

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release        # Windows: cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Requirements: CMake 3.24+, a C++20 compiler (MSVC 2022, GCC 12+, Clang 15+,
Apple Clang), and on Linux `libsystemd` (sd-bus, >= 246) with pkg-config.
The Linux D-Bus test also uses `dbus-daemon` and `gdbus` when present. There
are no sibling repos to fetch.

Add it to another CMake project with `add_subdirectory(broconf)` and link
`broconf::broconf`.

## Tests

Real ctests: no `assert()`, failures count in every configuration, exit 77 is
a skip with the reason printed. Everything runs in a temporary directory and
leaves nothing behind.

| Test | What it checks against |
|---|---|
| test_types, test_schema | value codecs (exact double round trip, escapes, nan/inf), schema validation |
| test_keyfile | the INI format and atomic saves, read back from disk |
| test_storage_layered | user-over-system layering, reset, external rewrites seen by reload, concurrent readers and writers |
| test_store_api | the Store end to end, persistence across instances |
| test_file_watcher | a second process (the test binary run again) sets and resets a key in the shared file; the watcher must report both (inotify / directory watch / kqueue) |
| test_dbus_sync (Linux) | on a private `dbus-daemon`: a second process's write, a `gdbus emit` from an independent client, and no echo of the store's own signal |

## License

MIT, see [LICENSE](LICENSE).

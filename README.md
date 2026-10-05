# broconf

Standalone C++20 desktop configuration and settings library.

`broconf` provides desktop-wide settings: a typed, schema'd store that notifies across processes (similar to GSettings/dconf or KConfig), feeding desktop environments, shells, and portal settings (`org.freedesktop.portal.Settings`).

## Overview & Architecture

- **Typed & Schema-Driven**: First-class support for `bool`, `int64_t`, `double`, `std::string`, `std::vector<std::string>`, `enum`, `color` (RGBA), `rect` (x, y, width, height), and `dictionary` (structured key-value mapping).
- **Validation Rules**: Min/max range enforcement for numeric types, allowed values for enums, and custom predicate validators.
- **Layered Overrides**:
  1. In-memory user modifications
  2. User persistent configuration (`~/.config/bro/settings.ini` or `$XDG_CONFIG_HOME`)
  3. System defaults (`/etc/xdg/bro/settings.ini` or `$XDG_CONFIG_DIRS`, `/usr/share/bro/settings.ini`)
  4. Schema defaults
- **Atomic Persistent Storage**: Hierarchical INI/keyfile storage with atomic replacement via temporary files + `fsync` + atomic `rename`.
- **Cross-Process Synchronization**:
  - **Linux**: High-speed D-Bus change notification signals (`org.bro.Config.Changed(path, key)`) over the user session bus via `sd-bus` (`libsystemd`), with automatic inotify directory monitoring fallback.
  - **Windows**: Directory change notifications via `FindFirstChangeNotificationW`.
  - **macOS**: Directory monitoring via `kqueue` VNODE notifications.
- **Reactive Watchers**: Thread-safe change listeners (`watch(path, callback)`, `watch(path, key, callback)`).
- **Strict House Rules**: All source files strictly under 1,000 LOC, modern C++20, RAII wrappers, zero mock objects.

## Directory Structure

```
broconf/
├── CMakeLists.txt
├── README.md
├── include/
│   └── broconf/
│       ├── broconf.h       # Umbrella header & version definitions
│       ├── types.h         # Type enum, Color, Rect, EnumValue, error hierarchy
│       ├── value.h         # Value variant and Dictionary class
│       ├── schema.h        # KeySchema, Schema builder, SchemaRegistry
│       ├── storage.h       # KeyFile parser/serializer & LayeredStorage
│       ├── watcher.h       # WatcherRegistry & INotifier interface
│       └── store.h         # Store facade & StoreOptions
├── src/
│   ├── common/
│   │   ├── keyfile.cpp     # INI/keyfile parser and atomic file saver
│   │   ├── schema.cpp      # Validation rules and SchemaRegistry
│   │   ├── storage.cpp     # Layered storage fallback & reload diffs
│   │   ├── store.cpp       # Main Store orchestration & default paths
│   │   ├── types.cpp       # Color and Rect string parsers & formatters
│   │   ├── value.cpp       # Value variant, Dictionary, and JSON/INI codecs
│   │   └── watcher.cpp     # Thread-safe in-process watcher registry
│   ├── linux/
│   │   ├── linux_notifier.h
│   │   └── linux_notifier.cpp  # D-Bus (sd-bus) signals + inotify worker thread
│   ├── win/
│   │   ├── win_notifier.h
│   │   └── win_notifier.cpp    # Windows directory monitoring bridge
│   └── mac/
│       ├── mac_notifier.h
│       └── mac_notifier.cpp    # macOS kqueue directory monitoring bridge
└── tests/
    ├── CMakeLists.txt
    ├── test_types.cpp          # 9 typed values, parsers, serializers
    ├── test_schema.cpp         # Schema builder, validation rules, registry
    ├── test_keyfile.cpp        # INI parser, serializer, atomic persistence
    ├── test_storage_layered.cpp# Real disk layering, fallback, concurrent RW
    ├── test_inotify_watcher.cpp# Real inotify file monitoring with atomic rename
    ├── test_dbus_sync.cpp      # Real cross-process D-Bus signals over session bus
    └── test_store_api.cpp      # End-to-end Store API tests
```

## Backend Implementation Matrix

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| **Storage Engine** | Hierarchical keyfile (`settings.ini`) | Hierarchical keyfile (`settings.ini`) | Hierarchical keyfile (`settings.ini`) |
| **Atomic Writes** | `.tmp` file + `fsync` + `rename` | `.tmp` file + `MoveFileExW` / `rename` | `.tmp` file + `fsync` + `rename` |
| **User Path** | `$XDG_CONFIG_HOME/bro/settings.ini` | `%APPDATA%\bro\settings.ini` | `~/Library/Preferences/bro/settings.ini` |
| **System Paths** | `$XDG_CONFIG_DIRS`, `/usr/share/bro/` | `%PROGRAMDATA%\bro\settings.ini` | `/Library/Preferences/bro/settings.ini` |
| **IPC Notifications** | D-Bus signal (`org.bro.Config.Changed`) | Platform bridge notifier | Platform bridge notifier |
| **File Monitoring** | Linux inotify (`IN_MOVED_TO`, `IN_CLOSE_WRITE`) | `FindFirstChangeNotificationW` | BSD `kqueue` (`EVFILT_VNODE`) |

## Quick Start Example

```cpp
#include <broconf/broconf.h>
#include <iostream>

using namespace broconf;

int main() {
    // 1. Initialize store
    auto store = Store::default_store();

    // 2. Define schema
    auto iface = std::make_shared<Schema>("org.bro.desktop.interface");
    iface->add_color("accent-color", Color(53, 132, 228))
         .add_int("font-size", 11, 8, 36)
         .add_bool("dark-mode", false)
         .add_enum("clock-format", "24h", {"12h", "24h"});
    store->register_schema(iface);

    // 3. Watch for changes
    store->watch("org.bro.desktop.interface", "accent-color",
                 [](const std::string& path, const std::string& key, const Value& val) {
                     std::cout << "Accent color changed to: "
                               << val.get_color().to_hex_string() << "\n";
                 });

    // 4. Read settings (falls back to schema default if not set)
    Color accent = store->get_as<Color>("org.bro.desktop.interface", "accent-color");
    std::cout << "Current accent: " << accent.to_hex_string() << "\n";

    // 5. Update settings (persists atomically and notifies across processes)
    store->set("org.bro.desktop.interface", "accent-color", Value(Color(255, 64, 128)));

    // 6. Reset setting to system/schema default
    store->reset("org.bro.desktop.interface", "accent-color");

    return 0;
}
```

## Building & Testing

```bash
# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Compile (bounded concurrency to respect system memory limits)
cmake --build build -j 2

# Run tests
ctest --test-dir build --output-on-failure
```

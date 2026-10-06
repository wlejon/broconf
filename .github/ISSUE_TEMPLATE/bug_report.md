---
name: Bug report
about: broconf loses or misreads a setting, misses another process's change, rejects a valid value, hangs, or crashes
labels: bug
---

**What you called** (the smallest program or call sequence you can manage):

```cpp
```

**The settings files involved** (the user file and any system files, as they
are on disk; trim unrelated sections):

```ini
```

**What broconf reports or does instead** (the value, the exception and its
message, the watcher calls you saw or did not see; paste it):

```
```

**Does it reproduce in the tests?** Which `ctest` test fails, if any:

**Environment:**
- OS and version:
- Linux: is there a session bus (`echo $DBUS_SESSION_BUS_ADDRESS`)? Which filesystem holds the settings directory (local, NFS, a sync folder)?
- broconf commit:

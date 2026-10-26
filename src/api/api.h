#pragma once

#include <functional>
#include <memory>
#include <string>

namespace broconf {
class Store;
}

namespace broconf::api {

/// Mounts `bro.conf` in the current Bronze realm.
void installConf();

/// Pumps async change events and watcher callbacks on the JS thread.
void tickConfAsync();

/// Cleans up active watchers and flushes pending writes to the settings file.
void shutdownConfAsync();

/// Sets the store used by the API (defaults to Store::default_store()).
void setStore(std::shared_ptr<broconf::Store> store);

/// Gets the store currently used by the API.
std::shared_ptr<broconf::Store> getStore();

// ---- Access -----------------------------------------------------------------------
//
// The store is the user's desktop settings, so changing it is the host's
// decision: set and reset ask the host's access gate, bronze::embed::checkAccess
// (embed/embed_access.h), for AccessKind::DesktopWrite with "set <path>.<key> in
// the desktop settings" / "reset <path>.<key> in the desktop settings", on the JS
// thread before anything is parsed or stored. A refusal is
// bronze::embed::newAccessError (code EACCES; PermissionRevoked for a grant taken
// back): set's promise rejected with it, reset throwing it. Reading, watching
// and registering a schema ask nothing. With no gate installed nothing changes.

} // namespace broconf::api

using broconf::api::installConf;
using broconf::api::tickConfAsync;
using broconf::api::shutdownConfAsync;
using broconf::api::setStore;
using broconf::api::getStore;

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

/// Cleans up active watchers.
void shutdownConfAsync();

/// Sets the store used by the API (defaults to Store::default_store()).
void setStore(std::shared_ptr<broconf::Store> store);

/// Gets the store currently used by the API.
std::shared_ptr<broconf::Store> getStore();

} // namespace broconf::api

using broconf::api::installConf;
using broconf::api::tickConfAsync;
using broconf::api::shutdownConfAsync;
using broconf::api::setStore;
using broconf::api::getStore;

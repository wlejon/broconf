#pragma once

#include "api.h"
#include "embed/embed.h"
#include <memory>
#include <string>

namespace broconf {
class Store;
}

namespace broconf::api {

namespace ev = bronze::embed;
using Value = bronze::Value;

std::shared_ptr<broconf::Store> activeStore();

void installConfOnto(Value confObj);
void drainWatcherEvents();
void clearWatchers();

Value makeError(const std::string& msg);

} // namespace broconf::api

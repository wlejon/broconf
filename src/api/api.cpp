#include "api.h"
#include "host_conf_internal.h"
#include "broconf/store.h"

#include <mutex>

namespace broconf::api {

namespace {

std::mutex g_store_mu;
std::shared_ptr<broconf::Store> g_custom_store;

} // namespace

std::shared_ptr<broconf::Store> activeStore() {
    std::lock_guard lock(g_store_mu);
    if (g_custom_store) {
        return g_custom_store;
    }
    return broconf::Store::default_store();
}

void setStore(std::shared_ptr<broconf::Store> store) {
    std::lock_guard lock(g_store_mu);
    g_custom_store = std::move(store);
}

std::shared_ptr<broconf::Store> getStore() {
    return activeStore();
}

Value makeError(const std::string& msg) {
    ev::Persistent text(ev::fromUtf8(msg));
    auto ctor = ev::globalValue("Error");
    if (ctor.found && ev::isFunction(ctor.value)) {
        ev::Persistent c(ctor.value);
        const Value arg = text.get();
        auto r = ev::construct(c.get(), std::span<const Value>(&arg, 1));
        if (!r.thrown) return r.value;
    }
    return text.get();
}

Value ensureBroConf() {
    ev::Persistent globalThisVal;
    auto gt = ev::globalValue("globalThis");
    if (gt.found && ev::isObject(gt.value)) {
        globalThisVal.set(gt.value);
    }

    ev::Persistent broP;
    auto bro = ev::globalValue("bro");
    if (bro.found && ev::isObject(bro.value)) broP.set(bro.value);
    if (!ev::isObject(broP.get()) && ev::isObject(globalThisVal.get())) {
        Value candidate = ev::getProperty(globalThisVal.get(), "bro");
        if (ev::isObject(candidate)) broP.set(candidate);
    }
    if (!ev::isObject(broP.get())) {
        broP.set(ev::createObject());
        ev::registerGlobal("bro", broP.get());
        if (ev::isObject(globalThisVal.get())) {
            globalThisVal.set(ev::setProperty(globalThisVal.get(), "bro", broP.get()));
        }
    }

    ev::Persistent confP(ev::getProperty(broP.get(), "conf"));
    if (!ev::isObject(confP.get())) {
        confP.set(ev::createObject());
        broP.set(ev::setProperty(broP.get(), "conf", confP.get()));
    }
    return confP.get();
}

void installConf() {
    ev::Persistent confObj(ensureBroConf());
    installConfOnto(confObj.get());
}

void tickConfAsync() {
    drainWatcherEvents();
}

void shutdownConfAsync() {
    clearWatchers();
}

} // namespace broconf::api

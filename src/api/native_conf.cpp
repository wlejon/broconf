#include "host_conf_internal.h"
#include "object_builder.h"
#include "broconf/store.h"
#include "broconf/schema.h"
#include "embed/embed_access.h"

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace broconf::api {

namespace {

struct QueuedChange {
    uint64_t apiToken{0};
    std::string path;
    std::string key;
    broconf::Value newValue;
};

struct ActiveWatcher {
    uint64_t apiToken{0};
    broconf::WatcherToken nativeToken{0};
    std::string path;
    std::string key;
    std::shared_ptr<ev::Persistent> callback;
    std::unordered_map<std::string, broconf::Value> previousValues;
};

std::mutex g_watcher_mu;
uint64_t g_next_api_token = 1;
std::unordered_map<uint64_t, ActiveWatcher> g_watchers;
std::vector<QueuedChange> g_change_queue;

std::vector<std::string> getObjectKeys(Value objVal) {
    std::vector<std::string> keys;
    if (!ev::isObject(objVal)) return keys;
    ev::Persistent obj(objVal);
    auto gObject = ev::globalValue("Object");
    if (!gObject.found || !ev::isObject(gObject.value)) return keys;
    ev::Persistent objCtor(gObject.value);
    ev::Persistent keysFn(ev::getProperty(objCtor.get(), "keys"));
    if (!ev::isFunction(keysFn.get())) return keys;
    Value arg = obj.get();
    auto kres = ev::call(keysFn.get(), objCtor.get(), std::span<const Value>(&arg, 1));
    if (kres.thrown || !ev::isObject(kres.value)) return keys;
    ev::Persistent keysArr(kres.value);
    Value lenVal = ev::getProperty(keysArr.get(), "length");
    uint32_t len = ev::isNumber(lenVal) ? static_cast<uint32_t>(ev::toDouble(lenVal)) : 0;
    for (uint32_t i = 0; i < len; ++i) {
        keys.push_back(ev::toUtf8(ev::getElement(keysArr.get(), i)));
    }
    return keys;
}

bool isArrayValue(Value v) {
    if (!ev::isObject(v)) return false;
    auto g = ev::globalValue("Array");
    if (g.found && ev::isObject(g.value)) {
        ev::Persistent arrCtor(g.value);
        ev::Persistent isArrFn(ev::getProperty(arrCtor.get(), "isArray"));
        if (ev::isFunction(isArrFn.get())) {
            Value arg = v;
            auto r = ev::call(isArrFn.get(), arrCtor.get(), std::span<const Value>(&arg, 1));
            if (!r.thrown && ev::isBool(r.value)) return ev::toBool(r.value);
        }
    }
    return false;
}

Value confValueToJs(const broconf::Value& val) {
    if (!val.is_valid()) return ev::undefined();

    if (val.is_bool()) {
        return ev::fromBool(val.get_bool());
    }
    if (val.is_int()) {
        return ev::fromDouble(static_cast<double>(val.get_int()));
    }
    if (val.is_double()) {
        return ev::fromDouble(val.get_double());
    }
    if (val.is_string()) {
        return ev::fromUtf8(val.get_string());
    }
    if (val.is_string_list()) {
        const auto& list = val.get_string_list();
        ev::Persistent arr(ev::makeArray());
        for (uint32_t i = 0; i < list.size(); ++i) {
            ev::Persistent s(ev::fromUtf8(list[i]));
            arr.set(ev::setElement(arr.get(), i, s.get()));
        }
        return arr.get();
    }
    if (val.is_enum()) {
        return ev::fromUtf8(val.get_enum().name);
    }
    if (val.is_color()) {
        Color c = val.get_color();
        ObjectBuilder b;
        b.set("r", static_cast<double>(c.r));
        b.set("g", static_cast<double>(c.g));
        b.set("b", static_cast<double>(c.b));
        b.set("a", static_cast<double>(c.a));
        return b.build();
    }
    if (val.is_rect()) {
        Rect r = val.get_rect();
        ObjectBuilder b;
        b.set("x", static_cast<double>(r.x));
        b.set("y", static_cast<double>(r.y));
        b.set("width", static_cast<double>(r.width));
        b.set("height", static_cast<double>(r.height));
        return b.build();
    }
    if (val.is_dictionary()) {
        const auto& dict = val.get_dictionary();
        ObjectBuilder b;
        for (const auto& [k, v] : dict.entries()) {
            ev::Persistent entryVal(confValueToJs(v));
            b.set(k, entryVal.get());
        }
        return b.build();
    }
    return ev::undefined();
}

broconf::Value jsToConfValue(Value vIn, std::optional<Type> expected_type = std::nullopt) {
    ev::Persistent vRoot(vIn);
    if (expected_type.has_value()) {
        switch (*expected_type) {
            case Type::Bool: {
                if (ev::isBool(vRoot.get())) return broconf::Value(ev::toBool(vRoot.get()));
                if (ev::isNumber(vRoot.get())) return broconf::Value(ev::toDouble(vRoot.get()) != 0.0);
                throw ValidationError("Expected boolean value");
            }
            case Type::Int64: {
                if (!ev::isNumber(vRoot.get())) throw ValidationError("Expected integer number");
                double d = ev::toDouble(vRoot.get());
                return broconf::Value(static_cast<int64_t>(d));
            }
            case Type::Double: {
                if (!ev::isNumber(vRoot.get())) throw ValidationError("Expected number");
                return broconf::Value(ev::toDouble(vRoot.get()));
            }
            case Type::String: {
                if (!ev::isString(vRoot.get())) throw ValidationError("Expected string");
                return broconf::Value(ev::toUtf8(vRoot.get()));
            }
            case Type::StringList: {
                if (!isArrayValue(vRoot.get())) throw ValidationError("Expected string array");
                std::vector<std::string> list;
                Value lenVal = ev::getProperty(vRoot.get(), "length");
                uint32_t len = ev::isNumber(lenVal) ? static_cast<uint32_t>(ev::toDouble(lenVal)) : 0;
                for (uint32_t i = 0; i < len; ++i) {
                    list.push_back(ev::toUtf8(ev::getElement(vRoot.get(), i)));
                }
                return broconf::Value(std::move(list));
            }
            case Type::Enum: {
                if (ev::isString(vRoot.get())) {
                    return broconf::Value::make_enum(ev::toUtf8(vRoot.get()));
                }
                if (ev::isObject(vRoot.get())) {
                    ev::Persistent nameVal(ev::getProperty(vRoot.get(), "name"));
                    if (ev::isString(nameVal.get())) {
                        Value valVal = ev::getProperty(vRoot.get(), "value");
                        int64_t numVal = ev::isNumber(valVal) ? static_cast<int64_t>(ev::toDouble(valVal)) : 0;
                        return broconf::Value::make_enum(ev::toUtf8(nameVal.get()), numVal);
                    }
                }
                throw ValidationError("Expected enum string or object with name");
            }
            case Type::Color: {
                if (ev::isString(vRoot.get())) {
                    auto c = Color::from_hex_string(ev::toUtf8(vRoot.get()));
                    if (c) return broconf::Value(*c);
                    throw ValidationError("Invalid color hex string");
                }
                if (ev::isObject(vRoot.get())) {
                    uint8_t r = static_cast<uint8_t>(ev::toDouble(ev::getProperty(vRoot.get(), "r")));
                    uint8_t g = static_cast<uint8_t>(ev::toDouble(ev::getProperty(vRoot.get(), "g")));
                    uint8_t b = static_cast<uint8_t>(ev::toDouble(ev::getProperty(vRoot.get(), "b")));
                    Value aVal = ev::getProperty(vRoot.get(), "a");
                    uint8_t a = ev::isUndefined(aVal) ? 255 : static_cast<uint8_t>(ev::toDouble(aVal));
                    return broconf::Value(Color(r, g, b, a));
                }
                throw ValidationError("Expected color object or hex string");
            }
            case Type::Rect: {
                if (ev::isObject(vRoot.get())) {
                    int32_t x = static_cast<int32_t>(ev::toDouble(ev::getProperty(vRoot.get(), "x")));
                    int32_t y = static_cast<int32_t>(ev::toDouble(ev::getProperty(vRoot.get(), "y")));
                    int32_t w = static_cast<int32_t>(ev::toDouble(ev::getProperty(vRoot.get(), "width")));
                    int32_t h = static_cast<int32_t>(ev::toDouble(ev::getProperty(vRoot.get(), "height")));
                    return broconf::Value(Rect(x, y, w, h));
                }
                throw ValidationError("Expected rect object with x, y, width, height");
            }
            case Type::Dictionary: {
                if (!ev::isObject(vRoot.get())) throw ValidationError("Expected dictionary object");
                std::map<std::string, broconf::Value> map;
                auto keys = getObjectKeys(vRoot.get());
                for (const auto& k : keys) {
                    ev::Persistent item(ev::getProperty(vRoot.get(), k));
                    map[k] = jsToConfValue(item.get(), std::nullopt);
                }
                return broconf::Value(Dictionary(std::move(map)));
            }
        }
    }

    if (ev::isBool(vRoot.get())) {
        return broconf::Value(ev::toBool(vRoot.get()));
    }
    if (ev::isNumber(vRoot.get())) {
        double d = ev::toDouble(vRoot.get());
        if (std::floor(d) == d && d >= -9223372036854775808.0 && d <= 9223372036854775807.0) {
            return broconf::Value(static_cast<int64_t>(d));
        }
        return broconf::Value(d);
    }
    if (ev::isString(vRoot.get())) {
        return broconf::Value(ev::toUtf8(vRoot.get()));
    }
    if (isArrayValue(vRoot.get())) {
        std::vector<std::string> list;
        Value lenVal = ev::getProperty(vRoot.get(), "length");
        uint32_t len = ev::isNumber(lenVal) ? static_cast<uint32_t>(ev::toDouble(lenVal)) : 0;
        for (uint32_t i = 0; i < len; ++i) {
            list.push_back(ev::toUtf8(ev::getElement(vRoot.get(), i)));
        }
        return broconf::Value(std::move(list));
    }
    if (ev::isObject(vRoot.get())) {
        ev::Persistent rVal(ev::getProperty(vRoot.get(), "r"));
        ev::Persistent gVal(ev::getProperty(vRoot.get(), "g"));
        ev::Persistent bVal(ev::getProperty(vRoot.get(), "b"));
        if (ev::isNumber(rVal.get()) && ev::isNumber(gVal.get()) && ev::isNumber(bVal.get())) {
            uint8_t r = static_cast<uint8_t>(ev::toDouble(rVal.get()));
            uint8_t g = static_cast<uint8_t>(ev::toDouble(gVal.get()));
            uint8_t b = static_cast<uint8_t>(ev::toDouble(bVal.get()));
            Value aVal = ev::getProperty(vRoot.get(), "a");
            uint8_t a = ev::isUndefined(aVal) ? 255 : static_cast<uint8_t>(ev::toDouble(aVal));
            return broconf::Value(Color(r, g, b, a));
        }
        ev::Persistent xVal(ev::getProperty(vRoot.get(), "x"));
        ev::Persistent yVal(ev::getProperty(vRoot.get(), "y"));
        ev::Persistent wVal(ev::getProperty(vRoot.get(), "width"));
        ev::Persistent hVal(ev::getProperty(vRoot.get(), "height"));
        if (ev::isNumber(xVal.get()) && ev::isNumber(yVal.get()) && ev::isNumber(wVal.get()) && ev::isNumber(hVal.get())) {
            return broconf::Value(Rect(
                static_cast<int32_t>(ev::toDouble(xVal.get())),
                static_cast<int32_t>(ev::toDouble(yVal.get())),
                static_cast<int32_t>(ev::toDouble(wVal.get())),
                static_cast<int32_t>(ev::toDouble(hVal.get()))
            ));
        }
        std::map<std::string, broconf::Value> map;
        auto keys = getObjectKeys(vRoot.get());
        for (const auto& k : keys) {
            ev::Persistent item(ev::getProperty(vRoot.get(), k));
            map[k] = jsToConfValue(item.get(), std::nullopt);
        }
        return broconf::Value(Dictionary(std::move(map)));
    }
    return broconf::Value();
}

struct PathKey {
    std::string path;
    std::string key;
};

// "desktop.interface.theme": a key as the access gate is told of it.
std::string displayKey(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

PathKey parsePathKey(std::span<const Value> args) {
    PathKey pk;
    if (args.empty()) return pk;
    if (args.size() >= 2 && !ev::isUndefined(args[1]) && !ev::isNull(args[1]) && !ev::isFunction(args[1])) {
        pk.path = ev::toUtf8(args[0]);
        pk.key = ev::toUtf8(args[1]);
        return pk;
    }
    std::string s = ev::toUtf8(args[0]);
    auto dot = s.rfind('.');
    if (dot != std::string::npos) {
        pk.path = s.substr(0, dot);
        pk.key = s.substr(dot + 1);
    } else {
        pk.path = "";
        pk.key = s;
    }
    return pk;
}

bool parseSetArgs(std::span<const Value> args, std::string& path, std::string& key, ev::Persistent& val) {
    if (args.size() >= 3) {
        val.set(args[2]);
        path = ev::toUtf8(args[0]);
        key = ev::toUtf8(args[1]);
        return true;
    }
    if (args.size() == 2) {
        val.set(args[1]);
        std::string s = ev::toUtf8(args[0]);
        auto dot = s.rfind('.');
        if (dot != std::string::npos) {
            path = s.substr(0, dot);
            key = s.substr(dot + 1);
        } else {
            path = "";
            key = s;
        }
        return true;
    }
    return false;
}

void enqueueChange(uint64_t apiToken, const std::string& path, const std::string& key, const broconf::Value& val) {
    std::lock_guard lock(g_watcher_mu);
    g_change_queue.push_back(QueuedChange{apiToken, path, key, val});
}

bool unwatchInternal(uint64_t apiToken) {
    auto store = activeStore();
    std::lock_guard lock(g_watcher_mu);
    auto it = g_watchers.find(apiToken);
    if (it == g_watchers.end()) return false;
    if (store && it->second.nativeToken) {
        store->unwatch(it->second.nativeToken);
    }
    g_watchers.erase(it);
    return true;
}

} // namespace

void drainWatcherEvents() {
    std::vector<QueuedChange> ready;
    {
        std::lock_guard lock(g_watcher_mu);
        ready.swap(g_change_queue);
    }
    for (const auto& chg : ready) {
        std::shared_ptr<ev::Persistent> cb;
        broconf::Value oldVal;
        bool hasOld = false;
        {
            std::lock_guard lock(g_watcher_mu);
            auto it = g_watchers.find(chg.apiToken);
            if (it != g_watchers.end() && it->second.callback) {
                cb = it->second.callback;
                std::string fullKey = chg.path + "/" + chg.key;
                auto prevIt = it->second.previousValues.find(fullKey);
                if (prevIt != it->second.previousValues.end()) {
                    hasOld = true;
                    oldVal = prevIt->second;
                }
                it->second.previousValues[fullKey] = chg.newValue;
            }
        }
        if (!cb) continue;

        ev::Persistent keyVal(ev::fromUtf8(chg.key));
        ev::Persistent newValVal(confValueToJs(chg.newValue));
        ev::Persistent oldValVal(hasOld ? confValueToJs(oldVal) : ev::undefined());
        ev::Persistent pathVal(ev::fromUtf8(chg.path));

        const Value args[4] = {
            keyVal.get(),
            newValVal.get(),
            oldValVal.get(),
            pathVal.get()
        };
        ev::call(cb->get(), ev::undefined(), std::span<const Value>(args, 4));
    }
}

void clearWatchers() {
    auto store = activeStore();
    std::lock_guard lock(g_watcher_mu);
    for (auto& [id, w] : g_watchers) {
        if (store && w.nativeToken) {
            store->unwatch(w.nativeToken);
        }
    }
    g_watchers.clear();
    g_change_queue.clear();
}

void installConfOnto(Value confObj) {
    ObjectBuilder conf(confObj);

    conf.def("get", 1, [](Value, std::span<const Value> args) -> Value {
        auto store = activeStore();
        PathKey pk = parsePathKey(args);
        try {
            auto val = store->get(pk.path, pk.key);
            return confValueToJs(val);
        } catch (const ConfError& e) {
            return ev::throwError(e.what());
        } catch (const std::exception& e) {
            return ev::throwError(e.what());
        }
    });

    conf.def("getOptional", 1, [](Value, std::span<const Value> args) -> Value {
        auto store = activeStore();
        PathKey pk = parsePathKey(args);
        auto val = store->get_optional(pk.path, pk.key);
        if (val) {
            return confValueToJs(*val);
        }
        return ev::undefined();
    });

    conf.def("set", 2, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent promise(ev::createPromise());
        std::string path;
        std::string key;
        ev::Persistent valArg;

        if (!parseSetArgs(args, path, key, valArg)) {
            ev::Persistent err(makeError("bro.conf.set requires (key, value) or (path, key, value)"));
            ev::rejectPromise(promise.get(), err.get());
            return promise.get();
        }

        // A change to the user's desktop settings: the host's access gate
        // first (api.h, "Access"), before anything is parsed or stored.
        if (std::string refused = ev::checkAccess(ev::AccessKind::DesktopWrite,
                                                  "set " + displayKey(path, key) + " in the desktop settings");
            !refused.empty()) {
            ev::Persistent err(ev::newAccessError(refused, "EACCES"));
            ev::rejectPromise(promise.get(), err.get());
            return promise.get();
        }

        auto store = activeStore();
        std::optional<Type> expectedType;
        auto schema = store->find_schema(path);
        if (schema) {
            auto ks = schema->get_key(key);
            if (ks) expectedType = ks->type;
        }

        broconf::Value confVal;
        try {
            confVal = jsToConfValue(valArg.get(), expectedType);
        } catch (const ValidationError& e) {
            ev::Persistent err(makeError(std::string("ValidationError: ") + e.what()));
            ev::rejectPromise(promise.get(), err.get());
            return promise.get();
        } catch (const std::exception& e) {
            ev::Persistent err(makeError(e.what()));
            ev::rejectPromise(promise.get(), err.get());
            return promise.get();
        }

        try {
            bool ok = store->set(path, key, confVal);
            if (ok) {
                ev::resolvePromise(promise.get(), ev::undefined());
            } else {
                ev::Persistent err(makeError("Failed to set configuration key '" + key + "'"));
                ev::rejectPromise(promise.get(), err.get());
            }
        } catch (const ValidationError& e) {
            ev::Persistent err(makeError(std::string("ValidationError: ") + e.what()));
            ev::rejectPromise(promise.get(), err.get());
        } catch (const std::exception& e) {
            ev::Persistent err(makeError(e.what()));
            ev::rejectPromise(promise.get(), err.get());
        }

        return promise.get();
    });

    // set()/reset() change memory and return; the file is written shortly
    // after, coalesced, on the store's writer thread. flush() writes what is
    // pending now and returns whether that write succeeded.
    conf.def("flush", 0, [](Value, std::span<const Value>) -> Value {
        return ev::fromBool(activeStore()->flush());
    });

    conf.def("reset", 1, [](Value, std::span<const Value> args) -> Value {
        PathKey pk = parsePathKey(args);
        if (std::string refused = ev::checkAccess(ev::AccessKind::DesktopWrite,
                                                  "reset " + displayKey(pk.path, pk.key) + " in the desktop settings");
            !refused.empty()) {
            return ev::throwAccessError(refused, "EACCES");
        }
        auto store = activeStore();
        bool ok = store->reset(pk.path, pk.key);
        return ev::fromBool(ok);
    });

    conf.def("has", 1, [](Value, std::span<const Value> args) -> Value {
        auto store = activeStore();
        PathKey pk = parsePathKey(args);
        bool res = store->has(pk.path, pk.key);
        return ev::fromBool(res);
    });

    conf.def("isDefault", 1, [](Value, std::span<const Value> args) -> Value {
        auto store = activeStore();
        PathKey pk = parsePathKey(args);
        bool res = store->is_default(pk.path, pk.key);
        return ev::fromBool(res);
    });

    conf.def("listKeys", 1, [](Value, std::span<const Value> args) -> Value {
        auto store = activeStore();
        std::string path = args.empty() ? "" : ev::toUtf8(args[0]);
        auto keys = store->list_keys(path);
        ev::Persistent arr(ev::makeArray());
        for (uint32_t i = 0; i < keys.size(); ++i) {
            ev::Persistent k(ev::fromUtf8(keys[i]));
            arr.set(ev::setElement(arr.get(), i, k.get()));
        }
        return arr.get();
    });

    conf.def("watch", 2, [](Value, std::span<const Value> args) -> Value {
        if (args.empty()) {
            return ev::throwTypeError("bro.conf.watch requires (path_or_key, callback)");
        }

        std::string path;
        std::string key;
        ev::Persistent cbArg;

        if (args.size() >= 3 && ev::isFunction(args[2])) {
            cbArg.set(args[2]);
            path = ev::toUtf8(args[0]);
            key = ev::toUtf8(args[1]);
        } else if (args.size() >= 2 && ev::isFunction(args[1])) {
            cbArg.set(args[1]);
            std::string s = ev::toUtf8(args[0]);
            auto store = activeStore();
            auto dot = s.rfind('.');
            if (dot != std::string::npos) {
                std::string candPath = s.substr(0, dot);
                std::string candKey = s.substr(dot + 1);
                if (store->has(candPath, candKey)) {
                    path = candPath;
                    key = candKey;
                } else if (store->find_schema(s)) {
                    path = s;
                    key = "";
                } else {
                    path = candPath;
                    key = candKey;
                }
            } else {
                path = s;
                key = "";
            }
        } else {
            return ev::throwTypeError("bro.conf.watch requires a callback function");
        }

        auto store = activeStore();
        uint64_t apiToken = 0;
        {
            std::lock_guard lock(g_watcher_mu);
            apiToken = g_next_api_token++;
        }

        ActiveWatcher watcher;
        watcher.apiToken = apiToken;
        watcher.path = path;
        watcher.key = key;
        watcher.callback = std::make_shared<ev::Persistent>(cbArg.get());

        if (!key.empty()) {
            auto cur = store->get_optional(path, key);
            if (cur) watcher.previousValues[path + "/" + key] = *cur;
        } else {
            for (const auto& k : store->list_keys(path)) {
                auto cur = store->get_optional(path, k);
                if (cur) watcher.previousValues[path + "/" + k] = *cur;
            }
        }

        broconf::WatcherToken nativeToken = 0;
        if (!key.empty()) {
            nativeToken = store->watch(path, key, [apiToken](const std::string& p, const std::string& k, const broconf::Value& val) {
                enqueueChange(apiToken, p, k, val);
            });
        } else {
            nativeToken = store->watch(path, [apiToken](const std::string& p, const std::string& k, const broconf::Value& val) {
                enqueueChange(apiToken, p, k, val);
            });
        }
        watcher.nativeToken = nativeToken;

        {
            std::lock_guard lock(g_watcher_mu);
            g_watchers.emplace(apiToken, std::move(watcher));
        }

        ObjectBuilder handle;
        handle.set("token", ev::fromDouble(static_cast<double>(apiToken)));
        handle.def("unwatch", 0, [apiToken](Value, std::span<const Value>) -> Value {
            bool ok = unwatchInternal(apiToken);
            return ev::fromBool(ok);
        });
        return handle.build();
    });

    conf.def("unwatch", 1, [](Value, std::span<const Value> args) -> Value {
        if (args.empty()) return ev::fromBool(false);
        uint64_t apiToken = 0;
        if (ev::isObject(args[0])) {
            Value tokVal = ev::getProperty(args[0], "token");
            if (ev::isNumber(tokVal)) {
                apiToken = static_cast<uint64_t>(ev::toDouble(tokVal));
            }
        } else if (ev::isNumber(args[0])) {
            apiToken = static_cast<uint64_t>(ev::toDouble(args[0]));
        }
        if (apiToken == 0) return ev::fromBool(false);
        return ev::fromBool(unwatchInternal(apiToken));
    });

    conf.def("registerSchema", 1, [](Value, std::span<const Value> args) -> Value {
        if (args.empty() || !ev::isObject(args[0])) {
            return ev::throwTypeError("registerSchema requires a schema definition object");
        }
        ev::Persistent def(args[0]);

        Value idVal = ev::getProperty(def.get(), "id");
        if (!ev::isString(idVal)) {
            return ev::throwTypeError("Schema definition requires a string 'id'");
        }
        std::string id = ev::toUtf8(idVal);

        Value pathVal = ev::getProperty(def.get(), "path");
        std::string path = ev::isString(pathVal) ? ev::toUtf8(pathVal) : id;

        auto schema = std::make_shared<Schema>(id, path);

        Value keysVal = ev::getProperty(def.get(), "keys");
        if (ev::isObject(keysVal)) {
            ev::Persistent keysObj(keysVal);
            auto keyNames = getObjectKeys(keysObj.get());
            for (const auto& kn : keyNames) {
                Value kSpecVal = ev::getProperty(keysObj.get(), kn);
                if (!ev::isObject(kSpecVal)) continue;
                ev::Persistent kSpec(kSpecVal);

                Value typeVal = ev::getProperty(kSpec.get(), "type");
                std::string typeStr = ev::isString(typeVal) ? ev::toUtf8(typeVal) : "string";
                auto optType = type_from_string(typeStr);
                Type keyType = optType.value_or(Type::String);

                KeySchema ks;
                ks.name = kn;
                ks.type = keyType;

                Value defVal = ev::getProperty(kSpec.get(), "default");
                if (ev::isUndefined(defVal)) {
                    defVal = ev::getProperty(kSpec.get(), "defaultValue");
                }
                if (!ev::isUndefined(defVal)) {
                    ks.default_value = jsToConfValue(defVal, keyType);
                }

                Value minVal = ev::getProperty(kSpec.get(), "min");
                if (ev::isUndefined(minVal)) minVal = ev::getProperty(kSpec.get(), "minValue");
                if (!ev::isUndefined(minVal)) {
                    ks.min_value = jsToConfValue(minVal, keyType);
                }

                Value maxVal = ev::getProperty(kSpec.get(), "max");
                if (ev::isUndefined(maxVal)) maxVal = ev::getProperty(kSpec.get(), "maxValue");
                if (!ev::isUndefined(maxVal)) {
                    ks.max_value = jsToConfValue(maxVal, keyType);
                }

                Value enumVal = ev::getProperty(kSpec.get(), "enum");
                if (ev::isUndefined(enumVal)) enumVal = ev::getProperty(kSpec.get(), "allowedValues");
                if (ev::isObject(enumVal)) {
                    ev::Persistent enumArr(enumVal);
                    Value lenVal = ev::getProperty(enumArr.get(), "length");
                    uint32_t len = ev::isNumber(lenVal) ? static_cast<uint32_t>(ev::toDouble(lenVal)) : 0;
                    for (uint32_t i = 0; i < len; ++i) {
                        ks.allowed_enum_values.push_back(ev::toUtf8(ev::getElement(enumArr.get(), i)));
                    }
                }

                Value sumVal = ev::getProperty(kSpec.get(), "summary");
                if (ev::isString(sumVal)) ks.summary = ev::toUtf8(sumVal);

                Value descVal = ev::getProperty(kSpec.get(), "description");
                if (ev::isString(descVal)) ks.description = ev::toUtf8(descVal);

                schema->add_key(std::move(ks));
            }
        }

        activeStore()->register_schema(std::move(schema));
        return ev::undefined();
    });
}

} // namespace broconf::api

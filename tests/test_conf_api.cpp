#include "../src/api/api.h"
#include "embed/embed.h"
#include "eval/eval.h"
#include "broconf/store.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "CHECK failed: " #cond " (line " << __LINE__ << ")" \
                      << std::endl;                                        \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

int main() {
    namespace ev = bronze::embed;
    using namespace bronze::eval;

    std::cout << "Starting broconf JavaScript API test..." << std::endl;

    // 1. Create an isolated store in temporary directory
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_api_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);
    auto user_path = tmp_dir / "user_settings.ini";

    broconf::StoreOptions opts;
    opts.user_config_path = user_path;
    opts.enable_dbus = false;
    opts.enable_file_watcher = false;

    auto test_store = broconf::Store::create(opts);
    broconf::api::setStore(test_store);

    // 2. Install bro.conf into Bronze realm
    broconf::api::installConf();

    auto g = ev::globalValue("bro");
    CHECK(g.found);
    CHECK(ev::isObject(g.value));

    ev::Persistent conf(ev::getProperty(g.value, "conf"));
    CHECK(ev::isObject(conf.get()));
    std::cout << "  Mounted bro.conf successfully." << std::endl;

    // Verify all methods exist
    const char* methods[] = {
        "get", "getOptional", "set", "reset", "has", "isDefault",
        "listKeys", "watch", "unwatch", "registerSchema"
    };
    for (const char* m : methods) {
        auto fn = ev::getProperty(conf.get(), m);
        CHECK(ev::isFunction(fn));
        std::cout << "  Found bro.conf." << m << std::endl;
    }

    // 3. Register schema via JS
    std::cout << "Testing registerSchema..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  bro.conf.registerSchema({\n"
            "    id: 'org.bro.test',\n"
            "    path: 'test.app',\n"
            "    keys: {\n"
            "      enabled: { type: 'bool', default: true },\n"
            "      count: { type: 'int', default: 10, min: 0, max: 100 },\n"
            "      opacity: { type: 'double', default: 0.75, min: 0.0, max: 1.0 },\n"
            "      name: { type: 'string', default: 'Bro' },\n"
            "      tags: { type: 'string_list', default: ['alpha', 'beta'] },\n"
            "      theme: { type: 'enum', default: 'dark', enum: ['light', 'dark', 'auto'] },\n"
            "      bgColor: { type: 'color', default: { r: 30, g: 40, b: 50, a: 255 } },\n"
            "      bounds: { type: 'rect', default: { x: 10, y: 20, width: 800, height: 600 } },\n"
            "      metadata: { type: 'dictionary', default: { author: 'bro', version: 1 } }\n"
            "    }\n"
            "  });\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!r.thrown);
        CHECK(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  registerSchema [PASS]" << std::endl;
    }

    // 4. Test has, isDefault, listKeys
    std::cout << "Testing has, isDefault, listKeys..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  if (!bro.conf.has('test.app.enabled')) return false;\n"
            "  if (!bro.conf.has('test.app', 'count')) return false;\n"
            "  if (bro.conf.has('test.app.nonexistent')) return false;\n"
            "  if (!bro.conf.isDefault('test.app.enabled')) return false;\n"
            "  if (!bro.conf.isDefault('test.app', 'count')) return false;\n"
            "  const keys = bro.conf.listKeys('test.app');\n"
            "  if (!Array.isArray(keys) || keys.length !== 9) return false;\n"
            "  if (!keys.includes('enabled') || !keys.includes('count')) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!r.thrown);
        CHECK(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  has, isDefault, listKeys [PASS]" << std::endl;
    }

    // 5. Test get and getOptional with schema defaults
    std::cout << "Testing get and getOptional..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  if (bro.conf.get('test.app.enabled') !== true) return false;\n"
            "  if (bro.conf.get('test.app', 'count') !== 10) return false;\n"
            "  if (Math.abs(bro.conf.get('test.app.opacity') - 0.75) > 0.001) return false;\n"
            "  if (bro.conf.get('test.app.name') !== 'Bro') return false;\n"
            "  const tags = bro.conf.get('test.app.tags');\n"
            "  if (!Array.isArray(tags) || tags.length !== 2 || tags[0] !== 'alpha' || tags[1] !== 'beta') return false;\n"
            "  if (bro.conf.get('test.app.theme') !== 'dark') return false;\n"
            "  const color = bro.conf.get('test.app.bgColor');\n"
            "  if (color.r !== 30 || color.g !== 40 || color.b !== 50 || color.a !== 255) return false;\n"
            "  const bounds = bro.conf.get('test.app.bounds');\n"
            "  if (bounds.x !== 10 || bounds.y !== 20 || bounds.width !== 800 || bounds.height !== 600) return false;\n"
            "  const meta = bro.conf.get('test.app.metadata');\n"
            "  if (!meta || meta.author !== 'bro' || meta.version !== 1) return false;\n"
            "  if (bro.conf.getOptional('test.app.unknown') !== undefined) return false;\n"
            "  let threw = false;\n"
            "  try {\n"
            "    bro.conf.get('test.app.unknown');\n"
            "  } catch (e) {\n"
            "    threw = true;\n"
            "  }\n"
            "  if (!threw) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!r.thrown);
        CHECK(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  get and getOptional [PASS]" << std::endl;
    }

    // 6. Test set (Promise-based)
    std::cout << "Testing set (Promise resolves and rejects)..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  globalThis._testSetSuccess = false;\n"
            "  globalThis._testSetValidationReject = false;\n"
            "  bro.conf.set('test.app.count', 42).then(() => {\n"
            "    globalThis._testSetSuccess = true;\n"
            "  });\n"
            "  bro.conf.set('test.app.count', 999).catch((err) => {\n"
            "    globalThis._testSetValidationReject = true;\n"
            "  });\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!r.thrown);
        ev::drainMicrotasks();

        auto rCheck = evalScript(
            "(function() {\n"
            "  if (!globalThis._testSetSuccess) return false;\n"
            "  if (!globalThis._testSetValidationReject) return false;\n"
            "  if (bro.conf.get('test.app.count') !== 42) return false;\n"
            "  if (bro.conf.isDefault('test.app.count') !== false) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!rCheck.thrown);
        CHECK(ev::isBool(rCheck.value) && ev::toBool(rCheck.value));
        std::cout << "  set Promise [PASS]" << std::endl;
    }

    // 7. Test reset
    std::cout << "Testing reset..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  const ok = bro.conf.reset('test.app.count');\n"
            "  if (!ok) return false;\n"
            "  if (!bro.conf.isDefault('test.app.count')) return false;\n"
            "  if (bro.conf.get('test.app.count') !== 10) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!r.thrown);
        CHECK(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  reset [PASS]" << std::endl;
    }

    // 8. Test watch, unwatch and tickConfAsync
    std::cout << "Testing watch and unwatch with tickConfAsync..." << std::endl;
    {
        auto rInit = evalScript(
            "(function() {\n"
            "  globalThis._events = [];\n"
            "  globalThis._watchHandle = bro.conf.watch('test.app', (key, newVal, oldVal) => {\n"
            "    globalThis._events.push({ key, newVal, oldVal });\n"
            "  });\n"
            "  return (typeof globalThis._watchHandle === 'object' && typeof globalThis._watchHandle.token === 'number');\n"
            "})()\n"
        );
        CHECK(!rInit.thrown);
        CHECK(ev::isBool(rInit.value) && ev::toBool(rInit.value));

        // Set new value
        evalScript("bro.conf.set('test.app.name', 'WatchedName');");
        ev::drainMicrotasks();

        // Before tick, queue has not been drained onto JS thread
        auto rBeforeTick = evalScript("globalThis._events.length;");
        CHECK(!rBeforeTick.thrown && ev::toDouble(rBeforeTick.value) == 0.0);

        // Pump async change queue
        broconf::api::tickConfAsync();

        // After tick, watcher callback should have been called
        auto rAfterTick = evalScript(
            "(function() {\n"
            "  if (globalThis._events.length !== 1) return false;\n"
            "  const ev = globalThis._events[0];\n"
            "  if (ev.key !== 'name') return false;\n"
            "  if (ev.newVal !== 'WatchedName') return false;\n"
            "  if (ev.oldVal !== 'Bro') return false;\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK(!rAfterTick.thrown);
        CHECK(ev::isBool(rAfterTick.value) && ev::toBool(rAfterTick.value));

        // Now unwatch
        evalScript("globalThis._watchHandle.unwatch();");

        // Set another value
        evalScript("bro.conf.set('test.app.name', 'FinalName');");
        ev::drainMicrotasks();
        broconf::api::tickConfAsync();

        // Event count should remain 1
        auto rAfterUnwatch = evalScript("globalThis._events.length;");
        CHECK(!rAfterUnwatch.thrown && ev::toDouble(rAfterUnwatch.value) == 1.0);
        std::cout << "  watch, unwatch, tickConfAsync [PASS]" << std::endl;
    }

    // 9. Shutdown and cleanup
    broconf::api::shutdownConfAsync();
    std::filesystem::remove_all(tmp_dir);

    std::cout << "All broconf API tests PASSED!" << std::endl;
    return 0;
}

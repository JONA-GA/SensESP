#ifndef SENSESP_SRC_SENSESP_SIGNALK_SIGNALK_PREFIX_LISTENER_H_
#define SENSESP_SRC_SENSESP_SIGNALK_SIGNALK_PREFIX_LISTENER_H_

#include <ArduinoJson.h>

#include <memory>
#include <utility>

#include "sensesp/system/valueproducer.h"
#include "signalk_listener.h"

namespace sensesp {

/**
 * @brief A value+path pair emitted for a delta on a prefix-matched path.
 *
 * Unlike `SKValueListener` (one exact path, value only), a prefix listener
 * serves a family of paths, so the consumer needs to know *which* path a
 * delta arrived on. `path` is the full received path; `value` is a read-only
 * view of that delta's `value` member.
 *
 * `value` is backed by `value_doc`, an owned, refcounted copy of the delta
 * value lifted off the transient receive-queue document. Copying this struct
 * is a refcount bump, not a deep copy, and `value` stays valid for as long as
 * any copy survives — so it is safe to retain past the observer call (e.g. for
 * UI work marshalled onto the event loop), unlike a raw alias of the receive
 * document.
 */
struct SKPathValue {
  String path;

  /// Owns the value object so `value` outlives the transient receive-queue
  /// document. Null on a default-constructed (no-data-yet) instance.
  std::shared_ptr<const JsonDocument> value_doc;

  /// Read-only view of the delta's `value` member, backed by `value_doc`.
  JsonVariantConst value;

  SKPathValue() = default;

  /**
   * @param path The full received Signal K path.
   * @param doc An owned document holding a copy of the delta `value`; `value`
   *   is exposed as a read-only view of its root.
   */
  SKPathValue(String path, std::shared_ptr<const JsonDocument> doc)
      : path{std::move(path)},
        value_doc{std::move(doc)},
        value{value_doc ? value_doc->as<JsonVariantConst>()
                        : JsonVariantConst{}} {}
};

/**
 * @brief A ValueProducer that listens to every Signal K value delta whose
 * path begins with a given prefix, emitting the `{path, value}` pair.
 *
 * `SKValueListener`/`SKMetadataListener` match a single exact path. Some
 * consumers must observe a *family* of paths whose members appear and
 * disappear at runtime and cannot be enumerated in advance — the canonical
 * case is `notifications.*`, where a Signal K server raises alarms on
 * arbitrary paths. This listener subscribes with the wildcard `<prefix>*`
 * and routes any delta whose path starts with `<prefix>` (via the overridden
 * `matches()`), so `SKWSClient::process_received_updates` fans matching
 * deltas to it through the normal listener dispatch.
 *
 * Example — observe all notifications:
 * @code
 * auto nl = std::make_shared<SKPrefixListener>("notifications.");
 * nl->connect_to(new LambdaConsumer<SKPathValue>([](const SKPathValue& pv) {
 *   // pv.path e.g. "notifications.mob"; pv.value the notification object
 * }));
 * @endcode
 *
 * @see SKValueListener
 * @see SKWSClient
 */
class SKPrefixListener : public SKListener, public ValueProducer<SKPathValue> {
 public:
  /**
   * @param prefix The path prefix to match, e.g. "notifications.". The
   *   subscription is sent as "<prefix>*".
   * @param listen_delay The minimum interval between updates in ms.
   * @param config_path Optional configuration path.
   */
  SKPrefixListener(const String& prefix, int listen_delay = 1000,
                   const String& config_path = "")
      : SKListener(prefix + "*", listen_delay, config_path), prefix_{prefix} {
    if (prefix == "") {
      ESP_LOGE(__FILENAME__,
               "SKPrefixListener: User has provided no prefix to listen to.");
    }
  }

  bool matches(const String& path) const override {
    return path.startsWith(prefix_);
  }

  void parse_value(const JsonObject& json) override {
    // Copy the delta value into an owned document so the emitted view outlives
    // the receive-queue entry (freed as soon as process_received_updates pops
    // it). Notifications are low-frequency, so the per-delta copy is cheap.
    auto value_doc = std::make_shared<JsonDocument>();
    value_doc->set(json["value"]);
    this->emit(SKPathValue{json["path"].as<String>(), std::move(value_doc)});
  }

 private:
  String prefix_;
};

}  // namespace sensesp

#endif  // SENSESP_SRC_SENSESP_SIGNALK_SIGNALK_PREFIX_LISTENER_H_

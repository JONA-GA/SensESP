/**
 * @file sk_metadata_test.cpp
 * @brief Host unit tests for SKMetadata::add_entry serialization.
 *
 * Runs on the `native` env (no Arduino core):
 *   pio test -e native -f native/test_sk_metadata
 *
 * The first tests pin the exact JSON emitted when none of the newer fields
 * are set, so additions to SKMetadata cannot silently change what existing
 * firmware sends. The rest cover the example, supportsPut, displayScale,
 * and zones serialization added for the Signal K MetaValue model.
 */

#include <ArduinoJson.h>
#include <unity.h>

#include "WString.h"
#include "sensesp/signalk/signalk_metadata.cpp"

using namespace sensesp;

static std::string entry_json(SKMetadata& meta_obj, const char* path) {
  JsonDocument doc;
  JsonArray meta = doc.to<JsonArray>();
  meta_obj.add_entry(String(path), meta);
  std::string output;
  serializeJson(doc, output);
  return output;
}

void test_default_metadata(void) {
  SKMetadata meta;
  TEST_ASSERT_EQUAL_STRING("[{\"path\":\"test.path\",\"value\":{}}]",
                           entry_json(meta, "test.path").c_str());
}

void test_units_only(void) {
  SKMetadata meta("V");
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{\"units\":\"V\"}}]",
      entry_json(meta, "test.path").c_str());
}

void test_legacy_fields(void) {
  SKMetadata meta("V", "Battery Voltage", "House battery", "Batt V", 30.0,
                  true);
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{"
      "\"displayName\":\"Battery Voltage\",\"units\":\"V\","
      "\"description\":\"House battery\",\"shortName\":\"Batt V\","
      "\"timeout\":30,\"supportsPut\":true}}]",
      entry_json(meta, "test.path").c_str());
}

void test_supports_put_false_omitted(void) {
  SKMetadata meta("V");
  meta.supports_put_ = false;
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{\"units\":\"V\"}}]",
      entry_json(meta, "test.path").c_str());
}

void test_example_field(void) {
  SKMetadata meta;
  meta.example_ = "12.6";
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{\"example\":\"12.6\"}}]",
      entry_json(meta, "test.path").c_str());
}

void test_display_scale_both_bounds(void) {
  SKMetadata meta;
  meta.display_scale_lower_ = 0.0f;
  meta.display_scale_upper_ = 8000.0f;
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{"
      "\"displayScale\":{\"lower\":0,\"upper\":8000}}}]",
      entry_json(meta, "test.path").c_str());
}

// A one-sided displayScale is invalid per the Signal K schema: omit it.
void test_display_scale_single_bound_omitted(void) {
  SKMetadata lower_only;
  lower_only.display_scale_lower_ = 0.0f;
  TEST_ASSERT_EQUAL_STRING("[{\"path\":\"test.path\",\"value\":{}}]",
                           entry_json(lower_only, "test.path").c_str());

  SKMetadata upper_only;
  upper_only.display_scale_upper_ = 8000.0f;
  TEST_ASSERT_EQUAL_STRING("[{\"path\":\"test.path\",\"value\":{}}]",
                           entry_json(upper_only, "test.path").c_str());
}

void test_zones(void) {
  SKMetadata meta;
  meta.zones_.push_back(
      SKMetadataZone(SKAlarmState::kNominal, "Normal range", 600.0f, 6500.0f));
  meta.zones_.push_back(
      SKMetadataZone(SKAlarmState::kAlarm, "Overspeed", 7000.0f));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{\"zones\":["
      "{\"state\":\"nominal\",\"message\":\"Normal range\","
      "\"lower\":600,\"upper\":6500},"
      "{\"state\":\"alarm\",\"message\":\"Overspeed\",\"lower\":7000}"
      "]}}]",
      entry_json(meta, "test.path").c_str());
}

void test_zone_upper_only(void) {
  SKMetadata meta;
  meta.zones_.push_back(
      SKMetadataZone(SKAlarmState::kAlarm, "Low fuel", NAN, 0.1f));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{\"zones\":["
      "{\"state\":\"alarm\",\"message\":\"Low fuel\",\"upper\":0.1}"
      "]}}]",
      entry_json(meta, "test.path").c_str());
}

// An empty zone message is omitted so the server default applies.
void test_zone_empty_message_omitted(void) {
  SKMetadata meta;
  meta.zones_.push_back(SKMetadataZone(SKAlarmState::kWarn, "", 100.0f));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"path\":\"test.path\",\"value\":{\"zones\":["
      "{\"state\":\"warn\",\"lower\":100}"
      "]}}]",
      entry_json(meta, "test.path").c_str());
}

void test_alarm_state_strings(void) {
  const struct {
    SKAlarmState state;
    const char* expected;
  } cases[] = {
      {SKAlarmState::kNominal, "nominal"},
      {SKAlarmState::kNormal, "normal"},
      {SKAlarmState::kAlert, "alert"},
      {SKAlarmState::kWarn, "warn"},
      {SKAlarmState::kAlarm, "alarm"},
      {SKAlarmState::kEmergency, "emergency"},
  };
  for (const auto& c : cases) {
    SKMetadata meta;
    meta.zones_.push_back(SKMetadataZone(c.state, "msg", 1.0f));
    std::string expected =
        std::string("[{\"path\":\"test.path\",\"value\":{\"zones\":[") +
        "{\"state\":\"" + c.expected + "\",\"message\":\"msg\",\"lower\":1}" +
        "]}}]";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(),
                             entry_json(meta, "test.path").c_str());
  }
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_metadata);
  RUN_TEST(test_units_only);
  RUN_TEST(test_legacy_fields);
  RUN_TEST(test_supports_put_false_omitted);
  RUN_TEST(test_example_field);
  RUN_TEST(test_display_scale_both_bounds);
  RUN_TEST(test_display_scale_single_bound_omitted);
  RUN_TEST(test_zones);
  RUN_TEST(test_zone_upper_only);
  RUN_TEST(test_zone_empty_message_omitted);
  RUN_TEST(test_alarm_state_strings);
  return UNITY_END();
}

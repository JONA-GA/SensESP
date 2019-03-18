#include "ws_client.h"

#include "Arduino.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#ifdef ESP8266
  #include <ESP8266mDNS.h>        // Include the mDNS library
#elif defined(ESP32)
  #include <ESPmDNS.h>
#endif

#include <ESP8266TrueRandom.h>

#include <WiFiClient.h>

#include "app.h"

WSClient* ws_client;

void webSocketClientEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      ws_client->on_disconnected();
      break;
    case WStype_ERROR:
      ws_client->on_error();
      break;
    case WStype_CONNECTED:
      ws_client->on_connected(payload);
      break;
    case WStype_TEXT:
      ws_client->on_receive_delta(payload);
      break;
   }
}

WSClient::WSClient(String id, String schema, SKDelta* sk_delta,
                   std::function<void(bool)> connected_cb,
                   void_cb_func delta_cb) : Configurable{id, schema} {
  this->sk_delta = sk_delta;
  this->connected_cb = connected_cb;
  this->delta_cb = delta_cb;

  // set the singleton object pointer
  ws_client = this;

  load_configuration();
}

void WSClient::enable() {
  app.onDelay(0, [this](){ this->connect(); });
  app.onRepeat(20, [this](){ this->loop(); });
  app.onRepeat(100, [this](){ this->send_delta(); });
  app.onRepeat(10000, [this](){ this->connect_loop(); });
}

void WSClient::connect_loop() {
  if (this->connection_state==disconnected) {
    this->connect();
  }
}

void WSClient::on_disconnected() {
  this->connection_state = disconnected;
  Serial.println("Websocket client disconnected.");
  this->connected_cb(false);
}

void WSClient::on_error() {
  this->connection_state = disconnected;
  Serial.println("Websocket client error.");
  this->connected_cb(false);
}

void WSClient::on_connected(uint8_t * payload) {
  this->connection_state = connected;
  Serial.printf("Websocket client connected to URL: %s\n", payload);
  this->connected_cb(true);
}

void WSClient::on_receive_delta(uint8_t * payload) {
  Serial.println("Received payload:");
  Serial.println((char*)payload);
}

bool WSClient::get_mdns_service(String& host, uint16_t& port) {
  // get IP address using an mDNS query
  int n = MDNS.queryService("signalk-ws", "tcp");
  if (n==0) {
    // no service found
    return false;
  } else {
    host = MDNS.IP(0).toString();
    port = MDNS.port(0);
    Serial.print(F("Found server with IP/Port: "));
    Serial.print(host); Serial.print(":"); Serial.println(port);
    return true;
  }
}

void WSClient::connect() {
  if (connection_state!=disconnected) {
    return;
  }
  Serial.println("Initiating connection");

  connection_state = connecting;

  String host = "";
  uint16_t port = 80;

  if (this->host.length() == 0) {
    get_mdns_service(host, port);
  } else {
    host = this->host;
    port = this->port;
  }

  if ((host.length() > 0) && (port > 0)) {
    Serial.println(F("Websocket client starting"));
  } else {
    // host and port not defined - wait for mDNS
    connection_state = disconnected;
    return;
  }

  if (this->polling_href != "") {
    // existing pending request
    this->poll_access_request(host, port, this->polling_href);
    return;
  }

  if (this->auth_token == "") {
    // initiate HTTP authentication
    this->send_access_request(host, port);
    return;
  }
  this->test_token(host, port);
}

void WSClient::test_token(const String host, const uint16_t port) {

  // FIXME: implement async HTTP client!
  WiFiClient client;
  HTTPClient http;

  Serial.println("Testing token");

  String url = String("http://") + host + ":" + port + "/signalk/v1/api/";
  http.begin(client, url);
  String full_token = String("JWT ") + auth_token;
  http.addHeader("Authorization", full_token.c_str());
  int httpCode = http.GET();
  http.end();
  Serial.printf("Testing resulted in http status %d\n", httpCode);
  if (httpCode == 200) {
    // our token is valid, go ahead and connect
    this->connect_ws(host, port);
  } else if (httpCode == 401) {
    this->send_access_request(host, port);
  } else {
    connection_state = disconnected;
  }
}

void WSClient::send_access_request(const String host, const uint16_t port) {
  Serial.println("Preparing a new access request");
  if (client_id == "") {
    // generate a client ID
    byte uuidNumber[16];
    ESP8266TrueRandom.uuid(uuidNumber);
    client_id = ESP8266TrueRandom.uuidToString(uuidNumber);
    save_configuration();
  }

  // create a new access request
  DynamicJsonBuffer buf;
  JsonObject& req = buf.createObject();
  req["clientId"] = client_id;
  req["description"] =
    String("SensESP sensor: ") + sensesp_app->get_hostname();
  String json_req = "";
  req.printTo(json_req);

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + host + ":" + port
      + "/signalk/v1/access/requests";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(json_req);
  String payload = http.getString();
  http.end();

  // if we get a response we can't handle, try to reconnect later
  if (httpCode != 202) {
    Serial.printf("Can't handle response %d to access request.\n", httpCode);
    Serial.println(payload);
    connection_state = disconnected;
    return;
  }

  // http status code 202

  JsonObject& resp = buf.parseObject(payload);
  String state = resp["state"];

  if (state != "PENDING") {
    Serial.print("Got unknown state: ");
    Serial.println(state);
    connection_state = disconnected;
    return;
  }

  String href = resp["href"];
  polling_href = href;
  save_configuration();

  Serial.print("Polling ");
  Serial.print(polling_href);
  Serial.println(" in 5 seconds");
  app.onDelay(5000, [this, host, port](){
    this->poll_access_request(host, port, this->polling_href);
  });
}

void WSClient::poll_access_request(const String host, const uint16_t port, const String href) {
  Serial.println("Polling");

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + host + ":" + port + href;
  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode == 200 or httpCode == 202) {
    String payload = http.getString();
    http.end();
    DynamicJsonBuffer buf;
    JsonObject& resp = buf.parseObject(payload);
    String state = resp["state"];
    Serial.println(state);
    if (state == "PENDING") {
      app.onDelay(5000, [this, host, port, href](){ this->poll_access_request(host, port, href); });
      return;
    } else if (state == "COMPLETED") {
      JsonObject& access_req = resp["accessRequest"];
      String permission = access_req["permission"];
      polling_href = "";
      save_configuration();

      if (permission == "DENIED") {
        Serial.println("Permission denied");
        connection_state = disconnected;
        return;
      } else if (permission == "APPROVED") {
        Serial.println("Permission granted");
        String token = access_req["token"];
        auth_token = token;
        save_configuration();
        app.onDelay(0, [this, host, port](){ this->connect_ws(host, port); });
        return;
      }
    }
  } else {
    http.end();
    if (httpCode==500) {
      // this is probably the server barfing due to
      // us polling a non-existing request. Just
      // delete the polling href.
      Serial.println("Got 500, probably a non-existing request.");
      polling_href = "";
      save_configuration();
      connection_state = disconnected;
      return;
    }
    // any other HTTP status code
    Serial.printf("Can't handle response %d to pending access request.\n", httpCode);
    connection_state = disconnected;
    return;
  }
}

void WSClient::connect_ws(const String host, const uint16_t port) {
  String path = "/signalk/v1/stream?subscribe=none";

  this->client.begin(host, port, path);
  this->client.onEvent(webSocketClientEvent);
  String full_token = String("JWT ") + auth_token;
  this->client.setAuthorization(full_token.c_str());
}

void WSClient::loop() {
  this->client.loop();
}

bool WSClient::is_connected() {
  return connection_state==connected;
}

void WSClient::restart() {
  if (connection_state==connected) {
    this->client.disconnect();
    connection_state = disconnected;
  }
}

void WSClient::send_delta() {
  String output;
  if (sk_delta->data_available()) {
    sk_delta->get_delta(output);
    if (connection_state==connected) {
      this->client.sendTXT(output);
      this->delta_cb();
    }
  }
}

JsonObject& WSClient::get_configuration(JsonBuffer& buf) {
  JsonObject& root = buf.createObject();
  root["sk_host"] = this->host;
  root["sk_port"] = this->port;
  root["token"] = this->auth_token;
  root["client_id"] = this->client_id;
  root["polling_href"] = this->polling_href;
  return root;
}

bool WSClient::set_configuration(const JsonObject& config) {
  String expected[] = {"sk_host", "sk_port", "token"};
  for (auto str : expected) {
    if (!config.containsKey(str)) {
      return false;
    }
  }
  this->host = config["sk_host"].as<String>();
  this->port = config["sk_port"].as<int>();
  // FIXME: setting the token should not be allowed via the REST API.
  this->auth_token = config["token"].as<String>();
  this->client_id = config["client_id"].as<String>();
  this->polling_href = config["polling_href"].as<String>();
  return true;
}

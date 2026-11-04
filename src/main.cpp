#include <WiFi.h>
#include <PubSubClient.h>
#include <BleMouse.h>
#include "WiFiClientSecure.h"
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>

String ssid;
String password;
String bleName;
Preferences preferences;
WebServer server(80);
bool portalActive = false;
constexpr int CONFIG_BUTTON_PIN = 0; // ESP32-S3-DevKitC-1 BOOT button
constexpr uint32_t CONFIG_HOLD_MS = 3000;
uint32_t buttonPressedAt = 0;
bool buttonWasPressed = false;
String mqttServer;
String mqttUserName;
String mqttPassword;
uint16_t mqttPort = 8883;
String mqttMode;
String mqttCa;
WiFiClient plainClient;

// Keep the existing topics and the command "1" for compatibility.
const char *subTopic = "wol";
const char *pubTopic = "esp32-heart";

BleMouse *bleMouse = nullptr;
WiFiClientSecure espClient;
PubSubClient client(espClient);

constexpr uint32_t WIFI_RETRY_MS = 30000;
constexpr uint32_t MQTT_RETRY_MS = 10000;
constexpr uint32_t HEARTBEAT_MS = 15000;
constexpr uint32_t WAKE_TIMEOUT_MS = 60000;
constexpr uint32_t BLE_READY_MS = 1500;
constexpr time_t MIN_VALID_TIME = 1704067200; // 2024-01-01 UTC

uint32_t lastWifiAttempt = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastHeartbeat = 0;
uint32_t wakeRequestedAt = 0;
uint32_t bleConnectedAt = 0;
bool wifiWasConnected = false;
bool bleWasConnected = false;
bool wakePending = false;
char mqttClientId[32];

void report(const char *message)
{
  Serial.println(message);
  if (client.connected() && !client.publish(pubTopic, message)) {
    Serial.println("MQTT publish failed");
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  if (strcmp(topic, subTopic) != 0 || length != 1 || payload[0] != '1') {
    Serial.printf("Ignoring invalid command (length=%u)\n", length);
    return;
  }
  if (wakePending) {
    report("wake already pending");
    return;
  }
  wakePending = true;
  wakeRequestedAt = millis();
  report("wake received; waiting for BLE connection");
}

void startPortal()
{
  if (portalActive) return;
  portalActive = true;
  WiFi.mode(WIFI_AP_STA);
  uint64_t id = ESP.getEfuseMac();
  char apName[32];
  snprintf(apName, sizeof(apName), "ESP32-Setup-%04X", (unsigned int)(id & 0xffff));
  IPAddress apIp(192, 168, 8, 1);
  IPAddress apMask(255, 255, 255, 0);
  if (!WiFi.softAPConfig(apIp, apIp, apMask)) {
    Serial.println("Failed to configure hotspot address");
    portalActive = false;
    return;
  }
  if (!WiFi.softAP(apName)) {
    Serial.println("Failed to start configuration hotspot");
    portalActive = false;
    return;
  }
  Serial.printf("Configuration hotspot: %s, address: 192.168.8.1\n", apName);
  server.on("/scan", HTTP_GET, []() {
    int count = WiFi.scanNetworks();
    String json = "[";
    bool first = true;
    for (int i = 0; i < count; ++i) {
      String name = WiFi.SSID(i);
      if (name.isEmpty()) continue;
      bool duplicate = false;
      for (int j = 0; j < i; ++j) {
        if (WiFi.SSID(j) == name) { duplicate = true; break; }
      }
      if (duplicate) continue;
      name.replace("\\", "\\\\");
      name.replace("\"", "\\\"");
      if (!first) json += ",";
      first = false;
      json += "\"" + name + "\"";
    }
    WiFi.scanDelete();
    server.send(200, "application/json", json + "]");
  });
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>设备配置 / Device setup</title>
<style>*{box-sizing:border-box}body{margin:0;background:#f3f5f8;color:#1d2939;font:16px -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}main{max-width:520px;margin:auto;padding:24px 16px 40px}h1{font-size:24px;margin:0 0 8px}p{color:#526174;line-height:1.5}section{background:white;border-radius:12px;padding:18px;margin:16px 0;box-shadow:0 1px 4px #0001}h2{font-size:18px;margin:0 0 16px}label{display:block;font-weight:600;margin:16px 0 7px}input,select,textarea,button{display:block;width:100%;font:inherit;border-radius:8px;min-height:46px}input,select,textarea{border:1px solid #aab6c5;background:white;color:#17212f;padding:10px}textarea{resize:vertical}button{border:0;background:#1769d2;color:white;font-weight:600;padding:12px;cursor:pointer}button.secondary{background:#e8f0fc;color:#1355ad;margin-top:10px}button:disabled{opacity:.6}small{display:block;color:#526174;line-height:1.5;margin-top:8px}#status{min-height:1.5em;margin:8px 0 0;font-size:14px}</style></head>
<body><main><h1>设备配置 / Device setup</h1><p>填写信息后保存，设备将自动重启。 / Save to restart.</p><form method="post" action="/save">
<section><h2>Wi-Fi</h2><label for="networks">选择附近的网络 / Select a network</label><select id="networks"><option value="">请选择，或在下方手动输入 / Select or enter below</option></select><button class="secondary" id="scan" type="button">扫描附近 Wi-Fi / Scan Wi-Fi</button><p id="status" aria-live="polite"></p><label for="ssid">Wi-Fi 名称 / Wi-Fi name</label><input id="ssid" name="ssid" maxlength="32" required autocapitalize="none" autocorrect="off" spellcheck="false"><label for="password">Wi-Fi 密码 / Wi-Fi password</label><input id="password" name="password" type="password" maxlength="63" autocomplete="new-password"><small>开放网络可留空。设备支持 2.4 GHz Wi-Fi。 / Leave blank for open networks. 2.4 GHz only.</small></section>
<section><h2>蓝牙</h2><label for="ble">蓝牙设备名称 / Bluetooth name</label><input id="ble" name="ble" maxlength="29" value="ESPblueMouse" required></section>
<section><h2>MQTT</h2><label for="host">服务器 / Server</label><input id="host" name="host" required autocapitalize="none" autocorrect="off" spellcheck="false"><label for="mode">连接方式 / Connection mode</label><select id="mode" name="mode"><option value="tls">TLS（验证 CA / verify CA）</option><option value="plain">普通连接 / Plain MQTT</option><option value="insecure">TLS（不验证证书 / no verification）</option></select><label for="ca">CA 证书 / CA certificate</label><textarea id="ca" name="ca" rows="6" placeholder="-----BEGIN CERTIFICATE-----" autocapitalize="none" autocorrect="off" spellcheck="false"></textarea><small>验证 CA 时请粘贴 PEM 证书。 / Paste the PEM CA certificate for verified TLS.</small><label for="port">端口 / Port</label><input id="port" name="port" type="number" inputmode="numeric" min="1" max="65535" value="8883" required><label for="user">用户名 / Username</label><input id="user" name="user" autocapitalize="none" autocorrect="off"><label for="mqttPassword">密码 / Password</label><input id="mqttPassword" name="mqttPassword" type="password" autocomplete="new-password"></section><button type="submit">保存并重启 / Save and restart</button></form></main>
<script>(function(){var mode=document.getElementById('mode'),port=document.getElementById('port'),ca=document.getElementById('ca');function updateMode(){ca.required=mode.value==='tls';ca.disabled=mode.value!=='tls';port.value=mode.value==='plain'?'1883':'8883'}mode.addEventListener('change',updateMode);updateMode();var list=document.getElementById('networks'),status=document.getElementById('status'),button=document.getElementById('scan');list.addEventListener('change',function(){if(list.value)document.getElementById('ssid').value=list.value});function scan(){button.disabled=true;status.textContent='正在扫描，请稍候… / Scanning…';var xhr=new XMLHttpRequest();xhr.open('GET','/scan',true);xhr.timeout=30000;xhr.onload=function(){try{if(xhr.status!==200)throw Error();var names=JSON.parse(xhr.responseText);while(list.options.length>1)list.remove(1);names.forEach(function(name){var option=document.createElement('option');option.value=name;option.textContent=name;list.appendChild(option)});status.textContent=names.length?'找到 '+names.length+' 个网络，请选择。 / networks found. Select one.':'未发现网络，可重试或手动输入。 / No networks found; retry or enter manually.'}catch(e){status.textContent='扫描失败，请重试或手动输入。 / Scan failed; retry or enter manually.'}button.disabled=false};xhr.onerror=xhr.ontimeout=function(){status.textContent='扫描失败，请重试或手动输入。 / Scan failed; retry or enter manually.';button.disabled=false};xhr.send()}button.addEventListener('click',scan);scan()})();</script></body></html>)HTML");
  });
  server.on("/save", HTTP_POST, []() {
    String newSsid = server.arg("ssid");
    String newPassword = server.arg("password");
    String newBle = server.arg("ble");
    String host = server.arg("host");
    String portText = server.arg("port");
    long port = portText.toInt();
    String user = server.arg("user");
    String mqttPass = server.arg("mqttPassword");
    String mode = server.arg("mode");
    String ca = server.arg("ca");
    if (!newSsid.length() || newSsid.length() > 32 || newPassword.length() > 63 || !newBle.length() || newBle.length() > 29 || !host.length() || host.length() > 253 || port < 1 || port > 65535 || user.length() > 128 || mqttPass.length() > 128 || (mode != "plain" && mode != "tls" && mode != "insecure") || (mode == "tls" && !ca.length()) || ca.length() > 8192 || (ca.length() && (!ca.startsWith("-----BEGIN CERTIFICATE-----") || ca.indexOf("-----END CERTIFICATE-----") < 0))) {
      server.send(400, "text/plain; charset=utf-8", "配置无效，请检查输入。 / Invalid configuration.");
      return;
    }
    if (!preferences.begin("settings", false)) {
      server.send(500, "text/plain; charset=utf-8", "无法打开配置存储，请重试。 / Cannot open storage.");
      return;
    }
    bool saved = preferences.putString("ssid", newSsid) > 0 &&
                 preferences.putString("password", newPassword) == newPassword.length() &&
                 preferences.putString("ble", newBle) > 0 &&
                 preferences.putString("mqttHost", host) > 0 &&
                 preferences.putUShort("mqttPort", port) > 0 &&
                 preferences.putString("mqttUser", user) == user.length() &&
                 preferences.putString("mqttPass", mqttPass) == mqttPass.length() &&
                 preferences.putString("mqttMode", mode) > 0 &&
                 preferences.putString("mqttCa", ca) == ca.length();
    preferences.end();
    if (!saved) {
      server.send(500, "text/plain; charset=utf-8", "保存失败，请重试。 / Save failed.");
      return;
    }
    server.send(200, "text/html; charset=utf-8", "<meta name=viewport content=width=device-width><p>配置已保存，设备正在重启。 / Saved. Restarting.</p>");
    delay(500);
    ESP.restart();
  });
  server.begin();
}

void maintainWifi(uint32_t now)
{
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!portalActive && ssid.isEmpty()) startPortal();
  if (portalActive) server.handleClient();
  if (connected && !wifiWasConnected) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    // TLS certificate validity requires a correct clock. SNTP runs in the background.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    lastMqttAttempt = now - MQTT_RETRY_MS;
  } else if (!connected && wifiWasConnected) {
    Serial.println("WiFi disconnected");
    espClient.stop();
    plainClient.stop();
    lastWifiAttempt = now - WIFI_RETRY_MS;
  }
  wifiWasConnected = connected;
  if (!connected && !ssid.isEmpty() && (uint32_t)(now - lastWifiAttempt) >= WIFI_RETRY_MS) {
    lastWifiAttempt = now;
    Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), password.c_str());
  }
}

void maintainMqtt(uint32_t now)
{
  if (!wifiWasConnected || mqttServer.isEmpty()) return;
  if (client.connected()) {
    client.loop();
    return;
  }
  if ((uint32_t)(now - lastMqttAttempt) < MQTT_RETRY_MS) return;
  lastMqttAttempt = now;
  if (mqttMode == "tls" && time(nullptr) < MIN_VALID_TIME) {
    Serial.println("Waiting for time synchronization before TLS connection");
    return;
  }
  Serial.println("Connecting to MQTT...");
  espClient.stop();
  plainClient.stop();
  if (!client.connect(mqttClientId, mqttUserName.c_str(), mqttPassword.c_str(),
                      pubTopic, 0, false, "device offline")) {
    Serial.printf("MQTT connection failed, state=%d\n", client.state());
    return;
  }
  if (!client.subscribe(subTopic, 0)) {
    Serial.println("MQTT subscribe send failed; reconnecting");
    client.disconnect();
    return;
  }
  // PubSubClient reports whether SUBSCRIBE was sent, not broker authorization.
  report("device online; subscription requested");
  lastHeartbeat = millis();
}

void maintainWake(uint32_t now)
{
  bool connected = bleMouse->isConnected();
  if (connected != bleWasConnected) {
    bleWasConnected = connected;
    if (connected) {
      bleConnectedAt = now;
      Serial.println("BLE mouse connected");
    } else {
      Serial.println("BLE mouse disconnected; advertising restarted");
    }
  }
  if (!wakePending) return;
  if ((uint32_t)(now - wakeRequestedAt) >= WAKE_TIMEOUT_MS) {
    wakePending = false;
    report("wake failed: BLE connection timeout");
    return;
  }
  // Allow the Mac time to configure HID notifications after reconnecting.
  if (connected && (uint32_t)(now - bleConnectedAt) >= BLE_READY_MS) {
    wakePending = false;
    bleMouse->click(MOUSE_LEFT);
    report("wake event sent via BLE mouse");
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  preferences.begin("settings", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  bleName = preferences.getString("ble", "ESPblueMouse");
  mqttServer = preferences.getString("mqttHost", "");
  mqttPort = preferences.getUShort("mqttPort", 8883);
  mqttUserName = preferences.getString("mqttUser", "");
  mqttPassword = preferences.getString("mqttPass", "");
  mqttMode = preferences.getString("mqttMode", "tls");
  mqttCa = preferences.getString("mqttCa", "");
  preferences.end();
  bleMouse = new BleMouse(bleName.c_str(), "Espressif", 100);
  bleMouse->begin();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(mqttClientId, sizeof(mqttClientId), "ESP32-%04X%08X",
           (unsigned int)(chipId >> 32), (unsigned int)chipId);
  lastWifiAttempt = millis() - WIFI_RETRY_MS;

  if (mqttMode == "plain") client.setClient(plainClient);
  else if (mqttMode == "insecure") espClient.setInsecure();
  else espClient.setCACert(mqttCa.c_str());
  espClient.setTimeout(5);
  espClient.setHandshakeTimeout(10);
  client.setServer(mqttServer.c_str(), mqttPort);
  client.setCallback(callback);
  client.setKeepAlive(30);
  client.setSocketTimeout(5);
}

void loop()
{
  uint32_t now = millis();
  bool pressed = digitalRead(CONFIG_BUTTON_PIN) == LOW;
  if (pressed && !buttonWasPressed) buttonPressedAt = now;
  if (pressed && buttonWasPressed && !portalActive && (uint32_t)(now - buttonPressedAt) >= CONFIG_HOLD_MS) startPortal();
  buttonWasPressed = pressed;
  maintainWifi(now);
  maintainMqtt(now);
  now = millis(); // A TLS or MQTT connection attempt can take several seconds.
  maintainWake(now);
  if (client.connected() && (uint32_t)(now - lastHeartbeat) >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    report(bleMouse->isConnected() ? "heartbeat: BLE connected" : "heartbeat: BLE disconnected");
  }
  delay(10);
}

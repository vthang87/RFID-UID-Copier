#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

#define LED_PIN 48
#define NUM_LEDS 1

#define RC522_SCK 12
#define RC522_MISO 13
#define RC522_MOSI 11
#define RC522_SS 10
#define RC522_RST 9

#define WAIT_CARD_MS 10000
#define DNS_PORT 53

const char *AP_SSID = "RFID-Copier";
const char *AP_PASS = "12345678";

Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
MFRC522 rfid(RC522_SS, RC522_RST);
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
IPAddress apIP(192, 168, 4, 1);

enum Mode { IDLE, WAIT_READ, WAIT_WRITE };
Mode mode = IDLE;
unsigned long deadline = 0;

byte storedUid[10];
byte storedUidSize = 0;
bool hasStored = false;

String statusMsg = "San sang";
String lastResult = "idle";

unsigned long ledUntil = 0;

String staSsid = "";
String staPass = "";
String wifiMsg = "Chua ket noi WiFi ngoai";
bool credsSaved = false;

bool apActive = false;
unsigned long bootTime = 0;
const unsigned long AP_FALLBACK_MS = 30000;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void flashLed(uint8_t r, uint8_t g, uint8_t b, int ms) {
  setLed(r, g, b);
  ledUntil = millis() + ms;
}

String uidToString(byte *uid, byte size) {
  String s = "";
  for (byte i = 0; i < size; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
    if (i < size - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}

String jsonEscape(const String &in) {
  String o = "";
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (c >= 32) {
      o += c;
    }
  }
  return o;
}

const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>RFID Copier</title>
<style>
  :root{--bg:#0f172a;--card:#1e293b;--accent:#38bdf8;--ok:#22c55e;--err:#ef4444;--warn:#f59e0b;--txt:#e2e8f0;--mut:#94a3b8}
  *{box-sizing:border-box;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
  body{margin:0;background:var(--bg);color:var(--txt);min-height:100vh;display:flex;justify-content:center}
  .wrap{width:100%;max-width:460px;padding:20px}
  h1{font-size:22px;margin:8px 0 2px;text-align:center}
  .sub{color:var(--mut);text-align:center;font-size:13px;margin-bottom:18px}
  .card{background:var(--card);border-radius:16px;padding:18px;margin-bottom:16px;box-shadow:0 8px 24px rgba(0,0,0,.3)}
  .label{font-size:12px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px}
  .uid{font-size:24px;font-weight:700;letter-spacing:1px;margin-top:6px;word-break:break-all;color:var(--accent)}
  .status{font-size:15px;margin-top:6px;min-height:22px}
  .dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:8px;vertical-align:middle;background:var(--mut)}
  .dot.busy{background:var(--warn);animation:pulse 1s infinite}
  .dot.ok{background:var(--ok)} .dot.err{background:var(--err)}
  @keyframes pulse{50%{opacity:.3}}
  button{width:100%;border:0;border-radius:12px;padding:14px;font-size:15px;font-weight:600;color:#06121f;margin-top:10px;cursor:pointer;transition:transform .05s}
  button:active{transform:scale(.98)}
  .b-read{background:var(--accent)} .b-write{background:var(--ok)} .b-clear{background:#475569;color:#fff}
  .b-scan{background:var(--warn)}
  button:disabled{opacity:.5}
  .hint{font-size:12px;color:var(--mut);margin-top:14px;line-height:1.5}
  .net{display:flex;justify-content:space-between;align-items:center;padding:11px 12px;background:#0f1b30;border-radius:10px;margin-top:8px;cursor:pointer}
  .net:active{background:#16243d}
  .net .nm{font-weight:600;font-size:14px}
  .net .meta{font-size:12px;color:var(--mut)}
  input{width:100%;padding:12px;border-radius:10px;border:1px solid #334155;background:#0f1b30;color:var(--txt);font-size:15px;margin-top:8px}
  .tabs{display:flex;gap:8px;margin-bottom:14px}
  .tab{flex:1;text-align:center;padding:10px;border-radius:10px;background:var(--card);color:var(--mut);cursor:pointer;font-weight:600}
  .tab.on{background:var(--accent);color:#06121f}
  .pane{display:none}.pane.on{display:block}
  .wifi-st{font-size:13px;color:var(--mut);margin-top:8px}
</style>
</head>
<body>
<div class="wrap">
  <h1>RFID UID Copier</h1>
  <div class="sub">ESP32-S3 + RC522</div>

  <div class="tabs">
    <div class="tab on" id="tabCopy" onclick="tab('copy')">Copy the</div>
    <div class="tab" id="tabWifi" onclick="tab('wifi')">WiFi</div>
  </div>

  <div class="pane on" id="paneCopy">
    <div class="card">
      <div class="label">UID da luu (the goc)</div>
      <div class="uid" id="uid">--</div>
    </div>
    <div class="card">
      <div class="label">Trang thai</div>
      <div class="status"><span class="dot" id="dot"></span><span id="msg">...</span></div>
    </div>
    <button class="b-read" id="btnRead" onclick="act('read')">1. Doc the GOC</button>
    <button class="b-write" id="btnWrite" onclick="act('write')">2. Ghi sang the MAGIC</button>
    <button class="b-clear" onclick="act('clear')">Xoa UID da luu</button>
    <div class="hint">Bam <b>Doc the GOC</b> roi ap the goc. Sau do dat <b>the magic</b> len va bam <b>Ghi</b>.</div>
  </div>

  <div class="pane" id="paneWifi">
    <div class="card">
      <div class="label">Ket noi WiFi ngoai</div>
      <div class="wifi-st" id="wifiSt">...</div>
    </div>
    <button class="b-scan" id="btnScan" onclick="scan()">Quet mang WiFi</button>
    <div id="netList"></div>
    <div class="card" id="connBox" style="display:none">
      <div class="label">Ket noi vao mang da chon</div>
      <input id="ssid" placeholder="SSID" readonly>
      <input id="pass" type="password" placeholder="Mat khau WiFi">
      <button class="b-write" onclick="connect()">Ket noi & Luu</button>
    </div>
    <button class="b-clear" id="btnForget" onclick="forget()" style="display:none">Quen WiFi da luu</button>
  </div>
</div>
<script>
function tab(t){
  document.getElementById('paneCopy').className='pane'+(t==='copy'?' on':'');
  document.getElementById('paneWifi').className='pane'+(t==='wifi'?' on':'');
  document.getElementById('tabCopy').className='tab'+(t==='copy'?' on':'');
  document.getElementById('tabWifi').className='tab'+(t==='wifi'?' on':'');
  if(t==='wifi') wifiStatus();
}
async function act(a){ try{ await fetch('/api/'+a); }catch(e){} poll(); }
async function poll(){
  try{
    const d = await (await fetch('/api/status')).json();
    document.getElementById('uid').textContent = d.uid || '--';
    document.getElementById('msg').textContent = d.msg;
    const dot = document.getElementById('dot');
    dot.className = 'dot ' + (d.result==='busy'?'busy':d.result==='ok'?'ok':d.result==='err'?'err':'');
    const busy = d.result==='busy';
    document.getElementById('btnRead').disabled = busy;
    document.getElementById('btnWrite').disabled = busy || !d.hasStored;
  }catch(e){}
}
let scanning=false;
async function scan(){
  if(scanning) return;
  scanning=true;
  const btn=document.getElementById('btnScan'); btn.disabled=true; btn.textContent='Dang quet...';
  document.getElementById('netList').innerHTML='';
  let tries=0;
  const loop=async()=>{
    try{
      const d=await (await fetch('/api/scan')).json();
      if(d.status==='done'){
        render(d.networks); finish(); return;
      }
    }catch(e){}
    if(++tries>15){ finish(); return; }
    setTimeout(loop, 800);
  };
  const finish=()=>{ scanning=false; btn.disabled=false; btn.textContent='Quet mang WiFi'; };
  loop();
}
function render(nets){
  const box=document.getElementById('netList');
  if(!nets||!nets.length){ box.innerHTML='<div class="wifi-st">Khong tim thay mang nao.</div>'; return; }
  box.innerHTML='';
  nets.forEach(n=>{
    const d=document.createElement('div'); d.className='net';
    d.innerHTML='<div class="nm">'+(n.lock?'&#128274; ':'')+n.ssid+'</div><div class="meta">'+n.rssi+' dBm</div>';
    d.onclick=()=>{ document.getElementById('ssid').value=n.ssid; document.getElementById('connBox').style.display='block'; document.getElementById('pass').focus(); };
    box.appendChild(d);
  });
}
async function connect(){
  const ssid=document.getElementById('ssid').value, pass=document.getElementById('pass').value;
  document.getElementById('wifiSt').textContent='Dang ket noi toi '+ssid+'...';
  try{ await fetch('/api/connect?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)); }catch(e){}
  let t=0; const chk=async()=>{ await wifiStatus(); if(++t<12) setTimeout(chk,1500); }; chk();
}
async function forget(){
  document.getElementById('wifiSt').textContent='Dang quen WiFi...';
  try{ await fetch('/api/forget'); }catch(e){}
  wifiStatus();
}
async function wifiStatus(){
  try{
    const d=await (await fetch('/api/wifi')).json();
    document.getElementById('wifiSt').textContent = d.connected ? ('Da ket noi: '+d.ssid+'  |  IP: '+d.ip) : d.msg;
    document.getElementById('btnForget').style.display = (d.saved || d.connected) ? 'block' : 'none';
  }catch(e){}
}
setInterval(()=>{ if(document.getElementById('paneCopy').classList.contains('on')) poll(); }, 700);
poll();
</script>
</body>
</html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE_HTML); }

void handleStatus() {
  String uid = hasStored ? uidToString(storedUid, storedUidSize) : "";
  String json = "{";
  json += "\"uid\":\"" + uid + "\",";
  json += "\"hasStored\":" + String(hasStored ? "true" : "false") + ",";
  json += "\"result\":\"" + lastResult + "\",";
  json += "\"msg\":\"" + jsonEscape(statusMsg) + "\"}";
  server.send(200, "application/json", json);
}

void handleRead() {
  mode = WAIT_READ;
  deadline = millis() + WAIT_CARD_MS;
  statusMsg = "Dua THE GOC lai gan dau doc...";
  lastResult = "busy";
  flashLed(0, 0, 80, 200);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWrite() {
  if (!hasStored) {
    statusMsg = "Chua co UID. Doc the goc truoc.";
    lastResult = "err";
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  mode = WAIT_WRITE;
  deadline = millis() + WAIT_CARD_MS;
  statusMsg = "Dua THE MAGIC lai gan dau doc...";
  lastResult = "busy";
  flashLed(0, 0, 80, 200);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClear() {
  hasStored = false;
  storedUidSize = 0;
  mode = IDLE;
  statusMsg = "Da xoa UID. San sang.";
  lastResult = "idle";
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  String json = "{\"status\":\"done\",\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"lock\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  staSsid = ssid;
  staPass = pass;
  credsSaved = false;
  wifiMsg = "Dang ket noi toi " + ssid + "...";
  WiFi.begin(ssid.c_str(), pass.c_str());
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleForget() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  WiFi.disconnect(false, true);
  staSsid = "";
  staPass = "";
  credsSaved = false;
  wifiMsg = "Da quen WiFi da luu.";
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifi() {
  bool conn = (WiFi.status() == WL_CONNECTED);
  String json = "{";
  json += "\"connected\":" + String(conn ? "true" : "false") + ",";
  json += "\"ssid\":\"" + jsonEscape(staSsid) + "\",";
  json += "\"ip\":\"" + (conn ? WiFi.localIP().toString() : String("")) + "\",";
  json += "\"saved\":" + String(credsSaved ? "true" : "false") + ",";
  json += "\"msg\":\"" + jsonEscape(wifiMsg) + "\"}";
  server.send(200, "application/json", json);
}

void handleCaptive() { server.send_P(200, "text/html", PAGE_HTML); }

void startAP() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  apActive = true;
  Serial.print("Hotspot BAT. AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopAP() {
  if (!apActive) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("Hotspot TAT (da ket noi WiFi).");
}

void saveWifiCreds() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", staSsid);
  prefs.putString("pass", staPass);
  prefs.end();
  credsSaved = true;
}

void tryConnectSaved() {
  prefs.begin("wifi", true);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  prefs.end();
  if (s.length() > 0) {
    staSsid = s;
    staPass = p;
    credsSaved = true;
    wifiMsg = "Tu ket noi lai toi " + s + "...";
    WiFi.begin(s.c_str(), p.c_str());
    Serial.println("Dang tu ket noi WiFi da luu: " + s);
  }
}

void processCard() {
  if (mode == IDLE) return;
  if (millis() > deadline) {
    mode = IDLE;
    statusMsg = "Het thoi gian, khong thay the.";
    lastResult = "err";
    flashLed(255, 80, 0, 400);
    return;
  }
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  if (mode == WAIT_READ) {
    storedUidSize = rfid.uid.size;
    memcpy(storedUid, rfid.uid.uidByte, storedUidSize);
    hasStored = true;
    String u = uidToString(storedUid, storedUidSize);
    statusMsg = "Da doc UID: " + u + (storedUidSize != 4 ? " (canh bao: khong phai 4 byte)" : "");
    lastResult = "ok";
    flashLed(0, 255, 0, 500);
    mode = IDLE;
  } else if (mode == WAIT_WRITE) {
    if (rfid.MIFARE_SetUid(storedUid, storedUidSize, true)) {
      statusMsg = "Clone thanh cong! (the la magic card)";
      lastResult = "ok";
      flashLed(0, 255, 0, 700);
    } else {
      statusMsg = "Ghi that bai: the KHONG phai magic card.";
      lastResult = "err";
      flashLed(255, 0, 0, 700);
    }
    mode = IDLE;
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void setup() {
  Serial.begin(115200);
  delay(800);

  pixels.begin();
  pixels.setBrightness(60);
  setLed(0, 0, 0);

  SPI.begin(RC522_SCK, RC522_MISO, RC522_MOSI, RC522_SS);
  rfid.PCD_Init();
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print("RC522 VersionReg = 0x");
  Serial.println(v, HEX);

  WiFi.mode(WIFI_STA);

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/read", handleRead);
  server.on("/api/write", handleWrite);
  server.on("/api/clear", handleClear);
  server.on("/api/scan", handleScan);
  server.on("/api/connect", handleConnect);
  server.on("/api/forget", handleForget);
  server.on("/api/wifi", handleWifi);

  server.on("/generate_204", handleCaptive);
  server.on("/gen_204", handleCaptive);
  server.on("/hotspot-detect.html", handleCaptive);
  server.on("/library/test/success.html", handleCaptive);
  server.on("/connecttest.txt", handleCaptive);
  server.on("/ncsi.txt", handleCaptive);
  server.onNotFound(handleCaptive);

  server.begin();
  Serial.println("Web server da chay.");

  bootTime = millis();
  tryConnectSaved();

  if (staSsid.length() == 0) {
    Serial.println("Chua co WiFi luu -> bat hotspot ngay de cau hinh.");
    startAP();
  } else {
    Serial.println("Co WiFi luu -> thu ket noi, bat hotspot sau 30s neu that bai.");
  }
  setLed(0, 20, 30);
}

void loop() {
  if (apActive) dnsServer.processNextRequest();
  server.handleClient();
  processCard();

  if (WiFi.status() == WL_CONNECTED) {
    if (!credsSaved && staSsid.length() > 0) {
      saveWifiCreds();
      Serial.println("Da luu WiFi vao bo nho: " + staSsid);
    }
    if (wifiMsg.indexOf("Da ket noi") < 0) {
      wifiMsg = "Da ket noi: " + staSsid;
    }
    if (apActive) stopAP();
  } else {
    if (!apActive && millis() - bootTime > AP_FALLBACK_MS) {
      Serial.println("Khong ket noi duoc WiFi sau 30s -> bat hotspot.");
      startAP();
    }
  }

  if (ledUntil != 0 && millis() > ledUntil) {
    ledUntil = 0;
    setLed(0, 20, 30);
  }
}

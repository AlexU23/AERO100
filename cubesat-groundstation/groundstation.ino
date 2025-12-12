#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <LoRa.h>

// ---------- WiFi ----------
const char* ssid     = "JordyKam";
const char* password = "PirateKingLuffy#2451";

// ---------- LoRa pins ----------
#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19
#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15
#define LORA_FREQ  915E6

WebServer server(80);

// ---------- Last RX ----------
String lastRxRaw = "None";
long   lastRxRSSI = 0;
unsigned long lastRxMs = 0;
unsigned long rxCount = 0;

// Parsed fields
String ctrS   = "-";
String pitchS = "-";
String rollS  = "-";
String axS = "-", ayS = "-", azS = "-";
String gxS = "-", gyS = "-", gzS = "-";
String tCS = "-", tFS = "-";
String magS = "-";
String rwS  = "-";

// RX log ring buffer
static const int RX_LOG_MAX = 12;
String rxLog[RX_LOG_MAX];
int rxLogCount = 0;
int rxLogHead  = 0;

static void addToRxLog(const String& line) {
  rxLog[rxLogHead] = line;
  rxLogHead = (rxLogHead + 1) % RX_LOG_MAX;
  if (rxLogCount < RX_LOG_MAX) rxLogCount++;
}

static String sanitizeRx(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c >= 32 && c <= 126) out += c;
  }
  out.trim();
  return out;
}

// ---------- Command TX ----------
void sendCommand(const String& cmd) {
  LoRa.beginPacket();
  LoRa.print(cmd);
  LoRa.endPacket();
  LoRa.receive();   // <<< CRITICAL: return to RX mode
  Serial.print("TX CMD: ");
  Serial.println(cmd);
}

// /cmd?mag=1  or  /cmd?rw=0
void handleCmd() {
  bool sent = false;

  if (server.hasArg("mag")) {
    String v = server.arg("mag"); v.trim();
    if (v == "0" || v == "1") {
      sendCommand("CMD:MAG=" + v + ";");
      sent = true;
    }
  }

  if (server.hasArg("rw")) {
    String v = server.arg("rw"); v.trim();
    if (v == "0" || v == "1") {
      sendCommand("CMD:RW=" + v + ";");
      sent = true;
    }
  }

  if (!sent) {
    server.send(400, "text/plain",
      "Bad cmd. Use /cmd?mag=0|1 or /cmd?rw=0|1");
    return;
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

// ---------- Telemetry parser ----------
// IMU,<ctr>,<pitch>,<roll>,<AcX>,<AcY>,<AcZ>,<GyX>,<GyY>,<GyZ>,<tC>,<tF>,MAG,<0/1>,RW,<0/1>
static bool parseIMUTelemetry(const String& msg) {
  if (!msg.startsWith("IMU,")) return false;

  String toks[32];
  int nt = 0;
  int start = 0;

  while (nt < 32) {
    int comma = msg.indexOf(',', start);
    if (comma < 0) {
      toks[nt++] = msg.substring(start);
      break;
    } else {
      toks[nt++] = msg.substring(start, comma);
      start = comma + 1;
    }
  }

  if (nt < 16) return false;

  ctrS   = toks[1];
  pitchS = toks[2];
  rollS  = toks[3];
  axS    = toks[4];
  ayS    = toks[5];
  azS    = toks[6];
  gxS    = toks[7];
  gyS    = toks[8];
  gzS    = toks[9];
  tCS    = toks[10];
  tFS    = toks[11];

  if (toks[12] != "MAG") return false;
  if (toks[14] != "RW")  return false;

  magS = (toks[13] == "1") ? "ON" : "OFF";
  rwS  = (toks[15] == "1") ? "ENABLED" : "DISABLED";

  return true;
}

String createHTML() {
  unsigned long age = (lastRxMs == 0) ? 0 : (millis() - lastRxMs);

  String str = "<!DOCTYPE html><html><head>";
  str += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  str += "<meta http-equiv=\"refresh\" content=\"2\">";
  str += "<title>Telemetry Dashboard</title>";
  str += "<style>";
  str += "body{font-family:Arial;color:#f0f0f0;background:#0b1020;text-align:center;}";
  str += ".card{background:#151a2c;padding:20px;margin:18px auto;max-width:900px;border-radius:12px;}";
  str += ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;}";
  str += ".box{padding:10px;border-radius:10px;background:#0f1324;text-align:left;}";
  str += ".mono{font-family:monospace;}";
  str += "button{padding:10px;margin:6px;border-radius:10px;}";
  str += "</style></head><body>";

  str += "<h1>Telemetry Dashboard</h1>";

  str += "<div class='card'><h2>Controls</h2>";
  str += "<a href='/cmd?mag=1'><button>MAG ON</button></a>";
  str += "<a href='/cmd?mag=0'><button>MAG OFF</button></a><br>";
  str += "<a href='/cmd?rw=1'><button>RW ENABLE</button></a>";
  str += "<a href='/cmd?rw=0'><button>RW DISABLE</button></a>";
  str += "</div>";

  str += "<div class='card'><h2>Telemetry</h2><div class='grid'>";
  str += "<div class='box'>CTR<br><span class='mono'>" + ctrS + "</span></div>";
  str += "<div class='box'>Pitch<br><span class='mono'>" + pitchS + "</span></div>";
  str += "<div class='box'>Roll<br><span class='mono'>" + rollS + "</span></div>";
  str += "<div class='box'>MAG<br><span class='mono'>" + magS + "</span></div>";
  str += "<div class='box'>RW<br><span class='mono'>" + rwS + "</span></div>";
  str += "<div class='box'>RSSI<br><span class='mono'>" + String(lastRxRSSI) + "</span></div>";
  str += "</div>";
  str += "<p>RX count: " + String(rxCount) +
         " | Last age: " + String(age) + " ms</p>";
  str += "<p class='mono'>" + lastRxRaw + "</p>";
  str += "</div>";

  str += "</body></html>";
  return str;
}

void handleRoot() {
  server.send(200, "text/html", createHTML());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    while (true);
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.setPreambleLength(8);
  LoRa.receive();     // <<< START IN RX MODE

  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.begin();

  Serial.println("Groundstation ready.");
}

void loop() {
  server.handleClient();

  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  String rx;
  while (LoRa.available()) rx += (char)LoRa.read();

  lastRxRSSI = LoRa.packetRssi();
  lastRxMs = millis();
  rxCount++;

  lastRxRaw = sanitizeRx(rx);
  bool ok = parseIMUTelemetry(lastRxRaw);

  addToRxLog((ok ? "" : "UNPARSED: ") + lastRxRaw +
             " (RSSI " + String(lastRxRSSI) + ")");

  Serial.println("RX: " + lastRxRaw);
}

// Captive portal with SPIFFS + SD card file serving
// Adapted from https://git.vvvvvvaria.org/then/ESP8266-captive-ota-spiffs

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include "./DNSServer.h"
#include <FS.h>    // SPIFFS
#include <SPI.h>
#include <SD.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;

ESP8266WebServer server(80);

#ifndef STASSID
#define STASSID "test"
#endif

IPAddress apIP(192, 168, 4, 1);
const char* ssid = STASSID;

const int SD_CS_PIN = D8;

bool sdAvailable = false;

const String pageStyle =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<style>"
  "  body { font-family: sans-serif; max-width: 700px; margin: 0 auto;"
  "         padding: 1rem; background: #f5f5f5; color: #222; }"
  "  h1   { font-size: 1.5rem; margin-bottom: .25rem; }"
  "  h2   { font-size: 1.3rem; border-bottom: 2px solid #ccc;"
  "         padding-bottom: .4rem; margin-bottom: 1rem; }"
  "  h3   { font-size: 1rem; margin: 0 0 .3rem 0; color: #444; }"
  "  p    { margin: 0 0 .5rem 0; line-height: 1.5; }"
  "  ul   { list-style: none; padding: 0; margin: 0; }"
  "  li   { background: #fff; border-radius: 8px; margin-bottom: .5rem;"
  "         padding: .75rem 1rem; box-shadow: 0 1px 3px rgba(0,0,0,.1); }"
  "  li a { text-decoration: none; color: #0066cc; font-size: 1rem; }"
  "  li a:active { color: #004499; }"
  "  .meta  { font-size: .8rem; color: #888; margin-top: .2rem; }"
  "  .back  { display:inline-block; margin-bottom:1rem; color:#0066cc;"
  "           text-decoration:none; font-size:.9rem; }"
  "  .card  { background:#fff; border-radius:8px; padding:1rem;"
  "           box-shadow:0 1px 3px rgba(0,0,0,.1); margin-bottom:1rem; }"
  "  .browse-btn { display:block; text-align:center; background:#0066cc;"
  "               color:#fff !important; text-decoration:none;"
  "               font-size:1.3rem; font-weight:bold; padding:1.1rem;"
  "               border-radius:10px; margin:1.5rem 0;"
  "               box-shadow:0 2px 6px rgba(0,0,0,.2); }"
  "  .browse-btn:active { background:#004499; }"
  "  .subtitle { color:#888; font-size:.9rem; margin-top:.1rem; }"
  "</style>";

String responseHTML =
  pageStyle +
  "<title>The Library</title></head><body>"
  "<h1>📚 The Library</h1>"
  "<p class='subtitle'>An offline, wireless reading room</p>"
  "<a class='browse-btn' href='/sd'>Browse the Library →</a>"
  "<div class='card'>"
  "<h3>What is this?</h3>"
  "<p>You've connected to a small device hosting a collection of documents "
  "and files. No internet connection is needed — everything is stored locally.</p>"
  "</div>"
  "<div class='card'>"
  "<h3>How do I use it?</h3>"
  "<p>Tap <b>Browse the Library</b> above to explore the available files. "
  "PDFs and web pages will open directly in your browser.</p>"
  "</div>"
  "<div class='card'>"
  "<h3>Do I need an app?</h3>"
  "<p>No. This works entirely in your browser — nothing to install.</p>"
  "</div>"
  "<div class='card'>"
  "<h3>Will this use my data?</h3>"
  "<p>No. This device has no connection to the internet. "
  "Your browsing here is completely local and private.</p>"
  "</div>"
  "</body></html>";


// ─── Content Type ────────────────────────────────────────────────────────────

String getContentType(String filename) {
  if (server.hasArg("download"))       return "application/octet-stream";
  else if (filename.endsWith(".htm"))  return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css"))  return "text/css";
  else if (filename.endsWith(".js"))   return "application/javascript";
  else if (filename.endsWith(".png"))  return "image/png";
  else if (filename.endsWith(".gif"))  return "image/gif";
  else if (filename.endsWith(".jpg"))  return "image/jpeg";
  else if (filename.endsWith(".ico"))  return "image/x-icon";
  else if (filename.endsWith(".xml"))  return "text/xml";
  else if (filename.endsWith(".mp4"))  return "video/mp4";
  else if (filename.endsWith(".pdf"))  return "application/pdf";
  else if (filename.endsWith(".zip"))  return "application/x-zip";
  else if (filename.endsWith(".gz"))   return "application/x-gzip";
  return "text/plain";
}


// ─── meta.txt Parser ─────────────────────────────────────────────────────────

// Read a key from /dirName/meta.txt on the SD root
// dirName is the raw 8.3 name e.g. "MOBYDI~1"
String readMeta(String dirName, String key) {
  String path = "/" + dirName + "/meta.txt";
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return "";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int sep = line.indexOf('=');
    if (sep > 0 && line.substring(0, sep) == key) {
      f.close();
      return line.substring(sep + 1);
    }
  }
  f.close();
  return "";
}


// ─── SPIFFS File Serving ─────────────────────────────────────────────────────

bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz)) path += ".gz";
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}


// ─── SD Card Serving ─────────────────────────────────────────────────────────

// Serve a file from the SD card using its exact path e.g. "/MOBYDI~1/book.pdf"
bool handleSDFileRead(String sdPath) {
  if (!sdAvailable) {
    server.send(503, "text/plain", "SD card not available");
    return false;
  }

  // Ensure single leading slash
  while (sdPath.startsWith("//")) sdPath = sdPath.substring(1);
  if (!sdPath.startsWith("/")) sdPath = "/" + sdPath;

  File file = SD.open(sdPath.c_str(), FILE_READ);
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "Could not open: [" + sdPath + "]");
    file.close();
    return false;
  }

  server.sendHeader("Content-Length", String(file.size()));
  server.streamFile(file, getContentType(sdPath));
  file.close();
  return true;
}

// List all book directories on the SD root
void handleSDList() {
  if (!sdAvailable) {
    server.send(503, "text/plain", "SD card not available");
    return;
  }

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    server.send(503, "text/plain", "SD card not readable");
    return;
  }

  String output = pageStyle + "<title>Library</title></head><body>";
  output += "<a class='back' href='/'>← Home</a>";
  output += "<h2>📚 Library</h2>";
  output += "<ul>";

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    // Only process directories, skip loose files and hidden entries
    if (!entry.isDirectory()) { entry.close(); continue; }

    String dirName = String(entry.name());
    if (dirName.lastIndexOf('/') >= 0)
      dirName = dirName.substring(dirName.lastIndexOf('/') + 1);
    if (dirName.startsWith(".")) { entry.close(); continue; }

    // Read display info from meta.txt using the 8.3 dir name
    String title  = readMeta(dirName, "title");
    String author = readMeta(dirName, "author");
    if (title.isEmpty()) title = dirName; // fallback to raw name

    // Prefer book.pdf, fall back to book.htm
    String contentFile = "";
    String pdfPath = "/" + dirName + "/book.pdf";
    String htmPath = "/" + dirName + "/book.htm";
    if (SD.exists(pdfPath.c_str()))      contentFile = "book.pdf";
    else if (SD.exists(htmPath.c_str())) contentFile = "book.htm";

    if (contentFile.isEmpty()) { entry.close(); continue; } // skip if no content

    String href = "/sd/" + dirName + "/" + contentFile;

    output += "<li><a href='" + href + "'>" + title + "</a>";
    if (!author.isEmpty())
      output += "<div class='meta'>by " + author + "</div>";
    output += "</li>";

    entry.close();
  }

  root.close();
  output += "</ul></body></html>";
  server.send(200, "text/html", output);
}


// ─── SD Routing ──────────────────────────────────────────────────────────────

void handleSD() {
  String uri = server.uri();

  if (uri == "/sd" || uri == "/sd/") {
    handleSDList();
    return;
  }

  // Strip "/sd" prefix → e.g. "/MOBYDI~1/book.pdf"
  String sdPath = uri.substring(3);
  handleSDFileRead(sdPath);
}


// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  Serial.print("Initializing SD card... ");
  if (SD.begin(SD_CS_PIN)) {
    sdAvailable = true;
    Serial.println("SD card ready.");
  } else {
    Serial.println("SD card init failed — continuing without it.");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid);
  dnsServer.start(DNS_PORT, "*", apIP);

  MDNS.begin("esp8266", WiFi.softAPIP());
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  SPIFFS.begin();

  server.onNotFound([]() {
    String uri = server.uri();
    if (uri.startsWith("/sd")) {
      handleSD();
    } else if (!handleFileRead(uri)) {
      server.send(200, "text/html", responseHTML);
    }
  });

  server.begin();
}


// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(50);
}
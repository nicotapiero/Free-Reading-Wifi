// Captive portal with SPIFFS + SD card file serving
// Now with precomputed SD index + cached HTML

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include "./DNSServer.h"
#include <FS.h>    // SPIFFS
#include <SPI.h>
#include <SD.h>
#include <vector>

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

// ─── Library Struct + Cache ────────────────────────────────────────────────

struct BookEntry {
  String dirName;
  String title;
  String author;
  String contentFile;
};

std::vector<BookEntry> library;
String cachedLibraryHTML;

// ─── UI HTML ───────────────────────────────────────────────────────────────

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
  "<div class='card'><h3>What is this?</h3><p>Offline library.</p></div>"
  "</body></html>";


// ─── Content Type ───────────────────────────────────────────────────────────

String getContentType(String filename) {
  if (server.hasArg("download"))       return "application/octet-stream";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css"))  return "text/css";
  else if (filename.endsWith(".js"))   return "application/javascript";
  else if (filename.endsWith(".png"))  return "image/png";
  else if (filename.endsWith(".jpg"))  return "image/jpeg";
  else if (filename.endsWith(".pdf"))  return "application/pdf";
  return "text/plain";
}


// ─── meta.txt reader ────────────────────────────────────────────────────────

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


// ─── Build Library Index (NEW) ──────────────────────────────────────────────

void buildLibraryIndex() {
  if (!sdAvailable) return;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) return;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    String dirName = String(entry.name());
    if (dirName.lastIndexOf('/') >= 0)
      dirName = dirName.substring(dirName.lastIndexOf('/') + 1);

    if (dirName.startsWith(".")) {
      entry.close();
      continue;
    }

    BookEntry book;
    book.dirName = dirName;
    book.title  = readMeta(dirName, "title");
    book.author = readMeta(dirName, "author");

    if (book.title.isEmpty()) book.title = dirName;

    String pdfPath = "/" + dirName + "/book.pdf";
    String htmPath = "/" + dirName + "/book.htm";

    if (SD.exists(pdfPath.c_str()))
      book.contentFile = "book.pdf";
    else if (SD.exists(htmPath.c_str()))
      book.contentFile = "book.htm";
    else {
      entry.close();
      continue;
    }

    library.push_back(book);
    entry.close();
  }

    root.close();

    // ─── SORT HERE ─────────────────────────────────────
    std::sort(library.begin(), library.end(), [](const BookEntry &a, const BookEntry &b) {
      return a.dirName < b.dirName;
    });

    // ─── BUILD CACHED HTML ─────────────────────────────
    cachedLibraryHTML = pageStyle + "<title>Library</title></head><body>";
    cachedLibraryHTML += "<a class='back' href='/'>← Home</a>";
    cachedLibraryHTML += "<h2>📚 Library</h2><ul>";

    for (auto &book : library) {
      String href = "/sd/" + book.dirName + "/" + book.contentFile;

      cachedLibraryHTML += "<li><a href='" + href + "'>" + book.title + "</a>";
      if (!book.author.isEmpty())
        cachedLibraryHTML += "<div class='meta'>by " + book.author + "</div>";
      cachedLibraryHTML += "</li>";
    }

    cachedLibraryHTML += "</ul></body></html>";

  // Pre-render HTML
  cachedLibraryHTML = pageStyle + "<title>Library</title></head><body>";
  cachedLibraryHTML += "<a class='back' href='/'>← Home</a>";
  cachedLibraryHTML += "<h2>📚 Library</h2><ul>";

  for (auto &book : library) {
    String href = "/sd/" + book.dirName + "/" + book.contentFile;

    cachedLibraryHTML += "<li><a href='" + href + "'>" + book.title + "</a>";
    if (!book.author.isEmpty())
      cachedLibraryHTML += "<div class='meta'>by " + book.author + "</div>";
    cachedLibraryHTML += "</li>";
  }

  cachedLibraryHTML += "</ul></body></html>";
}


// ─── SD Handlers ────────────────────────────────────────────────────────────

bool handleSDFileRead(String sdPath) {
  if (!sdAvailable) {
    server.send(503, "text/plain", "SD not available");
    return false;
  }

  if (!sdPath.startsWith("/")) sdPath = "/" + sdPath;

  File file = SD.open(sdPath.c_str(), FILE_READ);
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "Not found");
    return false;
  }

  server.streamFile(file, getContentType(sdPath));
  file.close();
  return true;
}

void handleSD() {
  String uri = server.uri();

  if (uri == "/sd" || uri == "/sd/") {
    server.send(200, "text/html", cachedLibraryHTML);
    return;
  }

  String sdPath = uri.substring(3);
  handleSDFileRead(sdPath);
}


// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  if (SD.begin(SD_CS_PIN)) {
    sdAvailable = true;
    buildLibraryIndex();
    Serial.printf("Indexed %d books\n", library.size());
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(ssid);

  dnsServer.start(DNS_PORT, "*", apIP);

  SPIFFS.begin();

  server.onNotFound([]() {
    String uri = server.uri();
    if (uri.startsWith("/sd")) handleSD();
    else server.send(200, "text/html", responseHTML);
  });

  server.begin();
}


// ─── Loop ───────────────────────────────────────────────────────────────────

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}

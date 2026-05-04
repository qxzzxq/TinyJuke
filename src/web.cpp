#include "web.h"
#include "tags.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

static WebServer server(80);

// ----------------------------------------------------------------
//  HTML fragments
// ----------------------------------------------------------------

static const char PAGE_HEAD[] = R"(
<!DOCTYPE html><html><head>
<title>Jukebox</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
 body{font-family:sans-serif;max-width:600px;margin:0 auto;padding:10px;background:#111;color:#eee}
 h1{color:#4f4}h2{color:#6c6;margin-top:24px}
 table{border-collapse:collapse;width:100%}
 td,th{border:1px solid #333;padding:4px 8px;font-size:14px}
 th{background:#222}
 input,select,button{padding:6px 8px;margin:2px;border:1px solid #555;border-radius:4px;background:#222;color:#eee}
 button{background:#393;color:#fff;cursor:pointer}
 button.danger{background:#933}
 .msg{padding:8px;margin:8px 0;border-radius:4px}
 .ok{background:#262;border:1px solid #4a4}
 .err{background:#622;border:1px solid #a44}
</style></head><body>
<h1>ESP Jukebox</h1>
)";

static const char PAGE_FOOT[] = "</body></html>";

// ----------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------

static void serveOK(const char *redirect) {
  server.sendHeader("Location", redirect, true);
  server.send(302, "text/plain", "OK");
}

static void serveMsg(const char *cls, const char *text) {
  server.send(200, "text/html",
    String(PAGE_HEAD)
    + "<div class='msg " + cls + "'>" + text + "</div>"
    + "<p><a href='/'>Back</a></p>"
    + PAGE_FOOT);
}

static String escapeHTML(const char *s) {
  String out;
  while (*s) {
    if      (*s == '<')  out += "&lt;";
    else if (*s == '>')  out += "&gt;";
    else if (*s == '&')  out += "&amp;";
    else if (*s == '"')  out += "&quot;";
    else                 out += *s;
    s++;
  }
  return out;
}

// ----------------------------------------------------------------
//  Main page: file list + tag mappings + upload form
// ----------------------------------------------------------------

static void handleRoot() {
  String html = PAGE_HEAD;

  // --- Upload section ---
  html += "<h2>Upload WAV</h2>"
          "<form action='/upload' method='POST' enctype='multipart/form-data'>"
          "<input type='file' name='file' accept='.wav'> "
          "<button type='submit'>Upload</button></form>";

  // --- File list ---
  html += "<h2>Music Files</h2><ul>";
  File dir = SD.open("/music");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        const char *name = f.name();
        // Strip leading /music/ if present
        const char *base = strrchr(name, '/');
        if (base) base++; else base = name;
        html += "<li>" + escapeHTML(base)
             +  " <a href='/deletefile?name=" + String(base)
             +  "' onclick='return confirm(\"Delete "
             +  escapeHTML(base) + "?\")' "
             +  "style='color:#f66;font-size:12px'>[delete]</a></li>";
      }
      f.close();
    }
    dir.close();
  } else {
    html += "<li>No files found</li>";
  }
  html += "</ul>";

  // --- Tag mappings ---
  html += "<h2>Tag Mappings</h2>"
          "<table><tr><th>UID</th><th>File</th><th>Title</th><th></th></tr>";

  if (tagDoc.size() > 0) {
    for (JsonPair kv : tagDoc.as<JsonObject>()) {
      const char *uid    = kv.key().c_str();
      const char *file   = kv.value()["file"]   | "";
      const char *title  = kv.value()["title"]  | "";
      html += "<tr><td>" + escapeHTML(uid) + "</td>"
              "<td>" + escapeHTML(file) + "</td>"
              "<td>" + escapeHTML(title) + "</td>"
              "<td><a href='/deltag?uid=" + String(uid)
              + "' onclick='return confirm(\"Unlink " + String(uid) + "?\")' "
              + "style='color:#f66;font-size:12px'>[unlink]</a></td></tr>";
    }
  } else {
    html += "<tr><td colspan=4>No tags mapped</td></tr>";
  }
  html += "</table>";

  // --- Add tag form ---
  html += "<h3>Link New Tag</h3>"
          "<form action='/addtag' method='POST'>"
          "<input type='text' name='uid' placeholder='AA:BB:CC:DD' "
          "pattern='[0-9A-Fa-f:]+' required style='width:160px'><br>"
          "<select name='file' required><option value=''>-- file --</option>";

  dir = SD.open("/music");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        const char *name = f.name();
        const char *base = strrchr(name, '/');
        if (base) base++; else base = name;
        html += "<option value='music/" + String(base) + "'>" + escapeHTML(base) + "</option>";
      }
      f.close();
    }
    dir.close();
  }
  html += "</select><br>"
          "<input type='text' name='title' placeholder='Title (optional)' style='width:160px'><br>"
          "<input type='text' name='artist' placeholder='Artist (optional)' style='width:160px'><br>"
          "<input type='text' name='album' placeholder='Album (optional)' style='width:160px'><br>"
          "<input type='text' name='img' placeholder='Album art filename (optional)' style='width:160px'><br>"
          "<button type='submit'>Link</button></form>";

  html += PAGE_FOOT;
  server.send(200, "text/html", html);
}

// ----------------------------------------------------------------
//  POST handlers
// ----------------------------------------------------------------

static void handleUpload() {
  HTTPUpload &up = server.upload();
  static File uploadFile;

  if (up.status == UPLOAD_FILE_START) {
    String path = "/music/" + up.filename;
    // Sanitize: only keep alphanumeric, dash, underscore, dot
    for (size_t i = 0; i < path.length(); i++) {
      char c = path[i];
      if (!isalnum(c) && c != '-' && c != '_' && c != '.' && c != '/')
        path[i] = '_';
    }
    uploadFile = SD.open(path, FILE_WRITE);
    if (!uploadFile)
      Serial.printf("Upload open failed: %s\n", path.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile)
      uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      serveOK("/");
    } else {
      serveMsg("err", "Upload failed — could not write to SD card.");
    }
  }
}

static void handleAddTag() {
  String uid    = server.arg("uid");
  String file   = server.arg("file");
  String title  = server.arg("title");
  String artist = server.arg("artist");
  String album  = server.arg("album");
  String img    = server.arg("img");
  uid.toUpperCase();

  if (uid.length() < 2 || file.length() < 2) {
    serveMsg("err", "Missing UID or file.");
    return;
  }

  tagDoc[uid]["file"] = file;
  if (title.length()  > 0) tagDoc[uid]["title"]  = title;
  if (artist.length() > 0) tagDoc[uid]["artist"] = artist;
  if (album.length()  > 0) tagDoc[uid]["album"]  = album;
  if (img.length()    > 0) tagDoc[uid]["img"]    = img;

  if (SD.exists("/tags.json")) SD.remove("/tags.json");
  File f = SD.open("/tags.json", FILE_WRITE);
  if (f) {
    serializeJson(tagDoc, f);
    f.close();
    serveOK("/");
  } else {
    serveMsg("err", "Failed to save tags.json to SD card.");
  }
}

static void handleDeleteTag() {
  String uid = server.arg("uid");
  uid.toUpperCase();

  if (tagDoc[uid].isNull()) {
    serveMsg("err", "Tag not found.");
    return;
  }

  tagDoc.remove(uid);

  if (SD.exists("/tags.json")) SD.remove("/tags.json");
  File f = SD.open("/tags.json", FILE_WRITE);
  if (f) {
    serializeJson(tagDoc, f);
    f.close();
    serveOK("/");
  } else {
    serveMsg("err", "Failed to save tags.json.");
  }
}

static void handleDeleteFile() {
  String name = server.arg("name");
  // Sanitize to prevent directory traversal
  for (size_t i = 0; i < name.length(); i++) {
    if (!isalnum(name[i]) && name[i] != '-' && name[i] != '_' && name[i] != '.') {
      serveMsg("err", "Invalid filename.");
      return;
    }
  }

  String path = "/music/" + name;
  if (SD.exists(path)) {
    SD.remove(path);
    serveOK("/");
  } else {
    serveMsg("err", "File not found.");
  }
}

// ----------------------------------------------------------------
//  Public API
// ----------------------------------------------------------------

void initWebServer() {
  Serial.println("Starting web server...");
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/upload",     HTTP_POST, []{ serveOK("/"); }, handleUpload);
  server.on("/addtag",     HTTP_POST, handleAddTag);
  server.on("/deltag",     HTTP_POST, handleDeleteTag);
  server.on("/deletefile", HTTP_POST, handleDeleteFile);

  server.begin();
  Serial.println("Web server started.");
}

void stopWebServer() {
  Serial.println("Stopping web server...");
  server.stop();
  WiFi.softAPdisconnect(true);
  Serial.println("Web server stopped.");
}

void handleWebClient() {
  server.handleClient();
}

int getWebConnectionCount() {
  return WiFi.softAPgetStationNum();
}

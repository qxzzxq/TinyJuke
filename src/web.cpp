#include "web.h"
#include "tags.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

static WebServer server(80);

// ================================================================
//  HTML Page (complete SPA)
// ================================================================

static const char PAGE_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jukebox · Tags</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0A0E1A;color:#eee;min-height:100vh}
#app{max-width:640px;margin:0 auto;padding:16px 16px 32px}
header{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:20px}
h1{font-size:22px;color:#4ADE80;font-weight:700}
#stats{font-size:13px;color:#6B7B8D}

#tag-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px}
@media(max-width:480px){#tag-grid{grid-template-columns:1fr}}
.tag-card{background:#111A2E;border-radius:10px;overflow:hidden;cursor:pointer;transition:transform .15s,box-shadow .15s;border:1px solid #1E293B}
.tag-card:hover{transform:translateY(-2px);box-shadow:0 4px 20px rgba(0,0,0,.5);border-color:#4ADE80}
.tag-card .card-img{width:100%;height:110px;object-fit:cover;background:#1a1f2e;display:block}
.tag-card .card-img-placeholder{width:100%;height:110px;background:linear-gradient(135deg,#1a1f2e 0%,#1E293B 100%);display:flex;align-items:center;justify-content:center;font-size:36px;color:#334155}
.tag-card .card-body{padding:10px 12px}
.tag-card .card-uid{font-family:'SF Mono',ui-monospace,monospace;font-size:10px;color:#6B7B8D;margin-bottom:4px;word-break:break-all}
.tag-card .card-title{font-size:15px;font-weight:600;color:#fff;margin-bottom:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tag-card .card-artist{font-size:13px;color:#94a3b8;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

.empty-state{grid-column:1/-1;text-align:center;padding:48px 16px;color:#6B7B8D}
.empty-state .empty-icon{font-size:48px;margin-bottom:12px}
.empty-state p{font-size:15px;margin-bottom:4px}
.empty-state .empty-hint{font-size:13px;color:#455A6E}

#pagination{display:flex;justify-content:center;align-items:center;gap:6px;margin-bottom:20px}
#pagination button{min-width:36px;height:36px;border:1px solid #1E293B;border-radius:8px;background:#111A2E;color:#eee;cursor:pointer;font-size:14px;transition:background .1s}
#pagination button:hover:not(:disabled){background:#1E293B}
#pagination button.active{background:#4ADE80;color:#0A0E1A;border-color:#4ADE80;font-weight:600}
#pagination button:disabled{opacity:.3;cursor:default}

#actions{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}
#actions button{padding:10px 24px;border-radius:8px;border:none;cursor:pointer;font-size:15px;font-weight:500;transition:background .15s}
#btn-add{background:#4ADE80;color:#0A0E1A}
#btn-add:hover{background:#6ee79a}
#btn-upload{background:#111A2E;color:#eee;border:1px solid #1E293B}
#btn-upload:hover{background:#1E293B}

#upload-panel{display:none;margin-top:16px;padding:16px;background:#111A2E;border-radius:10px;border:1px solid #1E293B}
#upload-panel label{font-size:13px;color:#6B7B8D;display:block;margin-bottom:8px}
#upload-panel input[type=file]{color:#eee;margin-bottom:8px;width:100%}
#upload-progress{width:100%;height:6px;display:none;accent-color:#4ADE80}

.modal-overlay{position:fixed;inset:0;background:rgba(0,0,0,.75);display:flex;align-items:center;justify-content:center;z-index:100;padding:16px}
.modal-box{background:#111A2E;border-radius:12px;width:100%;max-width:420px;max-height:90vh;overflow-y:auto;border:1px solid #1E293B;animation:modalIn .2s ease}
@keyframes modalIn{from{opacity:0;transform:scale(.95)translateY(8px)}to{opacity:1;transform:scale(1)translateY(0)}}
.modal-box h2{font-size:18px;color:#4ADE80;padding:16px 16px 0}
.modal-body{padding:16px;display:flex;flex-direction:column;gap:10px}
.modal-body .modal-img{width:100%;max-height:180px;object-fit:contain;background:#0a0e15;border-radius:8px;margin-bottom:2px}
.modal-body .modal-img-placeholder{width:100%;height:120px;background:linear-gradient(135deg,#0a0e15 0%,#1E293B 100%);border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:40px;color:#334155;margin-bottom:2px}
.modal-body label{font-size:11px;color:#6B7B8D;text-transform:uppercase;letter-spacing:.6px;margin-bottom:-6px}
.modal-body input,.modal-body select{padding:8px 10px;border:1px solid #1E293B;border-radius:6px;background:#0A0E1A;color:#eee;font-size:14px;width:100%}
.modal-body input:focus,.modal-body select:focus{outline:none;border-color:#4ADE80}
.modal-body input[readonly]{color:#6B7B8D;cursor:default}
.modal-footer{display:flex;gap:8px;padding:0 16px 16px;justify-content:flex-end}
.modal-footer button{padding:8px 18px;border-radius:6px;border:none;cursor:pointer;font-size:14px;font-weight:500;transition:background .15s}
#btn-save{background:#4ADE80;color:#0A0E1A}
#btn-save:hover{background:#6ee79a}
#btn-cancel{background:#1E293B;color:#eee}
#btn-cancel:hover{background:#2d3a4f}
#btn-remove{background:none;color:#F87171;margin-right:auto}
#btn-remove:hover{background:#2a1515}

#toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);z-index:200;display:flex;flex-direction:column;align-items:center;gap:8px;pointer-events:none}
.toast-msg{padding:10px 20px;border-radius:8px;font-size:14px;text-align:center;animation:toastIn .25s ease;pointer-events:auto;max-width:90vw}
.toast-ok{background:#14532d;color:#86efac;border:1px solid #166534}
.toast-err{background:#450a0a;color:#fca5a5;border:1px solid #7f1d1d}
@keyframes toastIn{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:translateY(0)}}
</style>
</head>
<body>
<div id="app">
<header><h1>Jukebox</h1><div id="stats"></div></header>
<div id="tag-grid"></div>
<div id="pagination"></div>
<div id="actions">
<button id="btn-add">+ Add Tag</button>
<button id="btn-upload">Upload WAV</button>
</div>
<div id="upload-panel">
<label>Select a WAV file to upload to the SD card:</label>
<input type="file" id="file-input" accept=".wav,.WAV">
<progress id="upload-progress" max="100" value="0"></progress>
</div>
</div>

<div id="modal" class="modal-overlay" style="display:none">
<div class="modal-box">
<h2 id="modal-heading">Edit Tag</h2>
<div class="modal-body">
<img id="modal-img" class="modal-img" src="" alt="" style="display:none">
<div id="modal-img-placeholder" class="modal-img-placeholder" style="display:none">&#9835;</div>
<label>Tag UID</label>
<input id="modal-uid" placeholder="AA:BB:CC:DD" autocomplete="off">
<label>Audio File</label>
<select id="modal-file"></select>
<label>Title</label>
<input id="modal-title" placeholder="Song title" autocomplete="off">
<label>Artist</label>
<input id="modal-artist" placeholder="Artist name" autocomplete="off">
<label>Album</label>
<input id="modal-album" placeholder="Album name" autocomplete="off">
<label>Album Art</label>
<select id="modal-imgname"></select>
</div>
<div class="modal-footer">
<button id="btn-remove">Remove</button>
<button id="btn-cancel">Cancel</button>
<button id="btn-save">Save</button>
</div>
</div>
</div>

<div id="toast"></div>

<script>
var tags=[],files=[],images=[],page=1,editingUid=null;
var PP=6;

function esc(s){
var d=document.createElement('div');
d.textContent=s;
return d.innerHTML;
}

function imgUrl(name){
return name?'/img?name='+encodeURIComponent(name):'';
}

async function loadTags(){
var r=await fetch('/api/tags');
var d=await r.json();
tags=d.tags||[];
}

async function loadFiles(){
var r=await fetch('/api/files');
var d=await r.json();
files=d.files||[];
}

async function loadImages(){
var r=await fetch('/api/images');
var d=await r.json();
images=d.images||[];
}

function renderGrid(){
var g=document.getElementById('tag-grid');
var start=(page-1)*PP;
var items=tags.slice(start,start+PP);
if(!items.length){
g.innerHTML='<div class="empty-state"><div class="empty-icon">&#9835;</div><p>No tags registered</p><p class="empty-hint">Tap + Add Tag to get started</p></div>';
return;
}
var h='';
for(var i=0;i<items.length;i++){
var t=items[i];
var img=t.img?('<img class="card-img" src="'+imgUrl(t.img)+'" onerror="this.style.display=\'none\';this.nextElementSibling.style.display=\'flex\'" alt="">'
+'<div class="card-img-placeholder" style="display:none">&#9835;</div>')
:'<div class="card-img-placeholder">&#9835;</div>';
h+='<div class="tag-card" data-uid="'+esc(t.uid)+'">'+img
+'<div class="card-body"><div class="card-uid">'+esc(t.uid)+'</div>'
+'<div class="card-title">'+esc(t.title||'Untitled')+'</div>'
+'<div class="card-artist">'+esc(t.artist||'Unknown artist')+'</div>'
+'</div></div>';
}
g.innerHTML=h;
var cards=g.querySelectorAll('.tag-card');
for(var j=0;j<cards.length;j++){
cards[j].addEventListener('click',function(){
editTag(this.dataset.uid);
});
}
}

function renderPagination(){
var p=document.getElementById('pagination');
var total=Math.ceil(tags.length/PP);
if(total<=1){p.innerHTML='';return;}
var h='<button '+(page<=1?'disabled':'')+' data-delta="-1">&lsaquo;</button>';
for(var i=1;i<=total;i++){
h+='<button class="'+(i===page?'active':'')+'" data-page="'+i+'">'+i+'</button>';
}
h+='<button '+(page>=total?'disabled':'')+' data-delta="1">&rsaquo;</button>';
p.innerHTML=h;
var btns=p.querySelectorAll('button:not([disabled])');
for(var j=0;j<btns.length;j++){
btns[j].addEventListener('click',function(){
if(this.dataset.page)page=parseInt(this.dataset.page);
else page+=parseInt(this.dataset.delta);
render();
window.scrollTo({top:0,behavior:'smooth'});
});
}
}

function render(){
document.getElementById('stats').textContent=tags.length+' tag'+(tags.length!==1?'s':'');
renderGrid();
renderPagination();
}

function showModal(title,uid,file,title_,artist,album,img){
editingUid=uid||null;
document.getElementById('modal-heading').textContent=title;
var uidEl=document.getElementById('modal-uid');
uidEl.value=uid||'';
uidEl.readOnly=!!uid;
if(!uid)uidEl.placeholder='AA:BB:CC:DD';

var sel=document.getElementById('modal-file');
sel.innerHTML='';
for(var i=0;i<files.length;i++){
sel.innerHTML+='<option value="music/'+esc(files[i])+'">'+esc(files[i])+'</option>';
}
for(var j=0;j<sel.options.length;j++){
if(sel.options[j].value===file){sel.selectedIndex=j;break;}
}

document.getElementById('modal-title').value=title_||'';
document.getElementById('modal-artist').value=artist||'';
document.getElementById('modal-album').value=album||'';

var imgSel=document.getElementById('modal-imgname');
imgSel.innerHTML='<option value="">-- none --</option>';
for(var k=0;k<images.length;k++){
imgSel.innerHTML+='<option value="'+esc(images[k])+'">'+esc(images[k])+'</option>';
}
for(var l=0;l<imgSel.options.length;l++){
if(imgSel.options[l].value===img){imgSel.selectedIndex=l;break;}
}

var imgEl=document.getElementById('modal-img');
var placeholder=document.getElementById('modal-img-placeholder');
if(img){
imgEl.src=imgUrl(img);
imgEl.style.display='block';
imgEl.onerror=function(){imgEl.style.display='none';placeholder.style.display='flex';};
placeholder.style.display='none';
}else{
imgEl.style.display='none';
placeholder.style.display='flex';
}

document.getElementById('btn-remove').style.display=uid?'block':'none';
document.getElementById('modal').style.display='flex';
}

function hideModal(){
document.getElementById('modal').style.display='none';
}

async function saveTag(){
var uid=document.getElementById('modal-uid').value.trim().toUpperCase();
if(!uid){toast('Tag UID is required','err');return;}
var file=document.getElementById('modal-file').value;
if(!file){toast('Please select an audio file','err');return;}
var body={uid:uid,file:file,
title:document.getElementById('modal-title').value.trim(),
artist:document.getElementById('modal-artist').value.trim(),
album:document.getElementById('modal-album').value.trim(),
img:document.getElementById('modal-imgname').value};
var r=await fetch('/api/tag',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
var d=await r.json();
if(d.ok){hideModal();await loadTags();render();toast('Tag saved','ok');}
else{toast(d.error||'Save failed','err');}
}

async function removeTag(){
if(!editingUid)return;
if(!confirm('Remove tag '+editingUid+'?'))return;
var r=await fetch('/api/tag?uid='+encodeURIComponent(editingUid),{method:'DELETE'});
var d=await r.json();
if(d.ok){hideModal();await loadTags();render();toast('Tag removed','ok');}
else{toast(d.error||'Remove failed','err');}
}

function editTag(uid){
for(var i=0;i<tags.length;i++){
if(tags[i].uid===uid){
var t=tags[i];
showModal('Edit Tag',t.uid,t.file,t.title,t.artist,t.album,t.img);
return;
}
}
}

function toast(msg,cls){
var c=document.getElementById('toast');
var el=document.createElement('div');
el.className='toast-msg toast-'+cls;
el.textContent=msg;
c.appendChild(el);
setTimeout(function(){el.remove();},3000);
}

document.getElementById('btn-add').addEventListener('click',function(){
showModal('Add Tag','','','','','','');
});

document.getElementById('btn-save').addEventListener('click',saveTag);
document.getElementById('btn-cancel').addEventListener('click',hideModal);
document.getElementById('btn-remove').addEventListener('click',removeTag);

document.getElementById('modal').addEventListener('click',function(e){
if(e.target===e.currentTarget)hideModal();
});

document.getElementById('btn-upload').addEventListener('click',function(){
var p=document.getElementById('upload-panel');
p.style.display=p.style.display==='none'?'block':'none';
});

document.getElementById('file-input').addEventListener('change',async function(){
var f=this.files[0];
if(!f)return;
var form=new FormData();
form.append('file',f);
document.getElementById('upload-progress').style.display='block';
document.getElementById('upload-progress').value=0;
try{
var r=await fetch('/upload',{method:'POST',body:form});
var d=await r.json();
if(d.ok){toast('Uploaded: '+d.filename,'ok');await loadFiles();}
else{toast(d.error||'Upload failed','err');}
}catch(e){toast('Upload failed','err');}
document.getElementById('upload-progress').style.display='none';
this.value='';
});

(async function(){
await Promise.all([loadTags(),loadFiles(),loadImages()]);
render();
})();
</script>
</body>
</html>
)HTML";

// ================================================================
//  Helpers
// ================================================================

static void sendJSON(int code, const String &json) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", json);
}

static void sendOK(const String &extra = "") {
  String json = "{\"ok\":true";
  if (extra.length() > 0) { json += ","; json += extra; }
  json += "}";
  sendJSON(200, json);
}

static void sendError(int code, const String &msg) {
  String json = "{\"ok\":false,\"error\":\"";
  for (size_t i = 0; i < msg.length(); i++) {
    char c = msg[i];
    if (c == '"' || c == '\\') json += '\\';
    json += c;
  }
  json += "\"}";
  sendJSON(code, json);
}

static String jsonStr(const char *s) {
  String out = "\"";
  while (*s) {
    char c = *s++;
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += "\"";
  return out;
}

// ================================================================
//  GET /api/tags  →  {"tags":[{uid,file,title,artist,album,img},...]}
// ================================================================

static void handleApiTags() {
  String json = "{\"tags\":[";
  bool first = true;
  if (tagDoc.size() > 0) {
    for (JsonPair kv : tagDoc.as<JsonObject>()) {
      if (!first) json += ",";
      first = false;
      JsonObject v = kv.value();
      json += "{\"uid\":";  json += jsonStr(kv.key().c_str());
      json += ",\"file\":"; json += jsonStr(v["file"]   | "");
      json += ",\"title\":";json += jsonStr(v["title"]  | "");
      json += ",\"artist\":";json += jsonStr(v["artist"]| "");
      json += ",\"album\":"; json += jsonStr(v["album"] | "");
      json += ",\"img\":";  json += jsonStr(v["img"]    | "");
      json += "}";
    }
  }
  json += "]}";
  sendJSON(200, json);
}

// ================================================================
//  GET /api/files  →  {"files":["song1.wav",...]}
// ================================================================

static void handleApiFiles() {
  String json = "{\"files\":[";
  bool first = true;
  File dir = SD.open("/music");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        const char *name = f.name();
        const char *base = strrchr(name, '/');
        if (base) base++; else base = name;
        if (!first) json += ",";
        first = false;
        json += jsonStr(base);
      }
      f.close();
    }
    dir.close();
  }
  json += "]}";
  sendJSON(200, json);
}

// ================================================================
//  GET /api/images  →  {"images":["art.bmp",...]}
// ================================================================

static void handleApiImages() {
  String json = "{\"images\":[";
  bool first = true;
  File dir = SD.open("/img");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        const char *name = f.name();
        const char *base = strrchr(name, '/');
        if (base) base++; else base = name;
        if (!first) json += ",";
        first = false;
        json += jsonStr(base);
      }
      f.close();
    }
    dir.close();
  }
  json += "]}";
  sendJSON(200, json);
}

// ================================================================
//  POST /api/tag  (JSON body)  →  upsert tag
// ================================================================

static void handleApiTagPost() {
  String body = server.arg("plain");
  if (body.length() == 0) {
    sendError(400, "Empty request body");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendError(400, "Invalid JSON");
    return;
  }

  const char *uid  = doc["uid"]  | "";
  const char *file = doc["file"] | "";
  if (strlen(uid) < 2 || strlen(file) < 2) {
    sendError(400, "Missing required fields: uid, file");
    return;
  }

  tagDoc[uid]["file"] = file;
  if (doc["title"].is<const char*>()  && strlen(doc["title"]))  tagDoc[uid]["title"]  = doc["title"];
  if (doc["artist"].is<const char*>() && strlen(doc["artist"])) tagDoc[uid]["artist"] = doc["artist"];
  if (doc["album"].is<const char*>()  && strlen(doc["album"]))  tagDoc[uid]["album"]  = doc["album"];
  if (doc["img"].is<const char*>()    && strlen(doc["img"]))    tagDoc[uid]["img"]    = doc["img"];

  // Remove empty optional fields
  if (tagDoc[uid]["title"].is<const char*>()  && strlen(tagDoc[uid]["title"])  == 0) tagDoc[uid].remove("title");
  if (tagDoc[uid]["artist"].is<const char*>() && strlen(tagDoc[uid]["artist"]) == 0) tagDoc[uid].remove("artist");
  if (tagDoc[uid]["album"].is<const char*>()  && strlen(tagDoc[uid]["album"])  == 0) tagDoc[uid].remove("album");
  if (tagDoc[uid]["img"].is<const char*>()    && strlen(tagDoc[uid]["img"])    == 0) tagDoc[uid].remove("img");

  if (SD.exists("/tags.json")) SD.remove("/tags.json");
  File f = SD.open("/tags.json", FILE_WRITE);
  if (f) {
    serializeJson(tagDoc, f);
    f.close();
    sendOK();
  } else {
    sendError(500, "Failed to write tags.json");
  }
}

// ================================================================
//  DELETE /api/tag?uid=...  →  remove tag
// ================================================================

static void handleApiTagDelete() {
  String uid = server.arg("uid");
  uid.toUpperCase();

  if (uid.length() < 2) {
    sendError(400, "Missing uid parameter");
    return;
  }

  if (tagDoc[uid].isNull()) {
    sendError(404, "Tag not found");
    return;
  }

  tagDoc.remove(uid);

  if (SD.exists("/tags.json")) SD.remove("/tags.json");
  File f = SD.open("/tags.json", FILE_WRITE);
  if (f) {
    serializeJson(tagDoc, f);
    f.close();
    sendOK();
  } else {
    sendError(500, "Failed to write tags.json");
  }
}

// ================================================================
//  GET /img?name=...  →  serve BMP image from SD
// ================================================================

static void handleImg() {
  String name = server.arg("name");

  // Sanitize: prevent directory traversal
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c == '/' || c == '\\' || c == '\0') {
      server.send(403, "text/plain", "Forbidden");
      return;
    }
  }

  String path = "/img/" + name;
  if (!SD.exists(path)) {
    server.send(404, "text/plain", "Not found");
    return;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "Not found");
    return;
  }

  server.streamFile(f, "image/bmp");
  f.close();
}

// ================================================================
//  POST /upload  →  WAV file upload (multipart)
// ================================================================

static String lastUploadedFile;
static bool   lastUploadOK = false;

static void handleUpload() {
  HTTPUpload &up = server.upload();
  static File uploadFile;

  if (up.status == UPLOAD_FILE_START) {
    lastUploadOK = false;
    String name = up.filename;
    // Sanitize filename
    for (size_t i = 0; i < name.length(); i++) {
      char c = name[i];
      if (!isalnum(c) && c != '-' && c != '_' && c != '.')
        name[i] = '_';
    }
    String path = "/music/" + name;
    uploadFile = SD.open(path, FILE_WRITE);
    if (uploadFile) {
      lastUploadedFile = name;
    } else {
      lastUploadedFile = "";
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile)
      uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      lastUploadOK = true;
    }
  }
}

static void handleUploadComplete() {
  if (lastUploadOK && lastUploadedFile.length() > 0) {
    sendOK("\"filename\":\"" + lastUploadedFile + "\"");
  } else {
    sendError(500, "Upload failed — could not write to SD card");
  }
}

// ================================================================
//  Main page
// ================================================================

static void handleRoot() {
  server.send(200, "text/html", PAGE_HTML);
}

// ================================================================
//  CORS preflight
// ================================================================

static void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

// ================================================================
//  Public API
// ================================================================

void initWebServer() {
  Serial.println("Starting web server...");
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",             HTTP_GET,    handleRoot);
  server.on("/api/tags",     HTTP_GET,    handleApiTags);
  server.on("/api/files",    HTTP_GET,    handleApiFiles);
  server.on("/api/images",   HTTP_GET,    handleApiImages);
  server.on("/api/tag",      HTTP_POST,   handleApiTagPost);
  server.on("/api/tag",      HTTP_DELETE, handleApiTagDelete);
  server.on("/api/tag",      HTTP_OPTIONS, handleOptions);
  server.on("/img",          HTTP_GET,    handleImg);
  server.on("/upload",       HTTP_POST,   handleUploadComplete, handleUpload);

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

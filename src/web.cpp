// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web.h"
#include "tags.h"
#include "wav_parser.h"
#include "screen.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <Update.h>
#include <esp_heap_caps.h>

static WebServer server(80);
static PN532 *s_nfc = nullptr;  // set by initWebServer(); used by /api/scan

// ================================================================
//  HTML Page (complete SPA)
// ================================================================

static const char PAGE_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TinyJuke</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0A0E1A;color:#eee;min-height:100vh}
#app{max-width:640px;margin:0 auto;padding:16px 16px 32px}
header{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:20px}
h1{font-size:22px;color:#4ADE80;font-weight:700}
#stats{font-size:13px;color:#6B7B8D}

#tabs{display:flex;gap:8px;margin-bottom:16px}
.tab{flex:1;padding:9px 0;border:1px solid #1E293B;border-radius:8px;background:#111A2E;color:#6B7B8D;cursor:pointer;font-size:14px;font-weight:500;transition:background .15s}
.tab.active{background:#4ADE80;color:#0A0E1A;border-color:#4ADE80;font-weight:600}

#tag-grid,#music-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px}
@media(max-width:480px){#tag-grid,#music-grid{grid-template-columns:1fr}}
.tag-card{background:#111A2E;border-radius:10px;overflow:hidden;cursor:pointer;transition:transform .15s,box-shadow .15s;border:1px solid #1E293B}
.tag-card:hover{transform:translateY(-2px);box-shadow:0 4px 20px rgba(0,0,0,.5);border-color:#4ADE80}
.tag-card .card-img{width:100%;height:110px;object-fit:cover;background:#1a1f2e;display:block}
.tag-card .card-img-placeholder{width:100%;height:110px;background:linear-gradient(135deg,#1a1f2e 0%,#1E293B 100%);display:flex;align-items:center;justify-content:center;font-size:36px;color:#334155}
.tag-card .card-body{padding:10px 12px}
.tag-card .card-uid{font-family:'SF Mono',ui-monospace,monospace;font-size:10px;color:#6B7B8D;margin-bottom:4px;word-break:break-all}
.tag-card .card-title{font-size:15px;font-weight:600;color:#fff;margin-bottom:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tag-card .card-artist{font-size:13px;color:#94a3b8;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tag-card .card-info{font-size:11px;color:#6B7B8D;margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

.empty-state{grid-column:1/-1;text-align:center;padding:48px 16px;color:#6B7B8D}
.empty-state .empty-icon{font-size:48px;margin-bottom:12px}
.empty-state p{font-size:15px;margin-bottom:4px}
.empty-state .empty-hint{font-size:13px;color:#455A6E}

#pagination,#music-pagination{display:flex;justify-content:center;align-items:center;gap:6px;margin-bottom:20px}
#pagination button,#music-pagination button{min-width:36px;height:36px;border:1px solid #1E293B;border-radius:8px;background:#111A2E;color:#eee;cursor:pointer;font-size:14px;transition:background .1s}
#pagination button:hover:not(:disabled),#music-pagination button:hover:not(:disabled){background:#1E293B}
#pagination button.active,#music-pagination button.active{background:#4ADE80;color:#0A0E1A;border-color:#4ADE80;font-weight:600}
#pagination button:disabled,#music-pagination button:disabled{opacity:.3;cursor:default}

#actions{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}
#actions button{padding:10px 24px;border-radius:8px;border:none;cursor:pointer;font-size:15px;font-weight:500;transition:background .15s}
#btn-add{background:#4ADE80;color:#0A0E1A}
#btn-add:hover{background:#6ee79a}
#btn-upload,#btn-upload-img{background:#111A2E;color:#eee;border:1px solid #1E293B}
#btn-upload:hover,#btn-upload-img:hover{background:#1E293B}

#fw-panel{margin-top:0;padding:16px;background:#111A2E;border-radius:10px;border:1px solid #1E293B}
#fw-panel label{font-size:13px;color:#6B7B8D;display:block;margin-bottom:8px}
.modal-body input[type=file],#fw-panel input[type=file]{padding:9px 10px;border:1px dashed #24324a;border-radius:6px;background:#0A0E1A;color:#6B7B8D;font-size:13px;width:100%;cursor:pointer}
.modal-body input[type=file]::file-selector-button,#fw-panel input[type=file]::file-selector-button{padding:6px 14px;margin-right:12px;border:none;border-radius:5px;background:#1E293B;color:#eee;font-size:13px;font-weight:500;cursor:pointer;transition:background .15s}
.modal-body input[type=file]:hover::file-selector-button,#fw-panel input[type=file]:hover::file-selector-button{background:#2d3a4f}
.modal-body input[type=file]::-webkit-file-upload-button,#fw-panel input[type=file]::-webkit-file-upload-button{padding:6px 14px;margin-right:12px;border:none;border-radius:5px;background:#1E293B;color:#eee;font-size:13px;font-weight:500;cursor:pointer}
.modal-hint{font-size:12px;color:#6B7B8D;line-height:1.5}
#upload-progress,#upload-img-progress,#fw-progress{width:100%;height:6px;display:none;accent-color:#4ADE80}
#upload-status,#upload-img-status,#fw-status{font-size:12px;color:#6B7B8D;margin-top:8px;min-height:14px}
#btn-upload-start:disabled,#btn-img-upload-start:disabled,#btn-fw-install:disabled{opacity:.5;cursor:default}
#btn-fw-install{padding:8px 18px;border-radius:6px;border:none;cursor:pointer;font-size:14px;font-weight:500;background:#4ADE80;color:#0A0E1A}
#fw-pin{padding:8px 10px;border:1px solid #1E293B;border-radius:6px;background:#0A0E1A;color:#eee;font-size:14px;width:100px}
#fw-pin:focus{outline:none;border-color:#4ADE80}

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
#btn-save,#btn-music-save,#btn-upload-start,#btn-img-upload-start{background:#4ADE80;color:#0A0E1A}
#btn-save:hover,#btn-music-save:hover,#btn-upload-start:hover,#btn-img-upload-start:hover{background:#6ee79a}
#btn-save:disabled,#btn-music-save:disabled{opacity:.5;cursor:default}
#btn-cancel,#btn-music-cancel,#audio-cancel,#img-cancel{background:#1E293B;color:#eee}
#btn-cancel:hover,#btn-music-cancel:hover,#audio-cancel:hover,#img-cancel:hover{background:#2d3a4f}
#btn-remove,#btn-music-delete{background:none;color:#F87171;margin-right:auto}
#btn-remove:hover,#btn-music-delete:hover{background:#2a1515}

#toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);z-index:200;display:flex;flex-direction:column;align-items:center;gap:8px;pointer-events:none}
.toast-msg{padding:10px 20px;border-radius:8px;font-size:14px;text-align:center;animation:toastIn .25s ease;pointer-events:auto;max-width:90vw}
.toast-ok{background:#14532d;color:#86efac;border:1px solid #166534}
.toast-err{background:#450a0a;color:#fca5a5;border:1px solid #7f1d1d}
@keyframes toastIn{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:translateY(0)}}
</style>
</head>
<body>
<div id="app">
<header><h1>TinyJuke</h1><div id="stats"></div></header>
<div id="tabs">
<button id="tab-tags" class="tab active">Tags</button>
<button id="tab-music" class="tab">Music</button>
<button id="tab-system" class="tab">System</button>
</div>
<div id="view-tags">
<div id="tag-grid"></div>
<div id="pagination"></div>
<div id="actions">
<button id="btn-add">+ Add Tag</button>
<button id="btn-upload">Upload Audio</button>
<button id="btn-upload-img">Upload Image</button>
</div>
</div>
<div id="view-music" style="display:none">
<div id="music-grid"></div>
<div id="music-pagination"></div>
</div>
<div id="view-system" style="display:none">
<div id="fw-panel">
<div style="font-size:14px;margin-bottom:12px">Firmware: <span id="fw-version" style="color:#4ADE80">&hellip;</span></div>
<label>Upload a firmware image (.bin built with the matching partition table). The device installs it to the inactive slot and reboots when done.</label>
<input type="file" id="fw-input" accept=".bin">
<label>Update PIN (shown on the device screen)</label>
<input id="fw-pin" inputmode="numeric" maxlength="4" placeholder="0000" autocomplete="off">
<div style="display:flex;gap:8px;align-items:center;margin-top:8px">
<button id="btn-fw-install">Install &amp; Reboot</button>
</div>
<div id="fw-status"></div>
<progress id="fw-progress" max="100" value="0"></progress>
</div>
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
<div id="modal-scan-status" style="font-size:12px;color:#FACC15;min-height:14px"></div>
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

<div id="music-modal" class="modal-overlay" style="display:none">
<div class="modal-box">
<h2>Edit Music</h2>
<div class="modal-body">
<label>File</label>
<input id="mmodal-name" readonly>
<label>Title</label>
<input id="mmodal-title" placeholder="Song title" autocomplete="off">
<label>Artist</label>
<input id="mmodal-artist" placeholder="Artist name" autocomplete="off">
<div id="mmodal-links" style="font-size:12px;color:#6B7B8D"></div>
<div id="mmodal-status" style="font-size:12px;color:#FACC15;min-height:14px"></div>
</div>
<div class="modal-footer">
<button id="btn-music-delete">Delete</button>
<button id="btn-music-cancel">Cancel</button>
<button id="btn-music-save">Save</button>
</div>
</div>
</div>

<div id="audio-modal" class="modal-overlay" style="display:none">
<div class="modal-box">
<h2>Upload Audio</h2>
<div class="modal-body">
<label>Audio File</label>
<input type="file" id="file-input" accept="audio/*,.mp3,.m4a,.aac,.ogg,.oga,.flac,.wav">
<p class="modal-hint">WAV / MP3 / M4A / AAC / OGG / FLAC. Non-WAV files are converted in your browser to 44.1 kHz 16-bit mono WAV. Embedded album art (if any) is saved as a 300&times;300 BMP.</p>
<div id="upload-status"></div>
<progress id="upload-progress" max="100" value="0"></progress>
</div>
<div class="modal-footer">
<button id="audio-cancel">Cancel</button>
<button id="btn-upload-start">Convert &amp; Upload</button>
</div>
</div>
</div>

<div id="img-modal" class="modal-overlay" style="display:none">
<div class="modal-box">
<h2>Upload Image</h2>
<div class="modal-body">
<label>Image File</label>
<input type="file" id="img-file-input" accept=".bmp,.jpg,.jpeg,.png">
<p class="modal-hint">BMP / JPG / PNG. Uploaded as-is to /img/ and selectable as album art when editing a tag.</p>
<div id="upload-img-status"></div>
<progress id="upload-img-progress" max="100" value="0"></progress>
</div>
<div class="modal-footer">
<button id="img-cancel">Cancel</button>
<button id="btn-img-upload-start">Upload</button>
</div>
</div>
</div>

<div id="toast"></div>

<script>
var tags=[],files=[],images=[],music=[],page=1,musicPage=1,editingUid=null,editingFile=null;
var PP=6;

function esc(s){
var d=document.createElement('div');
d.textContent=s;
return d.innerHTML;
}

function imgUrl(name){
return name?'/img?name='+encodeURIComponent(name):'';
}

function optHas(sel,val){
for(var i=0;i<sel.options.length;i++){if(sel.options[i].value===val)return true;}
return false;
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

async function loadMusic(){
var r=await fetch('/api/music');
var d=await r.json();
music=d.music||[];
}

async function loadVersion(){
try{
var r=await fetch('/api/version');
var d=await r.json();
document.getElementById('fw-version').textContent=d.version||'?';
}catch(e){}
}

async function installFirmware(){
var inp=document.getElementById('fw-input');
var f=inp.files[0];
if(!f){toast('Please select a .bin file first','err');return;}
if(!/\.bin$/i.test(f.name)){toast('Not a .bin file','err');return;}
var pin=document.getElementById('fw-pin').value.trim();
if(!/^\d{4}$/.test(pin)){toast('Enter the 4-digit PIN from the device screen','err');return;}
if(!confirm('Install '+f.name+' ('+formatSize(f.size)+')?\n\nThe device reboots when done; restart the Web Server from the device menu to reconnect.'))return;
var btn=document.getElementById('btn-fw-install');
var st=document.getElementById('fw-status');
var pr=document.getElementById('fw-progress');
btn.disabled=true;
st.textContent='Verifying PIN…';
try{
var vr=await fetch('/api/verify-pin?pin='+encodeURIComponent(pin));
if(!vr.ok){var vm='Invalid PIN';try{var vj=await vr.json();if(vj&&vj.error)vm=vj.error;}catch(e){}throw new Error(vm);}
st.textContent='Uploading firmware…';
pr.style.display='block';pr.value=0;
await uploadBlob('/update?size='+f.size+'&pin='+encodeURIComponent(pin),f,f.name,function(p){pr.value=p;});
st.textContent='Installed. Device is rebooting — start the Web Server from the device menu to reconnect.';
toast('Firmware installed','ok');
}catch(e){
st.textContent='';
toast(e.message||'Update failed','err');
}finally{
btn.disabled=false;
pr.style.display='none';pr.value=0;
inp.value='';
}
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
+'<div class="card-title">'+esc(t.title||(t.file||'').split('/').pop())+'</div>'
+'<div class="card-artist">'+(t.artist?esc(t.artist):'&nbsp;')+'</div>'
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

function formatSize(b){
return b<1048576?Math.round(b/1024)+' KB':(b/1048576).toFixed(1)+' MB';
}

function formatDur(s){
return Math.floor(s/60)+':'+('0'+(s%60)).slice(-2);
}

function linkedTags(name){
return tags.filter(function(t){
return t.file==='music/'+name||t.file==='/music/'+name;
});
}

function renderMusicGrid(){
var g=document.getElementById('music-grid');
var start=(musicPage-1)*PP;
var items=music.slice(start,start+PP);
if(!items.length){
g.innerHTML='<div class="empty-state"><div class="empty-icon">&#9835;</div><p>No music files</p><p class="empty-hint">Upload audio from the Tags view</p></div>';
return;
}
var h='';
for(var i=0;i<items.length;i++){
var m=items[i];
var nl=linkedTags(m.name).length;
h+='<div class="tag-card" data-name="'+esc(m.name)+'">'
+'<div class="card-body">'
+'<div class="card-title">'+esc(m.title||m.name)+'</div>'
+'<div class="card-artist">'+esc(m.artist||'Unknown artist')+'</div>'
+'<div class="card-info">'+esc(m.name)+'</div>'
+'<div class="card-info">'+formatDur(m.duration)+' &middot; '+formatSize(m.size)
+' &middot; '+(nl?nl+' tag'+(nl!==1?'s':''):'no tags')+'</div>'
+'</div></div>';
}
g.innerHTML=h;
var cards=g.querySelectorAll('.tag-card');
for(var j=0;j<cards.length;j++){
cards[j].addEventListener('click',function(){
showMusicModal(this.dataset.name);
});
}
}

function renderMusicPagination(){
var p=document.getElementById('music-pagination');
var total=Math.ceil(music.length/PP);
if(total<=1){p.innerHTML='';return;}
var h='<button '+(musicPage<=1?'disabled':'')+' data-delta="-1">&lsaquo;</button>';
for(var i=1;i<=total;i++){
h+='<button class="'+(i===musicPage?'active':'')+'" data-page="'+i+'">'+i+'</button>';
}
h+='<button '+(musicPage>=total?'disabled':'')+' data-delta="1">&rsaquo;</button>';
p.innerHTML=h;
var btns=p.querySelectorAll('button:not([disabled])');
for(var j=0;j<btns.length;j++){
btns[j].addEventListener('click',function(){
if(this.dataset.page)musicPage=parseInt(this.dataset.page);
else musicPage+=parseInt(this.dataset.delta);
renderMusic();
window.scrollTo({top:0,behavior:'smooth'});
});
}
}

function renderMusic(){
document.getElementById('stats').textContent=music.length+' file'+(music.length!==1?'s':'');
renderMusicGrid();
renderMusicPagination();
}

function switchView(v){
var views=['tags','music','system'];
for(var i=0;i<views.length;i++){
document.getElementById('view-'+views[i]).style.display=views[i]===v?'block':'none';
document.getElementById('tab-'+views[i]).className='tab'+(views[i]===v?' active':'');
}
if(v==='music')renderMusic();
else if(v==='tags')render();
else document.getElementById('stats').textContent='';
}

function showMusicModal(name){
var m=null;
for(var i=0;i<music.length;i++){
if(music[i].name===name){m=music[i];break;}
}
if(!m)return;
editingFile=name;
document.getElementById('mmodal-name').value=name;
document.getElementById('mmodal-title').value=m.title||'';
document.getElementById('mmodal-artist').value=m.artist||'';
var links=linkedTags(name);
document.getElementById('mmodal-links').textContent=links.length
?'Used by: '+links.map(function(t){return (t.title||'Untitled')+' ('+t.uid+')';}).join(', ')
:'Not linked to any tag';
document.getElementById('music-modal').style.display='flex';
}

function hideMusicModal(){
document.getElementById('music-modal').style.display='none';
}

async function saveMusicMeta(){
if(!editingFile)return;
var btn=document.getElementById('btn-music-save');
btn.disabled=true;
btn.textContent='Writing…';
document.getElementById('mmodal-status').textContent='Writing metadata… the first edit of a file rewrites it and can take a minute (progress shown on the device screen).';
try{
var body={name:editingFile,
title:document.getElementById('mmodal-title').value.trim(),
artist:document.getElementById('mmodal-artist').value.trim()};
var r=await fetch('/api/file/meta',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
var d=await r.json();
if(d.ok){hideMusicModal();await loadMusic();renderMusic();toast('Metadata saved','ok');}
else{toast(d.error||'Save failed','err');}
}catch(e){
toast(e.message||'Save failed','err');
}finally{
btn.disabled=false;
btn.textContent='Save';
document.getElementById('mmodal-status').textContent='';
}
}

async function deleteMusic(){
if(!editingFile)return;
var links=linkedTags(editingFile);
var msg='Delete '+editingFile+'?';
if(links.length){
msg+='\n\nThis also removes '+links.length+' linked tag'+(links.length!==1?'s':'')+':\n'
+links.map(function(t){return '- '+(t.title||'Untitled')+' ('+t.uid+')';}).join('\n');
}
if(!confirm(msg))return;
var r=await fetch('/api/file?name='+encodeURIComponent(editingFile),{method:'DELETE'});
var d=await r.json();
if(d.ok){
hideMusicModal();
await Promise.all([loadMusic(),loadTags(),loadFiles()]);
renderMusic();
var n=(d.removed||[]).length;
toast('Deleted'+(n?' (removed '+n+' tag'+(n!==1?'s':'')+')':''),'ok');
}else{toast(d.error||'Delete failed','err');}
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
// keep this tag's current file selectable even if it's hidden from listings (e.g. a dotfile) so saving an edit can't silently remap it
if(file&&!optHas(sel,file)){sel.innerHTML+='<option value="'+esc(file)+'">'+esc(file)+'</option>';}
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
if(img&&!optHas(imgSel,img)){imgSel.innerHTML+='<option value="'+esc(img)+'">'+esc(img)+'</option>';}
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
document.getElementById('btn-save').disabled=false;
if(!uid){startScan();}else{stopScan();document.getElementById('modal-scan-status').textContent='';}
document.getElementById('modal').style.display='flex';
}

function hideModal(){
stopScan();
document.getElementById('modal-scan-status').textContent='';
document.getElementById('modal').style.display='none';
}

function checkDupUid(okMsg){
var st=document.getElementById('modal-scan-status');
var uid=document.getElementById('modal-uid').value.trim().toUpperCase();
var known=null;
for(var i=0;i<tags.length;i++){if(tags[i].uid===uid){known=tags[i];break;}}
document.getElementById('btn-save').disabled=!!known;
if(known){
st.textContent='Already registered: '+(known.title||(known.file||'').split('/').pop());
st.style.color='#F87171';
}else{
st.textContent=okMsg;
st.style.color='#FACC15';
}
}

var scanTimer=null,lastScanUid=null,scanGen=0;
function startScan(){
stopScan();
lastScanUid=null;
checkDupUid('...or tap a tag on the reader');
var gen=scanGen;
scanTimer=setInterval(async function(){
try{
var r=await fetch('/api/scan');
if(!r.ok)return;
var d=await r.json();
if(gen!==scanGen)return;/* stale response - scan stopped/restarted while in flight */
if(d&&d.ok&&d.uid&&d.uid!==lastScanUid){
lastScanUid=d.uid;
document.getElementById('modal-uid').value=d.uid;
checkDupUid('Scanned: '+d.uid);
}
}catch(e){/* AP dropped or device left web screen - keep polling */}
},500);
}
function stopScan(){scanGen++;if(scanTimer){clearInterval(scanTimer);scanTimer=null;}}

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
document.getElementById('modal-uid').addEventListener('input',function(){checkDupUid('...or tap a tag on the reader');});
document.getElementById('btn-remove').addEventListener('click',removeTag);

document.getElementById('modal').addEventListener('click',function(e){
if(e.target===e.currentTarget)hideModal();
});

document.getElementById('tab-tags').addEventListener('click',function(){switchView('tags');});
document.getElementById('tab-music').addEventListener('click',function(){switchView('music');});
document.getElementById('tab-system').addEventListener('click',function(){switchView('system');});

document.getElementById('btn-fw-install').addEventListener('click',installFirmware);

document.getElementById('btn-music-save').addEventListener('click',saveMusicMeta);
document.getElementById('btn-music-cancel').addEventListener('click',hideMusicModal);
document.getElementById('btn-music-delete').addEventListener('click',deleteMusic);

document.getElementById('music-modal').addEventListener('click',function(e){
if(e.target===e.currentTarget)hideMusicModal();
});

function showAudioModal(){
document.getElementById('file-input').value='';
setUploadStatus('',null);
document.getElementById('audio-modal').style.display='flex';
}
function hideAudioModal(){document.getElementById('audio-modal').style.display='none';}

function showImgModal(){
document.getElementById('img-file-input').value='';
document.getElementById('upload-img-status').textContent='';
var p=document.getElementById('upload-img-progress');p.style.display='none';p.value=0;
document.getElementById('img-modal').style.display='flex';
}
function hideImgModal(){document.getElementById('img-modal').style.display='none';}

document.getElementById('btn-upload').addEventListener('click',showAudioModal);
document.getElementById('btn-upload-img').addEventListener('click',showImgModal);
document.getElementById('audio-cancel').addEventListener('click',hideAudioModal);
document.getElementById('img-cancel').addEventListener('click',hideImgModal);
document.getElementById('audio-modal').addEventListener('click',function(e){if(e.target===e.currentTarget)hideAudioModal();});
document.getElementById('img-modal').addEventListener('click',function(e){if(e.target===e.currentTarget)hideImgModal();});

var AC=window.AudioContext||window.webkitAudioContext;
var OAC=window.OfflineAudioContext||window.webkitOfflineAudioContext;
var TARGET_RATE=44100;

function baseName(name){
var i=name.lastIndexOf('.');
return i>0?name.substring(0,i):name;
}

function isWavName(name){return /\.wav$/i.test(name);}

function setUploadStatus(msg,pct){
var s=document.getElementById('upload-status');
var p=document.getElementById('upload-progress');
s.textContent=msg||'';
if(!msg){p.style.display='none';p.removeAttribute('value');return;}
p.style.display='block';
if(pct===undefined||pct===null){p.removeAttribute('value');}
else{p.value=Math.max(0,Math.min(100,pct));}
}

function uploadBlob(url,blob,filename,onProgress){
return new Promise(function(resolve,reject){
var xhr=new XMLHttpRequest();
xhr.open('POST',url);
xhr.upload.onprogress=function(e){
if(e.lengthComputable&&onProgress)onProgress(100*e.loaded/e.total);
};
xhr.onload=function(){
try{
var d=JSON.parse(xhr.responseText);
if(d.ok)resolve(d);
else reject(new Error(d.error||'Upload failed'));
}catch(err){reject(new Error('Upload failed'));}
};
xhr.onerror=function(){reject(new Error('Upload failed'));};
var form=new FormData();
form.append('file',blob,filename);
xhr.send(form);
});
}

function pcmToWavBlob(f32,rate){
var n=f32.length;
// 12 RIFF + 24 fmt + 156 canonical LIST INFO + 8 data header = 200.
// The empty padded INAM/IART fields let the firmware patch metadata
// in place later instead of rewriting the whole file.
var buf=new ArrayBuffer(200+n*2);
var v=new DataView(buf);
function ws(o,str){for(var i=0;i<str.length;i++)v.setUint8(o+i,str.charCodeAt(i));}
ws(0,'RIFF');v.setUint32(4,192+n*2,true);ws(8,'WAVE');
ws(12,'fmt ');v.setUint32(16,16,true);
v.setUint16(20,1,true);v.setUint16(22,1,true);
v.setUint32(24,rate,true);v.setUint32(28,rate*2,true);
v.setUint16(32,2,true);v.setUint16(34,16,true);
ws(36,'LIST');v.setUint32(40,148,true);ws(44,'INFO');
ws(48,'INAM');v.setUint32(52,64,true);
ws(120,'IART');v.setUint32(124,64,true);
ws(192,'data');v.setUint32(196,n*2,true);
var off=200;
for(var i=0;i<n;i++){
var x=f32[i];
if(x<-1)x=-1;else if(x>1)x=1;
v.setInt16(off,x<0?x*0x8000:x*0x7FFF,true);
off+=2;
}
return new Blob([buf],{type:'audio/wav'});
}

async function decodeAndResample(file){
setUploadStatus('Reading file…',null);
var ab=await file.arrayBuffer();
var bytes=new Uint8Array(ab);
var art=extractAlbumArt(bytes);
setUploadStatus('Decoding audio…',null);
var ac=new AC();
var audioBuf;
try{audioBuf=await ac.decodeAudioData(ab.slice(0));}
finally{if(ac.close)ac.close();}
setUploadStatus('Resampling to 44.1 kHz mono…',null);
var frames=Math.max(1,Math.ceil(audioBuf.duration*TARGET_RATE));
var oac=new OAC(1,frames,TARGET_RATE);
var src=oac.createBufferSource();
src.buffer=audioBuf;
src.connect(oac.destination);
src.start();
var rendered=await oac.startRendering();
setUploadStatus('Encoding WAV…',null);
return {wav:pcmToWavBlob(rendered.getChannelData(0),TARGET_RATE),art:art};
}

function be32(b,o){return (b[o]*16777216)+(b[o+1]<<16)+(b[o+2]<<8)+b[o+3];}
function syncsafe(a,b,c,d){return ((a&0x7F)<<21)|((b&0x7F)<<14)|((c&0x7F)<<7)|(d&0x7F);}

function extractAlbumArt(b){
try{
if(b.length>10&&b[0]===0x49&&b[1]===0x44&&b[2]===0x33)return apicFromID3(b);
if(b.length>4&&b[0]===0x66&&b[1]===0x4C&&b[2]===0x61&&b[3]===0x43)return pictureFromFlac(b);
if(b.length>12&&b[4]===0x66&&b[5]===0x74&&b[6]===0x79&&b[7]===0x70)return coverFromMp4(b);
}catch(e){}
return null;
}

function apicFromID3(b){
var major=b[3],flags=b[5];
var tagSize=syncsafe(b[6],b[7],b[8],b[9]);
var end=Math.min(10+tagSize,b.length);
var pos=10;
if(flags&0x40){
var ext=major>=4?syncsafe(b[pos],b[pos+1],b[pos+2],b[pos+3]):be32(b,pos);
pos+=ext;
}
var dec=new TextDecoder('utf-8',{fatal:false});
while(pos+10<=end){
var id=String.fromCharCode(b[pos],b[pos+1],b[pos+2],b[pos+3]);
var size=major>=4?syncsafe(b[pos+4],b[pos+5],b[pos+6],b[pos+7]):be32(b,pos+4);
if(id.charCodeAt(0)===0||size<=0||pos+10+size>end)break;
if(id==='APIC'){
var frameEnd=pos+10+size;
var p=pos+10;
var enc=b[p++];
var mEnd=p;
while(mEnd<frameEnd&&b[mEnd]!==0)mEnd++;
var mime=dec.decode(b.slice(p,mEnd));
p=mEnd+1;
p++;
if(enc===1||enc===2){
while(p+1<frameEnd&&!(b[p]===0&&b[p+1]===0))p+=2;
p+=2;
}else{
while(p<frameEnd&&b[p]!==0)p++;
p++;
}
if(p>=frameEnd)return null;
return {mime:mime||'image/jpeg',bytes:b.slice(p,frameEnd)};
}
pos+=10+size;
}
return null;
}

function pictureFromFlac(b){
var pos=4;
var dec=new TextDecoder('utf-8',{fatal:false});
while(pos+4<b.length){
var h=b[pos];
var isLast=(h&0x80)!==0;
var type=h&0x7F;
var size=(b[pos+1]<<16)|(b[pos+2]<<8)|b[pos+3];
pos+=4;
if(type===6&&pos+size<=b.length){
var p=pos+4;
var ml=be32(b,p);p+=4;
var mime=dec.decode(b.slice(p,p+ml));p+=ml;
var dl=be32(b,p);p+=4;
p+=dl+16;
var il=be32(b,p);p+=4;
return {mime:mime||'image/jpeg',bytes:b.slice(p,p+il)};
}
if(isLast)break;
pos+=size;
}
return null;
}

function coverFromMp4(b){
function find(start,end,name){
var p=start;
while(p+8<=end){
var sz=be32(b,p);
var t=String.fromCharCode(b[p+4],b[p+5],b[p+6],b[p+7]);
if(sz<8||p+sz>end)return null;
if(t===name)return {start:p+8,end:p+sz};
p+=sz;
}
return null;
}
var moov=find(0,b.length,'moov');if(!moov)return null;
var udta=find(moov.start,moov.end,'udta');if(!udta)return null;
var meta=find(udta.start,udta.end,'meta');if(!meta)return null;
var ilst=find(meta.start+4,meta.end,'ilst');if(!ilst)return null;
var covr=find(ilst.start,ilst.end,'covr');if(!covr)return null;
var data=find(covr.start,covr.end,'data');if(!data)return null;
var flag=be32(b,data.start);
var mime=flag===13?'image/jpeg':(flag===14?'image/png':'image/jpeg');
return {mime:mime,bytes:b.slice(data.start+8,data.end)};
}

async function artTo300Bmp(art){
var blob=new Blob([art.bytes],{type:art.mime});
var bmp=await createImageBitmap(blob);
var canvas=document.createElement('canvas');
canvas.width=300;canvas.height=300;
var ctx=canvas.getContext('2d');
ctx.imageSmoothingQuality='high';
var sw=bmp.width,sh=bmp.height;
var scale=Math.min(300/sw,300/sh);
var dw=sw*scale,dh=sh*scale;
ctx.drawImage(bmp,(300-dw)/2,(300-dh)/2,dw,dh);
var img=ctx.getImageData(0,0,300,300);
return rgba300ToBmp(img);
}

function rgba300ToBmp(img){
var w=300,h=300;
var rowSize=((24*w+31)>>5)<<2;
var pixSize=rowSize*h;
var fileSize=54+pixSize;
var buf=new ArrayBuffer(fileSize);
var v=new DataView(buf);
var u=new Uint8Array(buf);
v.setUint8(0,0x42);v.setUint8(1,0x4D);
v.setUint32(2,fileSize,true);
v.setUint32(6,0,true);
v.setUint32(10,54,true);
v.setUint32(14,40,true);
v.setInt32(18,w,true);
v.setInt32(22,h,true);
v.setUint16(26,1,true);
v.setUint16(28,24,true);
v.setUint32(30,0,true);
v.setUint32(34,pixSize,true);
v.setInt32(38,2835,true);
v.setInt32(42,2835,true);
v.setUint32(46,0,true);
v.setUint32(50,0,true);
var px=img.data;
for(var y=0;y<h;y++){
var srcRow=(h-1-y)*w*4;
var dstRow=54+y*rowSize;
for(var x=0;x<w;x++){
var si=srcRow+x*4,di=dstRow+x*3;
u[di]=px[si+2];u[di+1]=px[si+1];u[di+2]=px[si];
}
}
return new Blob([buf],{type:'image/bmp'});
}

async function handleAudioUpload(){
var inp=document.getElementById('file-input');
var f=inp.files[0];
if(!f){toast('Please select a file first','err');return;}
var btn=document.getElementById('btn-upload-start');
btn.disabled=true;
var base=baseName(f.name);
try{
var wavBlob,art=null;
if(isWavName(f.name)){
wavBlob=f;
}else{
var out=await decodeAndResample(f);
wavBlob=out.wav;
art=out.art;
}
setUploadStatus('Uploading audio…',0);
var r=await uploadBlob('/upload',wavBlob,base+'.wav',function(p){
setUploadStatus('Uploading audio…',p);
});
var audioName=r.filename||(base+'.wav');
var artName='';
if(art){
try{
setUploadStatus('Encoding album art…',null);
var bmpBlob=await artTo300Bmp(art);
setUploadStatus('Uploading album art…',0);
var ri=await uploadBlob('/upload-img',bmpBlob,base+'.bmp',function(p){
setUploadStatus('Uploading album art…',p);
});
artName=ri.filename||'';
}catch(e){
toast('Album art skipped: '+(e.message||'unknown'),'err');
}
}
setUploadStatus('',null);
toast('Uploaded '+audioName+(artName?' + '+artName:''),'ok');
inp.value='';
hideAudioModal();
await Promise.all([loadFiles(),loadImages(),loadMusic()]);
}catch(e){
setUploadStatus('',null);
toast(e.message||'Upload failed','err');
}finally{
btn.disabled=false;
}
}

function doImgUpload(url,file,progEl,statusEl,onDone){
var xhr=new XMLHttpRequest();
xhr.open('POST',url);
xhr.upload.onprogress=function(e){
if(e.lengthComputable){progEl.value=Math.round(100*e.loaded/e.total);}
};
xhr.onload=function(){
progEl.value=0;progEl.style.display='none';statusEl.textContent='';
try{
var d=JSON.parse(xhr.responseText);
if(d.ok){toast('Uploaded: '+d.filename,'ok');onDone();}
else{toast(d.error||'Upload failed','err');}
}catch(e){toast('Upload failed','err');}
};
xhr.onerror=function(){
progEl.value=0;progEl.style.display='none';statusEl.textContent='';
toast('Upload failed','err');
};
var form=new FormData();
form.append('file',file);
progEl.style.display='block';
progEl.value=0;
statusEl.textContent='Uploading…';
xhr.send(form);
}

document.getElementById('btn-upload-start').addEventListener('click',handleAudioUpload);

document.getElementById('btn-img-upload-start').addEventListener('click',function(){
var inp=document.getElementById('img-file-input');
var f=inp.files[0];
if(!f){toast('Please select a file first','err');return;}
doImgUpload('/upload-img', f,
document.getElementById('upload-img-progress'),
document.getElementById('upload-img-status'),
function(){loadImages().then(function(){inp.value='';hideImgModal();});});
});

(async function(){
await Promise.all([loadTags(),loadFiles(),loadImages(),loadMusic(),loadVersion()]);
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

// Leftover temp file from an interrupted metadata rewrite (power loss /
// reset mid-write) — hide from listings.
static bool isTmpName(const char *base) {
  size_t n = strlen(base);
  return n > 4 && strcmp(base + n - 4, ".tmp") == 0;
}

// Hidden/dotfile (e.g. macOS "._" resource forks, ".DS_Store") — hide from listings.
static bool isHiddenName(const char *base) {
  return base[0] == '.';
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
        if (!isTmpName(base) && !isHiddenName(base)) {
          if (!first) json += ",";
          first = false;
          json += jsonStr(base);
        }
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
        if (!isHiddenName(base)) {
          if (!first) json += ",";
          first = false;
          json += jsonStr(base);
        }
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
  if (doc["title"].is<const char*>())   tagDoc[uid]["title"]  = doc["title"];
  if (doc["artist"].is<const char*>()) tagDoc[uid]["artist"] = doc["artist"];
  if (doc["album"].is<const char*>())  tagDoc[uid]["album"]  = doc["album"];
  if (doc["img"].is<const char*>())    tagDoc[uid]["img"]    = doc["img"];

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
//  Music file management
// ================================================================

// Shared scratch buffer: WAV header/meta scans + slow-path rewrite streaming.
static uint8_t wavScanBuf[4096];

static bool isSafeName(const String &name) {
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c == '/' || c == '\\' || c == '\0') return false;
  }
  return true;
}

// ================================================================
//  GET /api/music  →  {"music":[{name,size,duration,title,artist},...]}
// ================================================================

static void handleApiMusic() {
  String json = "{\"music\":[";
  bool first = true;
  File dir = SD.open("/music");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        const char *name = f.name();
        const char *base = strrchr(name, '/');
        if (base) base++; else base = name;
        if (isTmpName(base) || isHiddenName(base)) { f.close(); continue; }

        size_t n = f.read(wavScanBuf, sizeof(wavScanBuf));
        WavHeader hdr = {};
        uint32_t duration = parseWavHeaderBuffer(wavScanBuf, n, hdr) ? wavDurationSeconds(hdr) : 0;
        WavMeta meta;
        parseWavMetaBuffer(wavScanBuf, n, meta);

        if (!first) json += ",";
        first = false;
        json += "{\"name\":";     json += jsonStr(base);
        json += ",\"size\":";     json += String((uint32_t)f.size());
        json += ",\"duration\":"; json += String(duration);
        json += ",\"title\":";    json += jsonStr(meta.title);
        json += ",\"artist\":";   json += jsonStr(meta.artist);
        json += "}";
      }
      f.close();
    }
    dir.close();
  }
  json += "]}";
  sendJSON(200, json);
}

// ================================================================
//  POST /api/file/meta  (JSON body {name,title,artist})
//  Writes title/artist into the WAV's LIST INFO chunk.
//  Fast path: canonical chunk present → patch the two fields in place.
//  Slow path: stream-rewrite the file with a canonical chunk inserted
//  before the data chunk (one-time cost for legacy files).
// ================================================================

// Returns nullptr on success, else a short error message.
static const char *writeWavMeta(const String &path, const char *title, const char *artist) {
  uint32_t t0 = millis();
  Serial.printf("[meta] writeWavMeta '%s' title='%s' artist='%s'\n", path.c_str(), title, artist);

  File f = SD.open(path, FILE_READ);
  if (!f) { Serial.println("[meta] ERROR: cannot open file"); return "Cannot open file"; }
  size_t fileSize = (size_t)f.size();
  size_t headLen = f.read(wavScanBuf, sizeof(wavScanBuf));
  Serial.printf("[meta] fileSize=%u headLen=%u\n", (unsigned)fileSize, (unsigned)headLen);

  size_t listOff = 0;
  if (findCanonicalListInfo(wavScanBuf, headLen, &listOff)) {
    // Fast path: in-place patch.
    Serial.printf("[meta] canonical chunk at %u -> fast path\n", (unsigned)listOff);
    f.close();
    File w = SD.open(path, "r+");
    if (w) {
      size_t titleOff = 0, artistOff = 0;
      canonicalListFieldOffsets(listOff, &titleOff, &artistOff);
      uint8_t field[WAV_INFO_CAP];
      writeCanonicalField(field, title);
      bool ok = w.seek(titleOff) && w.write(field, WAV_INFO_CAP) == WAV_INFO_CAP;
      writeCanonicalField(field, artist);
      ok = ok && w.seek(artistOff) && w.write(field, WAV_INFO_CAP) == WAV_INFO_CAP;
      w.close();
      Serial.printf("[meta] fast path %s in %lums\n", ok ? "OK" : "FAILED", millis() - t0);
      return ok ? nullptr : "Write failed";
    }
    // "r+" unsupported/failed → fall through to the slow path.
    Serial.println("[meta] WARN: open \"r+\" failed -> falling back to slow path");
    f = SD.open(path, FILE_READ);
    if (!f) { Serial.println("[meta] ERROR: cannot reopen file"); return "Cannot open file"; }
    headLen = f.read(wavScanBuf, sizeof(wavScanBuf));
  }

  WavHeader hdr = {};
  if (!parseWavHeaderBuffer(wavScanBuf, headLen, hdr)) {
    Serial.println("[meta] ERROR: header parse failed -> not a valid WAV");
    f.close();
    return "Not a valid WAV";
  }
  size_t dataChunkStart = hdr.dataOffset - 8;
  Serial.printf("[meta] slow path: dataOffset=%u dataSize=%u (~%us of audio)\n",
                (unsigned)hdr.dataOffset, (unsigned)hdr.dataSize, (unsigned)wavDurationSeconds(hdr));

  String tmpPath = path + ".tmp";
  if (SD.exists(tmpPath)) {
    Serial.println("[meta] removing stale .tmp from a previous attempt");
    SD.remove(tmpPath);
  }
  File w = SD.open(tmpPath, FILE_WRITE);
  if (!w) {
    Serial.println("[meta] ERROR: cannot create temp file");
    f.close();
    return "Cannot create temp file";
  }

  // RIFF/WAVE header (size patched below), then every front chunk except
  // an existing LIST INFO (dropped — replaced by the canonical one).
  size_t written = 0;
  bool ok = w.write(wavScanBuf, 12) == 12;
  written += 12;
  size_t srcPos = 12;
  while (ok && srcPos + 8 <= dataChunkStart) {
    uint32_t csize = (uint32_t)wavScanBuf[srcPos + 4] | ((uint32_t)wavScanBuf[srcPos + 5] << 8) |
                     ((uint32_t)wavScanBuf[srcPos + 6] << 16) | ((uint32_t)wavScanBuf[srcPos + 7] << 24);
    size_t total = 8 + csize + (csize & 1);
    if (srcPos + total > dataChunkStart) total = dataChunkStart - srcPos;
    bool isListInfo = memcmp(wavScanBuf + srcPos, "LIST", 4) == 0 && csize >= 4 &&
                      memcmp(wavScanBuf + srcPos + 8, "INFO", 4) == 0;
    Serial.printf("[meta] front chunk '%c%c%c%c' size=%u -> %s\n",
                  wavScanBuf[srcPos], wavScanBuf[srcPos + 1], wavScanBuf[srcPos + 2], wavScanBuf[srcPos + 3],
                  (unsigned)csize, isListInfo ? "drop (old LIST INFO)" : "copy");
    if (!isListInfo) {
      ok = w.write(wavScanBuf + srcPos, total) == total;
      written += total;
    }
    srcPos += total;
  }

  // Canonical LIST INFO with the new metadata.
  uint8_t chunk[WAV_CANON_LIST_SIZE];
  buildCanonicalListInfo(chunk, title, artist);
  ok = ok && w.write(chunk, sizeof(chunk)) == sizeof(chunk);
  written += sizeof(chunk);

  // Stream the remainder: data chunk header + payload + pad + trailing chunks.
  // A large PSRAM buffer minimises read<->write switching on the SD card
  // (4 KB alternating chunks measured ~146 KB/s); fall back to the small
  // static buffer if PSRAM is unavailable.
  size_t bufSize = 262144;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { buf = wavScanBuf; bufSize = sizeof(wavScanBuf); }
  size_t toStream = fileSize - dataChunkStart;
  Serial.printf("[meta] streaming %u KB through %u KB buffer...\n",
                (unsigned)(toStream / 1024), (unsigned)(bufSize / 1024));
  uint32_t tStream = millis();
  size_t streamed = 0, lastReport = 0;
  int lastPct = -1;
  drawWebProgress("Writing metadata", 0);
  f.seek(dataChunkStart);
  while (ok) {
    size_t n = f.read(buf, bufSize);
    if (n == 0) break;
    ok = w.write(buf, n) == n;
    written += n;
    streamed += n;
    int pct = (int)(streamed * 100 / toStream);
    if (pct != lastPct) { drawWebProgress("Writing metadata", pct); lastPct = pct; }
    if (streamed - lastReport >= 1048576) {  // progress every 1 MB
      lastReport = streamed;
      uint32_t el = millis() - tStream;
      Serial.printf("[meta] ... %u/%u KB, %u KB/s\n", (unsigned)(streamed / 1024),
                    (unsigned)(toStream / 1024),
                    (unsigned)(el ? streamed / el : 0));
    }
  }
  if (buf != wavScanBuf) free(buf);
  f.close();
  drawWebProgress(nullptr, -1);
  Serial.printf("[meta] streamed %u KB in %lums (write %s)\n",
                (unsigned)(streamed / 1024), millis() - tStream, ok ? "OK" : "FAILED");

  // Patch RIFF size from actual bytes written.
  uint32_t riffSize = (uint32_t)(written - 8);
  uint8_t sz[4] = { (uint8_t)riffSize, (uint8_t)(riffSize >> 8),
                    (uint8_t)(riffSize >> 16), (uint8_t)(riffSize >> 24) };
  ok = ok && w.seek(4) && w.write(sz, 4) == 4;
  w.close();

  if (!ok) {
    Serial.println("[meta] ERROR: rewrite failed, removing .tmp");
    SD.remove(tmpPath);
    return "Rewrite failed";
  }
  Serial.println("[meta] replacing original (remove + rename)...");
  if (!SD.remove(path)) {
    Serial.println("[meta] ERROR: cannot remove original");
    SD.remove(tmpPath);
    return "Cannot replace original";
  }
  if (!SD.rename(tmpPath, path)) {
    Serial.println("[meta] ERROR: rename failed");
    return "Rename failed";
  }
  Serial.printf("[meta] slow path OK in %lums total\n", millis() - t0);
  return nullptr;
}

static void handleApiFileMetaPost() {
  String body = server.arg("plain");
  if (body.length() == 0) {
    sendError(400, "Empty request body");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.println("[meta] ERROR: invalid JSON body");
    sendError(400, "Invalid JSON");
    return;
  }

  String name = doc["name"] | "";
  if (name.length() == 0) {
    sendError(400, "Missing required field: name");
    return;
  }
  if (!isSafeName(name)) {
    Serial.printf("[meta] ERROR: unsafe name '%s'\n", name.c_str());
    sendError(403, "Invalid name");
    return;
  }

  String path = "/music/" + name;
  if (!SD.exists(path)) {
    Serial.printf("[meta] ERROR: not found '%s'\n", path.c_str());
    sendError(404, "File not found");
    return;
  }

  const char *werr = writeWavMeta(path, doc["title"] | "", doc["artist"] | "");
  if (werr) sendError(500, werr);
  else sendOK();
}

// ================================================================
//  DELETE /api/file?name=...  →  delete music file + cascade tags
// ================================================================

static void handleApiFileDelete() {
  String name = server.arg("name");
  if (name.length() == 0) {
    sendError(400, "Missing name parameter");
    return;
  }
  if (!isSafeName(name)) {
    sendError(403, "Invalid name");
    return;
  }

  String path = "/music/" + name;
  if (!SD.exists(path)) {
    sendError(404, "File not found");
    return;
  }
  Serial.printf("[file] deleting '%s'\n", path.c_str());
  if (!SD.remove(path)) {
    Serial.println("[file] ERROR: SD.remove failed");
    sendError(500, "Delete failed");
    return;
  }

  // Cascade: drop tag mappings that reference the deleted file. tags.json
  // stores "music/x.wav"; also match a hand-edited "/music/x.wav".
  String rel = "music/" + name;
  String removed = "";
  bool removedAny = false;
  bool again = true;
  while (again) {  // remove one key per pass — can't remove while iterating
    again = false;
    for (JsonPair kv : tagDoc.as<JsonObject>()) {
      const char *file = kv.value()["file"] | "";
      if (rel == file || (file[0] == '/' && rel == file + 1)) {
        if (removedAny) removed += ",";
        removed += jsonStr(kv.key().c_str());
        removedAny = true;
        String key = kv.key().c_str();
        tagDoc.remove(key);
        again = true;
        break;
      }
    }
  }

  if (removedAny) {
    Serial.printf("[file] cascade-removed tags: %s\n", removed.c_str());
    if (SD.exists("/tags.json")) SD.remove("/tags.json");
    File f = SD.open("/tags.json", FILE_WRITE);
    if (!f) {
      sendError(500, "Failed to write tags.json");
      return;
    }
    serializeJson(tagDoc, f);
    f.close();
  }

  sendOK(String("\"removed\":[") + removed + "]");
}

// ================================================================
//  GET /api/version  →  {"version":"v1.6.0"}
// ================================================================

static void handleApiVersion() {
  sendJSON(200, String("{\"version\":") + jsonStr(VERSION_STRING) + "}");
}

// ================================================================
//  GET /api/scan  →  {"ok":true,"uid":"AA:BB:..."} when a tag is on
//  the reader, else {"ok":true,"uid":null}. On-demand single-shot
//  read, no background state — only a tag physically present during
//  the call is reported. Safe: the GUI loop owns the PN532 while on
//  the WEB screen (main.cpp short-circuits everything else).
// ================================================================

static void handleApiScan() {
  if (!s_nfc) { sendJSON(200, "{\"ok\":true,\"uid\":null}"); return; }
  uint8_t uid[10] = {0};
  uint8_t uidLen  = 0;
  bool found = s_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);
  if (found && uidLen > 0 && uidLen <= 10) {
    char buf[32];
    uidToStr(uid, uidLen, buf);
    sendJSON(200, String("{\"ok\":true,\"uid\":") + jsonStr(buf) + "}");
  } else {
    sendJSON(200, "{\"ok\":true,\"uid\":null}");
  }
}

// ================================================================
//  POST /update?size=...  →  OTA firmware upload (multipart .bin)
//  Writes into the inactive OTA slot via Update.h; on success the
//  response is sent and the device reboots into the new firmware.
//  No rollback: a firmware that boots but misbehaves needs USB reflash.
// ================================================================

static bool   s_updateOK = false;
static size_t s_updateExpected = 0;
static int    s_updateLastPct = -1;
static const char *s_updateError = nullptr;
static int    s_updateErrCode = 500;  // 4xx for client errors (PIN/size), 500 for OTA failures

// Per-session PIN shown on the device's web screen — proves physical
// presence before accepting a firmware image (the AP password alone is
// not enough to flash the device). After PIN_MAX_FAILURES wrong guesses
// the endpoint locks for the rest of the session (counter and PIN reset
// when the web server is restarted from the device), so the 10,000-PIN
// space cannot be brute-forced online.
#define PIN_MAX_FAILURES 5
static char    s_webPin[5] = "";
static uint8_t s_pinFailures = 0;

const char *getWebPin() {
  return s_webPin;
}

// GET /api/verify-pin?pin=XXXX — lets the web UI confirm the OTA PIN *before*
// it streams a multi-MB firmware image, so a wrong PIN fails fast instead of
// after the whole upload crosses the network. handleFwUpdate still re-checks
// the PIN server-side and shares this same lockout counter, so skipping this
// pre-check (e.g. a direct POST) gains an attacker nothing.
static void handleVerifyPin() {
  if (s_pinFailures >= PIN_MAX_FAILURES) {
    sendError(429, "Too many failed PINs - restart the web server on the device");
    return;
  }
  if (server.arg("pin") != s_webPin) {
    s_pinFailures++;
    Serial.printf("[ota] ERROR: invalid PIN via verify (attempt %u/%u)\n",
                  (unsigned)s_pinFailures, (unsigned)PIN_MAX_FAILURES);
    sendError(403, "Invalid PIN (shown on the device screen)");
    return;
  }
  s_pinFailures = 0;
  sendOK();
}

static void handleFwUpdate() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    s_updateOK = false;
    s_updateError = nullptr;
    s_updateErrCode = 500;
    s_updateLastPct = -1;
    s_updateExpected = 0;
    if (s_pinFailures >= PIN_MAX_FAILURES) {
      Serial.println("[ota] ERROR: PIN locked out");
      s_updateError = "Too many failed PINs - restart the web server on the device";
      s_updateErrCode = 429;
      return;  // Update never begins; WRITE/END stay no-ops
    }
    if (server.arg("pin") != s_webPin) {
      s_pinFailures++;
      Serial.printf("[ota] ERROR: invalid PIN (attempt %u/%u)\n",
                    (unsigned)s_pinFailures, (unsigned)PIN_MAX_FAILURES);
      s_updateError = "Invalid PIN (shown on the device screen)";
      s_updateErrCode = 403;
      return;
    }
    s_pinFailures = 0;
    long sizeArg = server.arg("size").toInt();
    if (sizeArg <= 0) {
      // Without the exact size, a truncated upload could not be detected.
      Serial.println("[ota] ERROR: missing/invalid size parameter");
      s_updateError = "Missing or invalid size parameter";
      s_updateErrCode = 400;
      return;
    }
    s_updateExpected = (size_t)sizeArg;
    Serial.printf("[ota] start '%s' (%u bytes)\n", up.filename.c_str(), (unsigned)s_updateExpected);
    if (!Update.begin(s_updateExpected)) {
      s_updateError = Update.errorString();
      Serial.printf("[ota] ERROR: begin failed: %s\n", s_updateError);
    } else {
      drawWebProgress("Installing", 0);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        // Capture the real failure before abort() overwrites it with "Aborted".
        s_updateError = Update.errorString();
        Serial.printf("[ota] ERROR: write failed: %s\n", s_updateError);
        Update.abort();
      } else if (s_updateExpected) {
        int pct = (int)(Update.progress() * 100 / s_updateExpected);
        if (pct != s_updateLastPct) { drawWebProgress("Installing", pct); s_updateLastPct = pct; }
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    // Require the exact declared byte count — end(true) would finalize a
    // short upload by shrinking the expected size to whatever arrived.
    if (!Update.isRunning()) {
      // begin() never ran (lockout / bad PIN / bad size / begin failure)
      // or a write aborted — error already recorded.
    } else if (Update.progress() != s_updateExpected) {
      Serial.printf("[ota] ERROR: incomplete upload (%u/%u bytes)\n",
                    (unsigned)Update.progress(), (unsigned)s_updateExpected);
      s_updateError = "Incomplete upload";
      s_updateErrCode = 400;
      Update.abort();
    } else if (Update.end(false)) {  // isFinished() holds; full verification
      s_updateOK = true;
      Serial.printf("[ota] success, %u bytes — rebooting\n", (unsigned)up.totalSize);
    } else {
      Serial.printf("[ota] ERROR: end failed: %s\n", Update.errorString());
    }
    drawWebProgress(nullptr, -1);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Serial.println("[ota] aborted by client");
    Update.abort();
    drawWebProgress(nullptr, -1);
  }
}

static void handleFwUpdateComplete() {
  if (s_updateOK) {
    sendOK();
    delay(750);  // let the response flush before the connection dies
    ESP.restart();
  } else {
    sendError(s_updateErrCode, s_updateError ? s_updateError : Update.errorString());
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
//  POST /upload-img  →  image file upload (multipart)
// ================================================================

static String lastUploadedImg;
static String uploadImgPath;
static bool   lastUploadImgOK = false;
static bool   uploadImgWriteError = false;

static void handleUploadImg() {
  HTTPUpload &up = server.upload();
  static File uploadFile;

  if (up.status == UPLOAD_FILE_START) {
    lastUploadImgOK = false;
    if (uploadFile) {
      uploadFile.close();
      if (uploadImgWriteError)
        SD.remove(uploadImgPath);
    }
    uploadImgWriteError = false;
    String name = up.filename;
    for (size_t i = 0; i < name.length(); i++) {
      char c = name[i];
      if (!isalnum(c) && c != '-' && c != '_' && c != '.')
        name[i] = '_';
    }
    uploadImgPath = "/img/" + name;
    uploadFile = SD.open(uploadImgPath, FILE_WRITE);
    if (uploadFile) {
      lastUploadedImg = name;
    } else {
      lastUploadedImg = "";
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && !uploadImgWriteError) {
      size_t written = uploadFile.write(up.buf, up.currentSize);
      if (written != up.currentSize)
        uploadImgWriteError = true;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      if (!uploadImgWriteError)
        lastUploadImgOK = true;
      else
        SD.remove(uploadImgPath);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
      SD.remove(uploadImgPath);
      lastUploadImgOK = false;
    }
  }
}

static void handleUploadImgComplete() {
  if (lastUploadImgOK && lastUploadedImg.length() > 0) {
    sendOK("\"filename\":\"" + lastUploadedImg + "\"");
  } else {
    sendError(500, "Upload failed — could not write to SD card");
  }
}

// ================================================================
//  POST /upload  →  WAV file upload (multipart)
// ================================================================

static String lastUploadedFile;
static String uploadPath;
static bool   lastUploadOK = false;
static bool   uploadWriteError = false;

static void handleUpload() {
  HTTPUpload &up = server.upload();
  static File uploadFile;

  if (up.status == UPLOAD_FILE_START) {
    lastUploadOK = false;
    if (uploadFile) {
      uploadFile.close();
      if (uploadWriteError)
        SD.remove(uploadPath);
    }
    uploadWriteError = false;
    String name = up.filename;
    // Sanitize filename
    for (size_t i = 0; i < name.length(); i++) {
      char c = name[i];
      if (!isalnum(c) && c != '-' && c != '_' && c != '.')
        name[i] = '_';
    }
    uploadPath = "/music/" + name;
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    if (uploadFile) {
      lastUploadedFile = name;
    } else {
      lastUploadedFile = "";
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && !uploadWriteError) {
      size_t written = uploadFile.write(up.buf, up.currentSize);
      if (written != up.currentSize)
        uploadWriteError = true;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      if (!uploadWriteError)
        lastUploadOK = true;
      else
        SD.remove(uploadPath);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
      SD.remove(uploadPath);
      lastUploadOK = false;
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
  // send_P streams from flash in chunks — the plain send(const char*)
  // overload copies the whole ~31 KB page into a heap String and serves
  // an empty page when that allocation fails.
  server.send_P(200, "text/html", PAGE_HTML, sizeof(PAGE_HTML) - 1);
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

void initWebServer(PN532 &nfc) {
  s_nfc = &nfc;
  Serial.printf("Starting web server... (heap %u, largest block %u)\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  if (!apOk) {
    Serial.println("[web] softAP failed, retrying once...");
    WiFi.mode(WIFI_OFF);
    delay(250);
    WiFi.mode(WIFI_AP);
    apOk = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  }
  Serial.printf("AP %s, IP: %s\n", apOk ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());

  // Fresh update PIN per web-server session; failed-attempt lockout resets
  // with it (restarting the server requires physical access to the device).
  snprintf(s_webPin, sizeof(s_webPin), "%04u", (unsigned)(esp_random() % 10000));
  s_pinFailures = 0;

  // Register routes once — server.on() appends to the handler list, so
  // re-registering on every start/stop cycle leaks heap.
  static bool s_routesRegistered = false;
  if (!s_routesRegistered) {
    s_routesRegistered = true;
    server.on("/",             HTTP_GET,    handleRoot);
    server.on("/api/tags",     HTTP_GET,    handleApiTags);
    server.on("/api/files",    HTTP_GET,    handleApiFiles);
    server.on("/api/images",   HTTP_GET,    handleApiImages);
    server.on("/api/tag",      HTTP_POST,   handleApiTagPost);
    server.on("/api/tag",      HTTP_DELETE, handleApiTagDelete);
    server.on("/api/tag",      HTTP_OPTIONS, handleOptions);
    server.on("/api/music",    HTTP_GET,    handleApiMusic);
    server.on("/api/music",    HTTP_OPTIONS, handleOptions);
    server.on("/api/file/meta", HTTP_POST,   handleApiFileMetaPost);
    server.on("/api/file/meta", HTTP_OPTIONS, handleOptions);
    server.on("/api/file",     HTTP_DELETE, handleApiFileDelete);
    server.on("/api/file",     HTTP_OPTIONS, handleOptions);
    server.on("/api/version",  HTTP_GET,    handleApiVersion);
    server.on("/api/scan",     HTTP_GET,    handleApiScan);
    server.on("/img",          HTTP_GET,    handleImg);
    server.on("/upload",       HTTP_POST,   handleUploadComplete, handleUpload);
    server.on("/upload-img",   HTTP_POST,   handleUploadImgComplete, handleUploadImg);
    server.on("/api/verify-pin", HTTP_GET,   handleVerifyPin);
    server.on("/update",       HTTP_POST,   handleFwUpdateComplete, handleFwUpdate);
  }

  server.begin();
  Serial.println("Web server started.");
}

void stopWebServer() {
  Serial.println("Stopping web server...");
  server.stop();
  // Single ordered teardown: drop AP without flipping the mode inside
  // softAPdisconnect (the combined transition races the WiFi stop state —
  // "netstack cb reg failed with 12308" / ESP_ERR_WIFI_STOP_STATE).
  WiFi.softAPdisconnect(false);
  WiFi.mode(WIFI_OFF);
  Serial.printf("Web server stopped. (heap %u, largest block %u)\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void handleWebClient() {
  server.handleClient();
}

int getWebConnectionCount() {
  return WiFi.softAPgetStationNum();
}

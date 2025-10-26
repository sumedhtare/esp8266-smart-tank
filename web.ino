String htmlMainPage();    // forward
String htmlSchedulePage(); // forward

void setupRoutes() {
  // Main dashboard (you had a big dashboard: here we serve a simplified version + reuse)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/html", htmlMainPage());
  });

  // New schedule page
  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/html", htmlSchedulePage());
  });

  // API: get schedules JSON
  server.on("/api/schedules", HTTP_GET, [](AsyncWebServerRequest * request) {
    DynamicJsonDocument doc(16000);
    JsonArray arr = doc.to<JsonArray>();
    for (auto &s : schedules) {
      JsonObject o = arr.createNestedObject();
      o["deviceId"] = s.deviceId;
      o["hour"] = s.hour;
      o["minute"] = s.minute;
      o["type"] = s.type;
      o["data"] = s.data;
      o["brightness"] = s.brightness;
      o["enabled"] = s.enabled;
    }
    String out;
    serializeJson(arr, out);
    logMsg(out);
    request->send(200, "application/json", out);
  });

  // API: save schedules (expects full array, overwrite existing)
server.on("/api/schedules", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL, // no upload
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        
       // Append the current chunk to global buffer
    if(index == 0) scheduleBody = ""; // first chunk, reset
for (size_t i = 0; i < len; i++) scheduleBody += (char)data[i];

    // Only process when full body received
    if(index + len == total){
          logMsg(scheduleBody);

        DynamicJsonDocument doc(16000); // increase if needed
        DeserializationError err = deserializeJson(doc, scheduleBody);
        if(err){
            request->send(400, "text/plain", "Bad JSON: " + String(err.c_str()));
            return;
        }

        JsonArray arr = doc.as<JsonArray>();
        schedules.clear();
        for(JsonObject obj : arr){
            ScheduleEntry s;
            s.deviceId = obj["deviceId"] | 0;
            s.hour = obj["hour"] | 0;
            s.minute = obj["minute"] | 0;
            s.type = obj["type"] ? String((const char*)obj["type"]) : "on";
            s.data = obj["data"] ? String((const char*)obj["data"]) : "";
            s.brightness = obj["brightness"] | 255;
            s.enabled = obj["enabled"] | true;
            schedules.push_back(s);
        }
        saveSchedulesToFS();
        request->send(200, "text/plain", "saved");
    }
});



  // status endpoint (small)
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest * request) {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.createNestedArray("devices");
    for (int i = 0; i < DEVICE_COUNT; i++) {
      JsonObject o = arr.createNestedObject();
      o["name"] = deviceNames[i];
      if (i < 4) o["state"] = deviceStates[i];
      else if (i == 4) o["state"] = stepper.currentPosition();
      else if (i == 5) {
        JsonObject neo = o.createNestedObject("state");
        char buf[8];
        // For simplicity send last color as #000000 (no tracking); UI reads schedules for current
        snprintf(buf, sizeof(buf), "#%06X", 0);
        neo["color"] = buf;
        neo["brightness"] = neoPixel.getBrightness();
      }
    }
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char timestr[16];
    snprintf(timestr, sizeof(timestr), "%02d:%02d", t->tm_hour, t->tm_min);
    doc["deviceTime"] = timestr;
    doc["waterLevel"] = waterLevelHigh ? 1 : 0;
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // manual control - similar to original
  server.on("/control", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (!request->hasParam("id", true)) {
      request->send(400, "text/plain", "missing id");
      return;
    }
    int id = request->getParam("id", true)->value().toInt();
    if (id < 0 || id >= DEVICE_COUNT) {
      request->send(400, "text/plain", "bad id");
      return;
    }

    if (id == 5) { // NeoPixel
      String colorStr = "#FF0000";
      uint8_t brightness = 255;
      if (request->hasParam("color", true)) colorStr = request->getParam("color", true)->value();
      if (request->hasParam("brightness", true)) brightness = request->getParam("brightness", true)->value().toInt();
      uint32_t color = strtoul(colorStr.substring(1).c_str(), nullptr, 16);
      applyNeoPixelColor(color, brightness);
    } else {
      if (!request->hasParam("value", true)) {
        request->send(400, "text/plain", "missing value");
        return;
      }
      int val = request->getParam("value", true)->value().toInt();
      val = constrain(val, 0, PWM_MAX);
      deviceStates[id] = val;
      if (devicePins[id] >= 0) analogWrite(devicePins[id], deviceStates[id]);
    }
    request->send(200, "text/plain", "ok");
  });

  // stepper control
  server.on("/stepper", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (!request->hasParam("dir", true)) {
      request->send(400, "text/plain", "missing");
      return;
    }
    String dir = request->getParam("dir", true)->value();
    int steps = 200;
    if (request->hasParam("steps", true)) steps = request->getParam("steps", true)->value().toInt();
    if (dir == "fwd" || dir == "forward") {
      stepper.move(steps);
    } else if (dir == "back" || dir == "backward") {
      stepper.move(-steps);
    } else if (dir == "stop") {
      stepper.stop();
    }
    request->send(200, "text/plain", "ok");
  });

  // thunder effect (kept from original)
  server.on("/thunder", HTTP_POST, [](AsyncWebServerRequest * request) {
    int flashes = 5;
    if (request->hasParam("times", true)) flashes = request->getParam("times", true)->value().toInt();
    for (int f = 0; f < flashes; f++) {
      uint8_t b = random(180, 255);
      neoPixel.setBrightness(b);
      for (int i = 0; i < NUMPIXELS; i++) neoPixel.setPixelColor(i, neoPixel.Color(255, 255, 255));
      neoPixel.show();
      delay(random(50, 150));
      neoPixel.clear();
      neoPixel.show();
      delay(random(100, 300));
    }
    request->send(200, "text/plain", "Thunderstorm triggered");
  });


  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP Logs</title>
</head>
<body>
  <h2>ESP WebSocket Logs</h2>
  <pre id="log"></pre>
  <script>
    var ws = new WebSocket('ws://' + location.hostname + '/ws');
    ws.onmessage = function(event) {
      var logElem = document.getElementById('log');
      logElem.textContent += event.data + '\n';
      logElem.scrollTop = logElem.scrollHeight; // auto-scroll
    };
  </script>
</body>
</html>
)rawliteral");
});
}

// ----------------------------- HTML pages -----------------------------
// A simplified main dashboard with a link to /schedule (you can paste your original big dashboard here)
String htmlMainPage() {
  String s = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartTank Manual Control</title>
<style>
body{font-family:Arial,sans-serif;padding:12px}
h2{margin-bottom:6px}
button{padding:8px 12px;margin:2px;border-radius:6px;border:none;background:#007BFF;color:#fff}
input[type=range]{width:120px}
table{width:100%;border-collapse:collapse}
th,td{padding:6px;text-align:left;border-bottom:1px solid #ddd}
.color-preview{display:inline-block;width:20px;height:20px;border-radius:4px;vertical-align:middle;border:1px solid #ccc}
</style>
</head>
<body>
<h2>Manual Control</h2>
<p><a href='/'>Back</a> | <a href='/schedule'>Schedules</a></p>

<table>
<tr><th>Device</th><th>Control</th></tr>

<tr><td>Water Pump</td>
<td>
<button onclick="setVal(0,1023)">ON</button>
<button onclick="setVal(0,0)">OFF</button>
<input type="range" min="0" max="1023" value="0" id="slider0" oninput="setVal(0,this.value)">
<span id="val0">0</span>
</td></tr>

<tr><td>Air Pump</td>
<td>
<button onclick="setVal(1,1023)">ON</button>
<button onclick="setVal(1,0)">OFF</button>
<input type="range" min="0" max="1023" value="0" id="slider1" oninput="setVal(1,this.value)">
<span id="val1">0</span>
</td></tr>

<tr><td>LED</td>
<td>
<button onclick="setVal(2,1023)">ON</button>
<button onclick="setVal(2,0)">OFF</button>
<input type="range" min="0" max="1023" value="0" id="slider2" oninput="setVal(2,this.value)">
<span id="val2">0</span>
</td></tr>

<tr><td>UV</td>
<td>
<button onclick="setVal(3,1023)">ON</button>
<button onclick="setVal(3,0)">OFF</button>
<input type="range" min="0" max="1023" value="0" id="slider3" oninput="setVal(3,this.value)">
<span id="val3">0</span>
</td></tr>

<tr><td>NeoPixel</td>
<td>
<input type="color" id="neoColor" value="#FF0000">
<input type="range" min="0" max="255" value="255" id="neoBrightness">
<button onclick="setNeo()">Set</button>
<span class="color-preview" id="preview" style="background:#FF0000"></span>
</td></tr>

<tr><td>Stepper</td>
<td>
<button onclick="step('fwd',200)">Forward 200</button>
<button onclick="step('back',200)">Backward 200</button>
<button onclick="step('stop',0)">Stop</button>
</td></tr>
</table>

<script>
function setVal(id,val){
  fetch('/control?id='+id+'&value='+val,{method:'POST'});
  fetch('/control', {
  method: 'POST',
  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
  body: 'id='+id+'&value='+val
});
  document.getElementById('val'+id).innerText = val;
  if(document.getElementById('slider'+id)) document.getElementById('slider'+id).value = val;
}

function setNeo(){
  const color = document.getElementById('neoColor').value;
  const br = document.getElementById('neoBrightness').value;
  fetch('/control', {
  method: 'POST',
  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
  body: 'id=5'+'&color='+color+'&brightness='+br
});
  document.getElementById('preview').style.background = color;
}

function step(dir,steps){
  fetch('/stepper', {
  method: 'POST',
  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
  body: 'dir='+dir+'&steps='+steps
});
}
</script>
</body>
</html>
)rawliteral";
  return s;
}

// The schedule page: single UI to view/add/edit/delete schedules
String htmlSchedulePage() {
  String p = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>SmartTank Schedules</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;padding:12px;margin:0}
h2{margin-top:0}
table{width:100%;border-collapse:collapse;margin-bottom:12px}
th,td{padding:6px;border-bottom:1px solid #ddd;text-align:left; font-size:14px;}
button{padding:10px 14px;border-radius:6px;border:none;background:#007BFF;color:#fff;margin:2px;font-size:14px}
.small{font-size:12px;color:#666}
input[type=number]{width:60px}
select,input[type=color],input[type=text]{width:100%}
.color-preview{display:inline-block;width:20px;height:20px;border-radius:4px;vertical-align:middle;border:1px solid #ccc;margin-left:4px}
.table-wrapper{overflow-x:auto}
@media(max-width:600px){
  body{padding:8px}
  th,td{font-size:12px;padding:4px}
  button{padding:8px 10px;font-size:12px}
  input[type=number]{width:50px}
}
</style>
</head>
<body>
<h2>Schedules</h2>
<p>
  <button onclick="addEmpty()">➕ Add Schedule</button> 
  <button onclick="saveAll()">💾 Save All</button> 
  <a href='/'>Back</a>
</p>
<div class="table-wrapper">
<table id="tbl"><thead><tr><th>Enable</th><th>Device</th><th>Time</th><th>Type</th><th>Data</th><th>Brightness</th><th>Action</th></tr></thead><tbody></tbody></table>
</div>
<script>
const DEV_NAMES = ["Water pump","Air pump","LED","UV","Auto feeder","Neo Pixel"];
function p(n){return n<10?'0'+n:n}
async function load() {
  const res = await fetch('/api/schedules');
  const arr = await res.json();
  const tbody = document.querySelector('#tbl tbody');
  tbody.innerHTML = '';
  arr.forEach((s, idx)=>{
    addRow(s, idx);
  });
}
function addEmpty(){
  addRow({deviceId:0, hour:0, minute:0, type:'on', data:'', brightness:255, enabled:true});
}
function addRow(s, idx) {
  const tbody = document.querySelector('#tbl tbody');
  const row = document.createElement('tr');

  // enabled
  const enTd = document.createElement('td');
  enTd.innerHTML = `<input type="checkbox" ${s.enabled ? 'checked' : ''}>`;
  row.appendChild(enTd);

  // device
  const devTd = document.createElement('td');
  let devOpt = '<select>';
  DEV_NAMES.forEach((n,i)=> devOpt += `<option value="${i}" ${s.deviceId==i?'selected':''}>${n}</option>`);
  devOpt += '</select>';
  devTd.innerHTML = devOpt;
  row.appendChild(devTd);

  // time
  const timeTd = document.createElement('td');
  timeTd.innerHTML = `<input type="number" min="0" max="23" value="${s.hour}"> : <input type="number" min="0" max="59" value="${s.minute}">`;
  row.appendChild(timeTd);

  // type
  const typeTd = document.createElement('td');
  typeTd.innerHTML = `<select>
    <option value="on" ${s.type==='on'?'selected':''}>ON</option>
    <option value="off" ${s.type==='off'?'selected':''}>OFF</option>
    <option value="value" ${s.type==='value'?'selected':''}>Value</option>
    <option value="color" ${s.type==='color'?'selected':''}>Color</option>
    <option value="stepper" ${s.type==='stepper'?'selected':''}>Stepper</option>
  </select>`;
  row.appendChild(typeTd);

  // data
  const dataTd = document.createElement('td');
  function dataHtml() {
    if (typeTd.querySelector('select').value === 'color') {
      const colorVal = s.data && s.data.length ? (s.data.startsWith('#') ? s.data : '#'+s.data) : '#FF0000';
      return `<input type="color" value="${colorVal}"> <span class="color-preview" style="background:${colorVal}"></span>`;
    } else {
      const val = s.data || '';
      return `<input type="text" value="${val}" placeholder="value or steps">`;
    }
  }
  dataTd.innerHTML = dataHtml();
  row.appendChild(dataTd);

  // brightness
  const brTd = document.createElement('td');
  brTd.innerHTML = `<input type="range" min="0" max="255" value="${s.brightness}"> <span class="small">${s.brightness}</span>`;
  row.appendChild(brTd);

  // action
  const actTd = document.createElement('td');
  actTd.innerHTML = `<button onclick="delRow(this)">Delete</button>`;
  row.appendChild(actTd);

  // listeners
  typeTd.querySelector('select').addEventListener('change', ()=> {
    dataTd.innerHTML = dataHtml();
    const colorInp = dataTd.querySelector('input[type=color]');
    if (colorInp) colorInp.addEventListener('input', ()=> dataTd.querySelector('.color-preview').style.background = colorInp.value);
  });
  const colorInp = dataTd.querySelector('input[type=color]');
  if (colorInp) colorInp.addEventListener('input', ()=> dataTd.querySelector('.color-preview').style.background = colorInp.value);

  // brightness display
  brTd.querySelector('input').addEventListener('input', e=> {
    brTd.querySelector('.small').innerText = e.target.value;
  });

  tbody.appendChild(row);
}

function delRow(btn) { btn.closest('tr').remove(); }

async function saveAll() {
  const rows = Array.from(document.querySelectorAll('#tbl tbody tr'));
  const out = [];
  rows.forEach(r=>{
    const enabled = r.cells[0].querySelector('input').checked;
    const deviceId = parseInt(r.cells[1].querySelector('select').value);
    const hour = parseInt(r.cells[2].querySelectorAll('input')[0].value) || 0;
    const minute = parseInt(r.cells[2].querySelectorAll('input')[1].value) || 0;
    const type = r.cells[3].querySelector('select').value;
    let data = '';
    if (type === 'color') {
      const c = r.cells[4].querySelector('input[type=color]').value;
      data = c.startsWith('#') ? c.substring(1) : c;
    } else {
      data = r.cells[4].querySelector('input[type=text]').value || '';
    }
    const brightness = parseInt(r.cells[5].querySelector('input[type=range]').value) || 255;
    out.push({deviceId, hour, minute, type, data, brightness, enabled});
  });
  await fetch('/api/schedules', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(out)});
  alert('Saved');
}

window.onload = load;
</script>
</body>
</html>
)rawliteral";
  return p;
}

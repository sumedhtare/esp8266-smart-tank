bool loadSchedulesFromFS() {
  schedules.clear();
  if (!LittleFS.exists(SCHEDULE_FILE)) {
    Serial.println("No schedules file found.");
    return false;
  }
  File f = LittleFS.open(SCHEDULE_FILE, "r");
  if (!f) {
    Serial.println("Failed to open schedule file");
    return false;
  }
  size_t size = f.size();
  if (size == 0) {
    f.close();
    return false;
  }
  // dynamic doc sized to file (cap at 32KB)
  const size_t CAP = min((size_t)32000, size);
  DynamicJsonDocument doc(CAP + 1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.print("Failed to parse schedules JSON: ");
    Serial.println(err.c_str());
    return false;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject o : arr) {
    ScheduleEntry s;
    s.deviceId = o["deviceId"] | 0;
    s.hour = o["hour"] | 0;
    s.minute = o["minute"] | 0;
    s.type =  o["type"] ? String((const char*)o["type"]) : "on";
    s.data =  o["data"] ? String((const char*)o["data"]) : "";
    s.brightness = o["brightness"] | 255;
    s.enabled = o["enabled"].as<bool>();
    schedules.push_back(s);
  }
  Serial.printf("Loaded %u schedules\n", (unsigned)schedules.size());
  return true;
}

bool saveSchedulesToFS() {
  DynamicJsonDocument doc(16000);
   logMsg("call saveSchedulesToFS");

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
  File f = LittleFS.open(SCHEDULE_FILE, "w");
  if (!f) {
    logMsg("Failed to open schedule file for write");
    return false;
  }
  if (serializeJson(doc, f) == 0) {
    logMsg("Failed to write schedule JSON");
    f.close();
    return false;
  }
  f.close();
  logMsg("Schedules saved to LittleFS");
  return true;
}

void executeScheduleEntry(const ScheduleEntry &s) {
  if (!s.enabled) return;

  Serial.printf("Execute schedule dev=%u time=%02u:%02u type=%s data=%s br=%u\n",
                s.deviceId, s.hour, s.minute, s.type.c_str(), s.data.c_str(), s.brightness);

  if (s.type == "on") {
    if (s.deviceId < 4 && devicePins[s.deviceId] >= 0) {
      deviceStates[s.deviceId] = PWM_MAX;
      analogWrite(devicePins[s.deviceId], deviceStates[s.deviceId]);
    }
  } else if (s.type == "off") {
    if (s.deviceId < 4 && devicePins[s.deviceId] >= 0) {
      deviceStates[s.deviceId] = 0;
      analogWrite(devicePins[s.deviceId], 0);
    }
  } else if (s.type == "value") {
    int val = s.data.toInt();
    val = constrain(val, 0, PWM_MAX);
    if (s.deviceId < 4 && devicePins[s.deviceId] >= 0) {
      deviceStates[s.deviceId] = val;
      analogWrite(devicePins[s.deviceId], deviceStates[s.deviceId]);
    }
  } else if (s.type == "color") {
    // deviceId for NeoPixel should be 5 by convention; allow user to target 5
    uint32_t colorHex = 0;
    String c = s.data;
    if (c.startsWith("#")) c = c.substring(1);
    colorHex = (uint32_t) strtoul(c.c_str(), NULL, 16);
    applyNeoPixelColor(colorHex, s.brightness);
  } else if (s.type == "stepper") {
    // stepper: data is steps (positive or negative)
    int steps = s.data.toInt();
    stepper.move(steps);
    // do not block; stepper.run() will be called in loop
  }
}

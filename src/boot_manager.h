#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "HB9IIUdisplayInit.h"
#include "myconfig.h"
#include "nvs_config.h"
#include "lv_driver.h"
#include "wifi_page.h"

extern LGFX tft;

#define BM_NTP1    "pool.ntp.org"
#define BM_NTP2    "time.nist.gov"
#define STATUS_Y   440
#define STATUS_H    40

namespace BootManager {

// ── Factory reset (touch & hold 3 s at boot) ──────────────────────────────────

static void checkFactoryReset() {
    Serial.println("[boot] Factory reset window — touch & hold 3s to reset");

    // Show faint hint in status bar
    tft.fillRect(0, STATUS_Y, 800, STATUS_H, TFT_BLACK);
    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(tft.color888(80, 80, 80));
    tft.drawString("Touch & hold 3s to reset WiFi settings", 400, STATUS_Y + STATUS_H / 2);
    tft.setTextDatum(TL_DATUM);

    const uint32_t WINDOW_MS = 3000;   // how long to watch for first touch
    const uint32_t HOLD_MS   = 3000;   // how long touch must be held

    uint32_t windowStart = millis();
    uint32_t touchStart  = 0;
    uint16_t tx, ty;

    while (true) {
        bool touching = tft.getTouch(&tx, &ty);

        if (touching) {
            if (touchStart == 0) {
                touchStart = millis();
                Serial.println("[boot] Touch detected — hold 3s to confirm reset");
            }
            uint32_t held = millis() - touchStart;

            // Progress bar
            tft.fillRect(100, STATUS_Y + 8, 600, 24, TFT_BLACK);
            tft.drawRect(100, STATUS_Y + 8, 600, 24, TFT_WHITE);
            tft.fillRect(101, STATUS_Y + 9,
                         (int)(held * 598 / HOLD_MS), 22, TFT_RED);

            if (held >= HOLD_MS) {
                Serial.println("[boot] *** FACTORY RESET triggered ***");
                tft.fillScreen(TFT_BLACK);
                tft.setFont(&fonts::DejaVu24);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_RED);
                tft.drawString("Factory Reset", 400, 210);
                tft.setFont(&fonts::DejaVu18);
                tft.setTextColor(TFT_WHITE);
                tft.drawString("Clearing settings — rebooting...", 400, 270);
                NVSConfig::clearWiFi();
                NVSConfig::clearLocation();
                Serial.println("[boot] NVS cleared.");
                delay(2000);
                ESP.restart();
            }
        } else {
            if (touchStart > 0) {
                Serial.println("[boot] Touch released — factory reset aborted");
                touchStart = 0;
                // Restore hint
                tft.fillRect(0, STATUS_Y, 800, STATUS_H, TFT_BLACK);
                tft.setFont(&fonts::DejaVu12);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(tft.color888(80, 80, 80));
                tft.drawString("Touch & hold 3s to reset WiFi settings",
                               400, STATUS_Y + STATUS_H / 2);
                tft.setTextDatum(TL_DATUM);
            }
            // Window expired with no touch → proceed
            if (millis() - windowStart >= WINDOW_MS) {
                Serial.println("[boot] Factory reset window passed.");
                break;
            }
        }
        delay(50);
    }

    tft.fillRect(0, STATUS_Y, 800, STATUS_H, TFT_BLACK);
}

// ── Status bar ────────────────────────────────────────────────────────────────

static void drawStatus(const char* msg, uint32_t rgb888 = 0xFFFFFF) {
    tft.fillRect(0, STATUS_Y, 800, STATUS_H, TFT_BLACK);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(tft.color888(
        (rgb888 >> 16) & 0xFF,
        (rgb888 >>  8) & 0xFF,
         rgb888        & 0xFF));
    tft.setTextDatum(MC_DATUM);
    tft.drawString(msg, 400, STATUS_Y + STATUS_H / 2);
    tft.setTextDatum(TL_DATUM);
}

// ── Reboot with countdown ─────────────────────────────────────────────────────

static void rebootIn(const char* reason, uint8_t seconds = 10) {
    Serial.printf("[boot] REBOOTING: %s\n", reason);
    char msg[80];
    for (int i = seconds; i > 0; i--) {
        snprintf(msg, sizeof(msg), "%s — rebooting in %d s", reason, i);
        drawStatus(msg, 0xFF4444);
        Serial.printf("[boot] %s\n", msg);
        delay(1000);
    }
    ESP.restart();
}

// ── Generic HTTP GET ──────────────────────────────────────────────────────────

static String httpGet(const char* url, bool secure = false, int retries = 3) {
    String body;

    for (int attempt = 1; attempt <= retries; attempt++) {
        Serial.printf("[http] GET %s  (attempt %d/%d)\n", url, attempt, retries);
        HTTPClient http;
        http.setTimeout(8000);
        http.addHeader("User-Agent", "ESP32-SatTracker/1.0");

        bool begun = false;
        WiFiClientSecure* secClient = nullptr;
        WiFiClient         plainClient;

        if (secure) {
            secClient = new WiFiClientSecure();   // heap — avoids stack overflow
            secClient->setInsecure();
            begun = http.begin(*secClient, url);
        } else {
            begun = http.begin(plainClient, url);
        }

        if (!begun) {
            Serial.println("[http] ERROR: begin() failed");
            if (secClient) delete secClient;
            if (attempt < retries) delay(2000);
            continue;
        }

        int code = http.GET();
        Serial.printf("[http] Response code: %d\n", code);

        if (code == HTTP_CODE_OK) {
            body = http.getString();
            Serial.printf("[http] Body (%d bytes): %s\n", body.length(), body.c_str());
            http.end();
            if (secClient) delete secClient;
            return body;                          // success — return immediately
        }

        Serial.printf("[http] ERROR: code %d\n", code);
        http.end();
        if (secClient) delete secClient;
        if (attempt < retries) {
            Serial.printf("[http] Retrying in 2 s...\n");
            delay(2000);
        }
    }

    Serial.println("[http] All attempts failed.");
    return body;   // empty
}

// ── Geolocation — called ONCE on first boot ───────────────────────────────────

static bool fetchGeoLocation() {
    Serial.println("[geo] ════════════════════════════════");
    Serial.println("[geo] Fetching geolocation (ip-api.com)");
    drawStatus("Fetching location ...", 0x00D4FF);

    String body = httpGet("http://ip-api.com/json/", false);
    if (body.isEmpty()) {
        Serial.println("[geo] ERROR: empty response");
        drawStatus("Geolocation failed", 0xFF4444);
        delay(1500);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[geo] ERROR: JSON parse failed — %s\n", err.c_str());
        drawStatus("Geolocation parse error", 0xFF4444);
        delay(1500);
        return false;
    }

    const char* status  = doc["status"]  | "fail";
    const char* city    = doc["city"]    | "?";
    const char* country = doc["country"] | "?";
    const char* tz      = doc["timezone"] | "UTC";
    float lat           = doc["lat"]     | 0.0f;
    float lon           = doc["lon"]     | 0.0f;

    Serial.printf("[geo] Status   : %s\n", status);
    Serial.printf("[geo] City     : %s, %s\n", city, country);
    Serial.printf("[geo] Lat/Lon  : %.4f / %.4f\n", lat, lon);
    Serial.printf("[geo] Timezone : %s\n", tz);

    if (strcmp(status, "success") != 0) {
        Serial.println("[geo] ERROR: API returned non-success");
        drawStatus("Geolocation API error", 0xFF4444);
        delay(1500);
        return false;
    }

    NVSConfig::saveLocation(lat, lon, tz);
    Serial.println("[geo] Location saved to NVS.");

    char msg[80];
    snprintf(msg, sizeof(msg), "Location: %s, %s", city, country);
    drawStatus(msg, 0x00FF88);
    delay(1200);

    Serial.println("[geo] ════════════════════════════════");
    return true;
}

// ── UTC offset — called EVERY boot via Open-Meteo ────────────────────────────

static int32_t fetchUtcOffset(float lat, float lon) {
    Serial.println("[tz] ════════════════════════════════");
    Serial.printf("[tz] Fetching UTC offset for lat=%.4f lon=%.4f\n", lat, lon);
    drawStatus("Fetching timezone offset ...", 0x00D4FF);

    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f&timezone=auto&current_weather=true",
        lat, lon);

    String body = httpGet(url, true);
    if (body.isEmpty()) {
        Serial.println("[tz] ERROR: empty response — defaulting to UTC");
        drawStatus("Timezone fetch failed — using UTC", 0xFF4444);
        delay(1500);
        return 0;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[tz] ERROR: JSON parse failed — %s\n", err.c_str());
        drawStatus("Timezone parse error — using UTC", 0xFF4444);
        delay(1500);
        return 0;
    }

    int32_t     offset = doc["utc_offset_seconds"] | 0;
    const char* tzName = doc["timezone"]             | "UTC";
    const char* tzAbbr = doc["timezone_abbreviation"] | "UTC";

    Serial.printf("[tz] Timezone  : %s\n", tzName);
    Serial.printf("[tz] Abbrevia. : %s\n", tzAbbr);
    Serial.printf("[tz] UTC offset: %+d s  (%+.1f h)\n", offset, offset / 3600.0f);

    char msg[64];
    snprintf(msg, sizeof(msg), "Timezone: %s  (UTC%+.0f h)", tzAbbr, offset / 3600.0f);
    drawStatus(msg, 0x00FF88);
    delay(1000);

    Serial.println("[tz] ════════════════════════════════");
    return offset;
}

// ── NTP sync ──────────────────────────────────────────────────────────────────

static bool syncTime(int32_t utcOffsetSec) {
    Serial.println("[ntp] ════════════════════════════════");
    Serial.printf("[ntp] Syncing NTP  offset=%+d s\n", utcOffsetSec);
    drawStatus("Synchronising time ...", 0x00D4FF);

    configTime(utcOffsetSec, 0, BM_NTP1, BM_NTP2);

    struct tm ti{};
    uint32_t  t0       = millis();
    int       attempts = 0;

    while (!getLocalTime(&ti)) {
        attempts++;
        if (millis() - t0 > 10000) {
            Serial.printf("[ntp] TIMEOUT after %d attempts\n", attempts);
            drawStatus("Time sync failed", 0xFF4444);
            delay(1500);
            return false;
        }
        Serial.printf("[ntp] Waiting... attempt %d  (%lu ms elapsed)\n",
                      attempts, millis() - t0);
        delay(300);
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%d %b %Y  %H:%M:%S", &ti);
    Serial.printf("[ntp] Synced after %d attempts: %s\n", attempts, buf);

    char msg[80];
    snprintf(msg, sizeof(msg), "Time: %s", buf);
    drawStatus(msg, 0x00FF88);
    delay(1200);

    Serial.println("[ntp] ════════════════════════════════");
    return true;
}

// ── WiFi connect ──────────────────────────────────────────────────────────────

static bool connectWiFi(const char* ssid, const char* pass,
                         uint32_t timeoutMs = 15000) {
    Serial.println("[wifi] ════════════════════════════════");
    Serial.printf("[wifi] Connecting to: %s\n", ssid);

    // Already connected (e.g. from on-screen WiFi setup) — skip reconnect
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] Already connected  IP=%s\n",
                      WiFi.localIP().toString().c_str());
        if (MDNS.begin(WIFI_HOSTNAME))
            Serial.printf("[mdns] Started — http://%s.local\n", WIFI_HOSTNAME);
        char msg[80];
        snprintf(msg, sizeof(msg), "WiFi OK  %s  (%d dBm)",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
        drawStatus(msg, 0x00FF88);
        delay(800);
        Serial.println("[wifi] ════════════════════════════════");
        return true;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t t0   = millis();
    int      dots = 0;
    char     msg[80];

    while (WiFi.status() != WL_CONNECTED) {
        uint32_t elapsed = millis() - t0;
        if (elapsed > timeoutMs) {
            Serial.printf("[wifi] TIMEOUT after %lu ms  status=%d\n",
                          elapsed, WiFi.status());
            return false;
        }
        Serial.printf("[wifi] status=%d  elapsed=%lu ms\n",
                      WiFi.status(), elapsed);
        snprintf(msg, sizeof(msg), "Connecting to %s%s",
                 ssid, (dots++ % 2) ? " ..." : "    ");
        drawStatus(msg, 0x00D4FF);
        delay(500);
    }

    Serial.printf("[wifi] Connected!  IP=%s  RSSI=%d dBm  channel=%d\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(), WiFi.channel());

    // mDNS — device reachable at http://<WIFI_HOSTNAME>.local
    if (MDNS.begin(WIFI_HOSTNAME)) {
        Serial.printf("[mdns] Started — http://%s.local\n", WIFI_HOSTNAME);
    } else {
        Serial.println("[mdns] Failed to start");
    }

    // Let the WiFi/IP stack fully settle before HTTPS calls
    Serial.println("[wifi] Settling 2 s...");
    delay(2000);

    snprintf(msg, sizeof(msg), "WiFi OK  %s  (%d dBm)",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    drawStatus(msg, 0x00FF88);
    delay(800);

    Serial.println("[wifi] ════════════════════════════════");
    return true;
}

// ── Entry point ───────────────────────────────────────────────────────────────

inline void run() {
    Serial.println("\n[boot] ╔══════════════════════════════╗");
    Serial.println(  "[boot] ║   Boot Manager starting      ║");
    Serial.println(  "[boot] ╚══════════════════════════════╝");

    checkFactoryReset();

    // ── Step 1: check WiFi credentials ───────────────────────────────────────
    NVSConfig::WiFiCreds creds = NVSConfig::loadWiFi();
    Serial.printf("[boot] WiFi credentials : %s\n",
                  creds.valid ? "FOUND" : "NOT FOUND");

    if (!creds.valid) {
        Serial.println("[boot] → No credentials — launching LVGL WiFi setup");
        lv_init();
        lvgl_setup();
        _wifi_reboot_on_save = true;
        wifi_page_open();
        while (true) { lv_timer_handler(); delay(5); }
        // Device reboots after saving credentials — never reaches here
    }

    Serial.printf("[boot] → SSID: %s\n", creds.ssid);

    // ── Step 2: connect WiFi (up to 3 attempts) ──────────────────────────────
    bool wifiOk = false;
    for (int attempt = 1; attempt <= 3 && !wifiOk; attempt++) {
        Serial.printf("[boot] WiFi attempt %d/3\n", attempt);
        wifiOk = connectWiFi(creds.ssid, creds.password);
        if (!wifiOk && attempt < 3) {
            Serial.println("[boot] Retrying in 5 s...");
            drawStatus("WiFi failed — retrying ...", 0xFFAA00);
            WiFi.disconnect(true);
            delay(5000);
        }
    }
    if (!wifiOk) {
        rebootIn("No WiFi");
    }

    // ── Step 3: geolocation (first boot only) ─────────────────────────────────
    NVSConfig::LocationData loc = NVSConfig::loadLocation();
    Serial.printf("[boot] Location in NVS  : %s\n",
                  loc.valid ? "FOUND" : "NOT FOUND");

    if (!loc.valid) {
        Serial.println("[boot] → First boot: fetching geolocation");
        if (fetchGeoLocation()) {
            loc = NVSConfig::loadLocation();
            Serial.printf("[boot] → Location cached: lat=%.4f  lon=%.4f  tz=%s\n",
                          loc.lat, loc.lon, loc.timezone);
        } else {
            Serial.println("[boot] → Geolocation failed — will use UTC");
        }
    } else {
        Serial.printf("[boot] → Cached location: lat=%.4f  lon=%.4f  tz=%s\n",
                      loc.lat, loc.lon, loc.timezone);
    }

    // ── Step 4: fetch current UTC offset (every boot) ─────────────────────────
    int32_t offset = 0;
    if (loc.valid) {
        offset = fetchUtcOffset(loc.lat, loc.lon);
    } else {
        Serial.println("[boot] → No location — using UTC offset 0");
    }

    // ── Step 5: NTP sync ──────────────────────────────────────────────────────
    if (!syncTime(offset)) {
        rebootIn("No NTP sync");
    }

    Serial.println("\n[boot] ╔══════════════════════════════╗");
    Serial.println(  "[boot] ║   Boot sequence complete     ║");
    Serial.println(  "[boot] ╚══════════════════════════════╝\n");

    delay(500);
    tft.fillRect(0, STATUS_Y, 800, STATUS_H, TFT_BLACK);
}

} // namespace BootManager

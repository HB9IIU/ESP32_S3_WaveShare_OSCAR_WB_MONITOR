/*
 * main.cpp — OSCAR-100 WB Spectrum Monitor + BATC Chat Viewer
 *
 * Hardware : Waveshare ESP32-S3-Touch-LCD-7 (800×480, 8 MB PSRAM)
 * Display  : LovyanGFX — RGB parallel bus
 *
 * Two pages, switched by tapping the bottom-left corner (x<80, y>430):
 *   SPECTRUM — full-screen FFT waterfall (WSS /wb/fft)
 *   CHAT     — BATC QO-100 WB chat viewer (EIO4 + WSS)
 *
 * Both WebSocket connections run concurrently at all times.
 * Switching pages triggers a full-screen redraw; no data is lost.
 *
 * Chat reconnect: EIO session IDs are single-use.  On chat WebSocket
 * disconnect we re-run the polling handshake to obtain a fresh SID
 * (blocks ~3-6 s; FFT misses frames but recovers immediately after).
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "HB9IIUdisplayInit.h"
#include <LvglScreenshot.h>
#include "boot_manager.h"
#include "spectrum_display.h"
#include "chat_display.h"
#include "lv_driver.h"
#include "weatherData.h"
#include "weatherFetch.h"
#include "weather_page.h"

LGFX tft;
static LvglScreenshot screenshot;

// ── Page ──────────────────────────────────────────────────────────────────────
enum class Page : uint8_t { SPECTRUM, CHAT, WEATHER };
static Page _page = Page::SPECTRUM;
static bool _redraw_spectrum = false;

static void _weather_close_cb() {
    _page = Page::SPECTRUM;
    _redraw_spectrum = true;  // defer: LVGL still flushes dirty regions after this returns
}

// ── FFT WebSocket ─────────────────────────────────────────────────────────────
static WebSocketsClient ws;

static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
    case WStype_BIN:
        if (_page == Page::SPECTRUM)
            SpectrumDisplay::handleFFTData(payload, length);
        break;
    case WStype_CONNECTED:    Serial.println("[fft] Connected");    break;
    case WStype_DISCONNECTED: Serial.println("[fft] Disconnected"); break;
    case WStype_ERROR:        Serial.println("[fft] Error");        break;
    default: break;
    }
}

// ── Chat EIO4 polling handshake ───────────────────────────────────────────────
static WebSocketsClient wsChat;
static char     _sid[64]            = "";
static bool     _chatNeedsReconnect = false;
static uint32_t _chatReconnectAfter = 0;

static String pollRequest(const char* method, const char* body = nullptr) {
    char url[300];
    if (_sid[0])
        snprintf(url, sizeof(url),
            "https://eshail.batc.org.uk/wb/chat/socket.io/"
            "?EIO=4&transport=polling&sid=%s&room=eshail-wb", _sid);
    else
        snprintf(url, sizeof(url),
            "https://eshail.batc.org.uk/wb/chat/socket.io/"
            "?EIO=4&transport=polling&room=eshail-wb");

    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    http.begin(sc, url);
    http.addHeader("Origin", "https://eshail.batc.org.uk");
    http.setTimeout(10000);

    int code;
    if (body) {
        http.addHeader("Content-Type", "text/plain;charset=UTF-8");
        code = http.POST((uint8_t*)body, strlen(body));
    } else {
        code = http.GET();
    }
    String resp = http.getString();
    http.end();
    Serial.printf("[poll] %s HTTP%d (%u): %.200s\n",
                  method, code, resp.length(), resp.c_str());
    return (code == 200) ? resp : "";
}

static void parsePollEvents(const char* resp) {
    const char* p = resp;
    while ((p = strstr(p, "42[")) != nullptr) {
        ChatDisplay::handleEvent((uint8_t*)(p + 2), strlen(p + 2));
        p += 3;
    }
}

static bool fetchSid() {
    String open = pollRequest("GET");
    if (open.isEmpty()) return false;
    int jStart = open.indexOf('{');
    if (jStart < 0) return false;
    JsonDocument doc;
    if (deserializeJson(doc, open.c_str() + jStart) != DeserializationError::Ok) return false;
    const char* sid = doc["sid"] | "";
    if (!sid[0]) return false;
    strlcpy(_sid, sid, sizeof(_sid));
    Serial.printf("[poll] sid = %s\n", _sid);
    pollRequest("POST", "40");
    String events = pollRequest("GET");
    if (!events.isEmpty()) parsePollEvents(events.c_str());
    return true;
}

// ── Chat WebSocket ────────────────────────────────────────────────────────────
static void wsChatEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

    case WStype_CONNECTED:
        Serial.printf("[chat] Connected t=%lu\n", millis());
        ChatDisplay::onConnected();
        wsChat.sendTXT("2probe");
        break;

    case WStype_DISCONNECTED:
        Serial.printf("[chat] Disconnected t=%lu — scheduling SID refresh\n", millis());
        ChatDisplay::onDisconnected();
        _sid[0]             = '\0';
        _chatNeedsReconnect = true;
        _chatReconnectAfter = millis() + 10000;  // brief pause before re-handshake
        break;

    case WStype_TEXT: {
        if (length == 0) break;
        char first = (char)payload[0];
        if (length == 6 && memcmp(payload, "3probe", 6) == 0) {
            wsChat.sendTXT("5");
            ChatDisplay::onSIOConnected();
        } else if (first == '2') {
            wsChat.sendTXT("3");
        } else if (length >= 2 && first == '4' && payload[1] == '2') {
            ChatDisplay::handleEvent(payload + 2, length - 2);
        }
        break;
    }
    default: break;
    }
}

static void connectChatWs() {
    char wsPath[256];
    snprintf(wsPath, sizeof(wsPath),
        "/wb/chat/socket.io/?EIO=4&transport=websocket&sid=%s&room=eshail-wb", _sid);
    Serial.printf("[chat] WS path: %s\n", wsPath);
    wsChat.beginSSL("eshail.batc.org.uk", 443, wsPath);
    wsChat.onEvent(wsChatEvent);
    wsChat.setReconnectInterval(3600000);  // disable auto-reconnect — SID is single-use
    wsChat.setExtraHeaders("Origin: https://eshail.batc.org.uk");
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(300);
    Serial.println("\n=== OSCAR-100 WB Spectrum + Chat ===");

    initTFT();

    Serial.println("[fs] Mounting LittleFS...");
    if (LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.printf("[fs] Mounted OK — total=%u used=%u\n",
                      LittleFS.totalBytes(), LittleFS.usedBytes());
        Serial.println("[fs] Root listing:");
        File root = LittleFS.open("/");
        File f = root.openNextFile();
        while (f) {
            Serial.printf("[fs]   %s (%u bytes)\n", f.name(), f.size());
            f = root.openNextFile();
        }
        bool splashExists = LittleFS.exists("/splash.jpg");
        Serial.printf("[fs] /splash.jpg exists: %s\n", splashExists ? "YES" : "NO");
        if (splashExists) {
            Serial.println("[fs] Drawing splash...");
            bool ok = tft.drawJpgFile(LittleFS, "/splash.jpg", 0, 0, 800, 480);
            Serial.printf("[fs] drawJpgFile returned: %s\n", ok ? "true" : "false");
        }
        // LittleFS stays mounted for weather backgrounds and icons
    } else {
        Serial.println("[fs] LittleFS.begin() FAILED");
    }

    BootManager::run();

    lv_init();
    lvgl_setup();
    weather_fetch_start();

    // Spectrum is the default page — init its sprites and static UI
    SpectrumDisplay::init();

    // Screenshot server: start after all PSRAM allocations (LVGL bufs + sprites)
    // and hook into the LVGL flush callback via a function pointer (not a direct
    // include) to keep WebServer.h out of lv_driver.h / boot_manager.h.
    if (screenshot.begin(800, 480)) {
        // Capture on demand from LovyanGFX's PSRAM framebuffer — works for
        // all rendering paths (LVGL and direct sprite/pushPixels writes).
        screenshot.setPreServeFn([](uint16_t* buf, uint16_t w, uint16_t h) {
            tft.readRect(0, 0, w, h, buf);
        });
    }

    // FFT WebSocket
    ws.beginSSL("eshail.batc.org.uk", 443, "/wb/fft", "", "");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(5000);

    // Chat: EIO polling handshake (3 HTTPS calls, ~3-6 s)
    Serial.println("[chat] Fetching EIO sid...");
    if (fetchSid()) {
        connectChatWs();
    } else {
        Serial.println("[chat] sid fetch failed — retrying in 10 s");
        _chatNeedsReconnect = true;
        _chatReconnectAfter = millis() + 10000;
    }
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    ws.loop();
    wsChat.loop();
    screenshot.loop();

    if (_page == Page::WEATHER) {
        lv_timer_handler();
        if (_wthr_fetch_ready) {
            _wthr_fetch_ready = false;
            weather_page_update_data(_wthr_fetch_data);
        }
    }

    // Deferred: redraw spectrum after lv_timer_handler() fully returns
    // (LVGL flushes dirty regions after the close callback, which would
    // overwrite SpectrumDisplay::redraw() if called directly from the callback)
    if (_redraw_spectrum) {
        _redraw_spectrum = false;
        SpectrumDisplay::redraw();
    }

    // Chat reconnect after SID expiry.  fetchSid() blocks ~3-6 s (3 HTTPS calls);
    // FFT misses frames during this window but recovers on the next packet.
    if (_chatNeedsReconnect && millis() >= _chatReconnectAfter) {
        _chatNeedsReconnect = false;
        Serial.println("[chat] Reconnecting — fetching new sid...");
        if (fetchSid()) {
            connectChatWs();
        } else {
            Serial.println("[chat] sid failed — retry in 30 s");
            _chatNeedsReconnect = true;
            _chatReconnectAfter = millis() + 30000;
        }
    }

    // ── Touch ─────────────────────────────────────────────────────────────────
    uint16_t tx = 0, ty = 0;
    bool touched = tft.getTouch(&tx, &ty);

    // Continuous natural scroll — only active on chat page
    if (_page == Page::CHAT) {
        static bool     prevDown = false;
        static uint16_t prevY    = 0;
        static int      accumPx  = 0;
        if (touched) {
            if (!prevDown) { prevY = ty; accumPx = 0; }
            else {
                accumPx += (int)ty - (int)prevY;
                prevY = ty;
                int msgs = accumPx / ChatDisplay::SCROLL_PX_PER_MSG;
                if (msgs != 0) {
                    ChatDisplay::scroll(msgs);
                    accumPx -= msgs * ChatDisplay::SCROLL_PX_PER_MSG;
                }
            }
            prevDown = true;
        } else { prevDown = false; accumPx = 0; }
    }

    // Rising-edge tap actions — page-specific blocks, no cross-page conflicts
    static bool lastTouch = false;
    if (touched && !lastTouch) {

        if (_page == Page::SPECTRUM) {
            if (ty >= 25 && ty < 61) {                            // nav button strip
                if      (tx < 267)  { _page = Page::CHAT; ChatDisplay::init(); }
                else if (tx < 534)  SpectrumDisplay::toggleWaterfall();
                else                { _page = Page::WEATHER; weather_page_open(_weather_close_cb); }
            }
            else if (tx >= 44 && tx <= 220 && ty >= 61 && ty < 336) // beacon area → cycle color
                SpectrumDisplay::cycleSpectrumColor();
            else if (ty >= 336 && ty < 436) {                     // waterfall row → brightness
                if      (tx < 400) SpectrumDisplay::adjustWfBrightness(-10);
                else if (tx > 400) SpectrumDisplay::adjustWfBrightness(+10);
            }
            else if (ty >= 436) {                                  // bandplan corner buttons
                if      (tx < 60)  SpectrumDisplay::adjustWfBrightness(-10);
                else if (tx > 740) SpectrumDisplay::adjustWfBrightness(+10);
            }
        }
        else {  // Page::CHAT
            if (tx >= 631 && ty >= 458)                           // nick-panel button → SPECTRUM
                { ChatDisplay::suspend(); _page = Page::SPECTRUM; SpectrumDisplay::redraw(); }
        }
    }
    lastTouch = touched;
}

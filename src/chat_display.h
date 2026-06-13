#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <time.h>
#include "HB9IIUdisplayInit.h"
#include "DejaVuSans18_Latin.h"

extern LGFX tft;

namespace ChatDisplay {

// ── Layout (800 × 480) ───────────────────────────────────────────────────────
//   x  0..629  (630px) Chat messages  (DejaVu18)
//   x    630   (1px)   Vertical divider
//   x 631..799 (169px) Nick list  (DejaVu12)

static constexpr int CONTENT_Y = 0;
static constexpr int CONTENT_H = 480;

static constexpr int CHAT_W    = 630;
static constexpr int DIV_X     = 630;
static constexpr int NICK_X    = 631;
static constexpr int NICK_W    = 169;

// Chat column positions — sized for DejaVu18 (~11 px/char)
static constexpr int COL_TIME  = 4;
static constexpr int COL_NICK  = 80;
static constexpr int COL_MSG   = 230;
static constexpr int COL_MSG_W = CHAT_W - COL_MSG - 4;  // 396

static constexpr int LINE_H      = 22;  // DejaVuSans18_Latin yAdvance (matches fonts::DejaVu18)
static constexpr int MSG_GAP     = 6;   // extra px before first line of each message
static constexpr int NICK_LINE_H = 16;  // nick list stays at DejaVu12

// ── Ring buffer of display lines ─────────────────────────────────────────────
static constexpr int MAX_LINES = 300;

struct Line {
    char time[6];   // "HH:MM" or ""  (first wrapped line of each message only)
    char nick[22];  // callsign or ""  (first wrapped line of each message only)
    char msg[220];  // one wrapped row
};

static Line _lines[MAX_LINES];
static int  _head          = 0;  // index of oldest stored line
static int  _count         = 0;  // number of stored lines
static int  _msgScrollOffset = 0; // messages hidden from the bottom (0 = show newest)
static bool _renderEnabled   = false; // suppresses TFT writes until init() — prevents
                                      // corrupting the spectrum page during background updates

// ── Nick list ─────────────────────────────────────────────────────────────────
static constexpr int MAX_NICKS = 60;
static char _nicks[MAX_NICKS][22];
static int  _nickCount = 0;

// ── Helpers ──────────────────────────────────────────────────────────────────
inline uint16_t c(uint8_t r, uint8_t g, uint8_t b) {
    return tft.color565(r, g, b);
}

static int lineAt(int i) {
    return (_head + i) % MAX_LINES;
}

static void pushLine(const char* t, const char* n, const char* m) {
    int idx;
    if (_count < MAX_LINES) {
        idx = (_head + _count) % MAX_LINES;
        _count++;
    } else {
        idx = _head;
        _head = (_head + 1) % MAX_LINES;
    }
    strlcpy(_lines[idx].time, t, sizeof(_lines[idx].time));
    strlcpy(_lines[idx].nick, n, sizeof(_lines[idx].nick));
    strlcpy(_lines[idx].msg,  m, sizeof(_lines[idx].msg));
}

// Count total distinct messages (lines where time[0] is set)
static int countMessages() {
    int n = 0;
    for (int i = 0; i < _count; i++)
        if (_lines[lineAt(i)].time[0]) n++;
    return n;
}

// ── Word-wrap a message into display lines ────────────────────────────────────
static void addMessage(const char* timeStr, const char* nick, const char* msgIn) {
    tft.setFont(&DejaVuSans18_Latin);

    char buf[512];
    strlcpy(buf, msgIn, sizeof(buf));

    char line[220] = "";
    bool firstLine = true;

    char* sp;
    char* word = strtok_r(buf, " ", &sp);

    while (word) {
        char test[220];
        if (line[0])
            snprintf(test, sizeof(test), "%s %s", line, word);
        else
            strlcpy(test, word, sizeof(test));

        if ((int)tft.textWidth(test) > COL_MSG_W && line[0]) {
            pushLine(firstLine ? timeStr : "", firstLine ? nick : "", line);
            firstLine = false;
            strlcpy(line, word, sizeof(line));
        } else {
            strlcpy(line, test, sizeof(line));
        }
        word = strtok_r(nullptr, " ", &sp);
    }
    if (line[0])
        pushLine(firstLine ? timeStr : "", firstLine ? nick : "", line);
}

// ── Extract HH:MM ─────────────────────────────────────────────────────────────
static void extractTime(JsonVariantConst v, char* out, size_t outLen) {
    out[0] = '\0';
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        const char* t = strchr(s, 'T');
        if (t)
            strlcpy(out, t + 1, min(outLen, (size_t)6));
        else
            strlcpy(out, s, min(outLen, (size_t)6));
    } else if (v.is<long>()) {
        time_t ts = (time_t)v.as<long>();
        struct tm* ti = localtime(&ts);
        if (ti) strftime(out, outLen, "%H:%M", ti);
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────

static void drawChat() {
    if (!_renderEnabled) return;
    const uint16_t BG = c(12, 14, 22);
    tft.fillRect(0, CONTENT_Y, CHAT_W, CONTENT_H, BG);

    tft.setFont(&DejaVuSans18_Latin);
    tft.setTextDatum(TL_DATUM);

    if (_count == 0) return;

    // ── Step 1: compute effectiveEnd by skipping _msgScrollOffset whole messages
    // from the tail.  effectiveEnd is the first line index of the first hidden
    // message (or _count when scroll is 0).
    int effectiveEnd = _count;
    {
        int skipped = 0;
        for (int i = _count - 1; i >= 0 && skipped < _msgScrollOffset; i--) {
            if (_lines[lineAt(i)].time[0]) skipped++;
            effectiveEnd = i;
        }
    }
    if (effectiveEnd == 0) return;

    // ── Step 2: fill viewport with complete messages, scanning newest → oldest.
    // Lines are accumulated per-message; the whole message is committed only
    // when its header is reached.  This guarantees:
    //   • startIdx always lands on a message header — no orphaned continuation lines
    //   • height estimate matches the forward render exactly — no clip-guard overflow
    //   • newest messages are never lost off the bottom
    int startIdx = effectiveEnd;
    {
        int totalH = 0;
        int msgH   = 0;  // height accumulated for the message currently being scanned
        for (int i = effectiveEnd - 1; i >= 0; i--) {
            msgH += LINE_H;
            if (_lines[lineAt(i)].time[0]) {
                // Reached the header — we now know the full height of this message.
                int gap = (totalH > 0) ? MSG_GAP : 0;
                if (totalH + gap + msgH > CONTENT_H) break;  // whole message doesn't fit
                totalH  += gap + msgH;
                startIdx = i;
                msgH     = 0;  // reset for the next (older) message
            }
        }
    }

    // ── Step 3: render startIdx..effectiveEnd-1  (oldest → newest, top → bottom)
    int y = CONTENT_Y + 2;
    for (int i = startIdx; i < effectiveEnd; i++) {
        const Line& L = _lines[lineAt(i)];

        if (L.time[0] && i > startIdx) y += MSG_GAP;

        if (L.time[0]) {
            tft.setTextColor(c(80, 85, 100), BG);
            tft.drawString(L.time, COL_TIME, y);
        }
        if (L.nick[0]) {
            tft.setTextColor(c(220, 185, 50), BG);
            tft.drawString(L.nick, COL_NICK, y);
        }
        if (L.msg[0]) {
            tft.setTextColor(c(195, 200, 210), BG);
            tft.drawString(L.msg, COL_MSG, y);
        }
        y += LINE_H;
    }

    // ── Scroll position bar (right edge of chat column, visible when scrolled)
    if (_msgScrollOffset > 0) {
        int total = countMessages();
        if (total > 0) {
            float ratio = 1.0f - (float)_msgScrollOffset / total;
            int barH = max(10, (int)(CONTENT_H * ratio));
            int barY = CONTENT_Y + (CONTENT_H - barH);
            tft.fillRect(CHAT_W - 4, CONTENT_Y, 4, CONTENT_H, c(25, 28, 42));
            tft.fillRect(CHAT_W - 4, barY, 4, barH, c(70, 100, 160));
        }
    }
}

static void drawNickList() {
    if (!_renderEnabled) return;
    const uint16_t BG = c(10, 12, 20);
    tft.fillRect(NICK_X, CONTENT_Y, NICK_W, CONTENT_H, BG);
    tft.drawFastVLine(DIV_X, 0, 480, c(40, 45, 65));

    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(TL_DATUM);

    tft.setTextColor(c(70, 110, 170), BG);
    tft.drawString("USERS", NICK_X + 6, CONTENT_Y + 2);
    tft.drawFastHLine(NICK_X, CONTENT_Y + NICK_LINE_H, NICK_W, c(35, 40, 60));

    // Reserve bottom 22px for the "back" button — nick rows stop before it
    static constexpr int BTN_H   = 22;
    static constexpr int BTN_Y   = CONTENT_H - BTN_H;   // y=458
    int maxVis = (BTN_Y - NICK_LINE_H - 4) / NICK_LINE_H;
    int show   = min(_nickCount, maxVis);
    for (int i = 0; i < show; i++) {
        tft.setTextColor(c(150, 195, 150), BG);
        tft.drawString(_nicks[i], NICK_X + 6, CONTENT_Y + NICK_LINE_H + 4 + i * NICK_LINE_H);
    }

    // "Back to spectrum" button — tap here to switch page
    tft.drawFastHLine(NICK_X, BTN_Y, NICK_W, c(40, 45, 65));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(c(100, 160, 220), BG);
    tft.drawString("< SPECTRUM", NICK_X + NICK_W / 2, BTN_Y + BTN_H / 2);
    tft.setTextDatum(TL_DATUM);
}


// ── Public API ────────────────────────────────────────────────────────────────
inline void init() {
    _renderEnabled = true;
    tft.fillScreen(c(10, 12, 20));
    drawChat();
    drawNickList();
}

// Call when leaving the chat page — suppresses all TFT writes from background events.
inline void suspend() {
    _renderEnabled = false;
}

inline void onConnected()    {}
inline void onSIOConnected() {}
inline void onDisconnected() {}

// Scroll by deltaMsgs whole messages (positive = older, negative = newer).
// Exposed as static constexpr so main.cpp can tune the swipe sensitivity.
static constexpr int SCROLL_PX_PER_MSG = 30;  // pixels of swipe per message scrolled

inline void scroll(int deltaMsgs) {
    if (_count == 0 || deltaMsgs == 0) return;
    int total = countMessages();
    _msgScrollOffset = max(0, min(total - 1, _msgScrollOffset + deltaMsgs));
    drawChat();
}

inline void handleEvent(uint8_t* payload, size_t length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("[json] parse error: %s\n", err.c_str());
        return;
    }
    if (!doc.is<JsonArray>() || doc.size() < 2) return;

    const char* event = doc[0] | "";

    // ── "message" ────────────────────────────────────────────────────────────
    if (strcmp(event, "message") == 0) {
        JsonObject obj = doc[1].as<JsonObject>();
        const char* nick = obj["nick"] | obj["name"] | obj["user"] | "?";
        const char* msg  = obj["msg"] | obj["message"] | obj["text"] | obj["m"] | "";
        if (!msg[0]) return;

        char timeStr[6] = "";
        extractTime(obj["time"], timeStr, sizeof(timeStr));
        char nickBuf[22];
        strlcpy(nickBuf, nick, sizeof(nickBuf));

        addMessage(timeStr, nickBuf, msg);
        _msgScrollOffset = 0;  // auto-scroll to bottom for incoming messages
        drawChat();
    }

    // ── "history" ────────────────────────────────────────────────────────────
    else if (strcmp(event, "history") == 0) {
        JsonVariant data = doc[1];

        JsonArray histArr;
        if (data.is<JsonObject>()) {
            JsonObject obj = data.as<JsonObject>();

            if (obj["nicks"].is<JsonArray>()) {
                _nickCount = 0;
                for (JsonVariant v : obj["nicks"].as<JsonArray>()) {
                    if (_nickCount >= MAX_NICKS) break;
                    if (v.is<const char*>())
                        strlcpy(_nicks[_nickCount++], v.as<const char*>(), 22);
                    else if (v.is<JsonObject>()) {
                        const char* n = v["nick"] | v["name"] | v["user"] | "?";
                        strlcpy(_nicks[_nickCount++], n, 22);
                    }
                }
                drawNickList();
            }
            histArr = obj["history"].as<JsonArray>();
        } else {
            histArr = data.as<JsonArray>();
        }

        size_t n = histArr.size();
        if (n > 0) {
            Serial.printf("[history] %u messages  first=%s  last=%s\n",
                (unsigned)n,
                histArr[0]["time"]   | histArr[0]["t"]   | "?",
                histArr[n-1]["time"] | histArr[n-1]["t"] | "?");
        }

        for (JsonObject item : histArr) {
            const char* nick = item["nick"] | item["name"] | item["user"] | "?";
            const char* msg  = item["msg"] | item["message"] | item["text"] | item["m"] | "";
            if (!msg[0]) continue;
            char timeStr[6] = "";
            extractTime(item["time"], timeStr, sizeof(timeStr));
            char nickBuf[22];
            strlcpy(nickBuf, nick, sizeof(nickBuf));
            addMessage(timeStr, nickBuf, msg);
        }
        drawChat();
    }

    // ── "nicks" ──────────────────────────────────────────────────────────────
    else if (strcmp(event, "nicks") == 0) {
        _nickCount = 0;
        JsonVariant data = doc[1];

        JsonArray arr;
        if (data.is<JsonObject>())
            arr = data["nicks"].as<JsonArray>();
        else
            arr = data.as<JsonArray>();

        for (JsonVariant v : arr) {
            if (_nickCount >= MAX_NICKS) break;
            if (v.is<const char*>())
                strlcpy(_nicks[_nickCount++], v.as<const char*>(), 22);
            else if (v.is<JsonObject>()) {
                const char* n = v["nick"] | v["name"] | v["user"] | "?";
                strlcpy(_nicks[_nickCount++], n, 22);
            }
        }
        drawNickList();
    }


}

} // namespace ChatDisplay

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "HB9IIUdisplayInit.h"
#include "nvs_config.h"

extern LGFX tft;

namespace WiFiSetup {

static inline uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return tft.color888(r, g, b); }

// ── Network list ──────────────────────────────────────────────────────────────
static constexpr int NL_HDR = 50, NL_ROW = 54, NL_VIS = 7, NL_FTR = 52;

struct Net { char ssid[33]; int8_t rssi; bool open; };
static Net _nets[24];
static int _nCnt = 0, _nScr = 0;

static void drainTouch() {
    uint16_t tx, ty;
    while (tft.getTouch(&tx, &ty)) delay(20);
    delay(50);
}

static void doScan() {
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Scanning WiFi networks...", 400, 240);
    tft.setTextDatum(TL_DATUM);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(200);
    int n = WiFi.scanNetworks(false, false, false, 400);

    _nCnt = 0; _nScr = 0;
    for (int i = 0; i < n && _nCnt < 24; i++) {
        if (WiFi.SSID(i).isEmpty()) continue;
        strlcpy(_nets[_nCnt].ssid, WiFi.SSID(i).c_str(), 33);
        _nets[_nCnt].rssi = WiFi.RSSI(i);
        _nets[_nCnt].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        _nCnt++;
    }
    // Sort by RSSI descending (bubble sort — list is short)
    for (int i = 0; i < _nCnt - 1; i++)
        for (int j = 0; j < _nCnt - 1 - i; j++)
            if (_nets[j].rssi < _nets[j+1].rssi)
                { Net t = _nets[j]; _nets[j] = _nets[j+1]; _nets[j+1] = t; }
    WiFi.scanDelete();
}

static void drawNetList() {
    tft.fillScreen(TFT_BLACK);
    // Header
    tft.fillRect(0, 0, 800, NL_HDR, C(20, 60, 120));
    tft.setFont(&fonts::DejaVu24);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, C(20, 60, 120));
    tft.drawString("Select WiFi Network", 16, NL_HDR / 2);
    tft.fillRoundRect(668, 8, 124, 34, 5, C(50, 50, 100));
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, C(50, 50, 100));
    tft.drawString("Rescan", 730, 25);

    // Network rows
    for (int i = 0; i < NL_VIS; i++) {
        int idx = _nScr + i;
        int y   = NL_HDR + i * NL_ROW;
        uint16_t bg = (i % 2) ? C(28, 28, 28) : C(18, 18, 18);
        tft.fillRect(0, y, 800, NL_ROW, bg);
        if (idx >= _nCnt) continue;

        tft.setFont(&fonts::DejaVu18);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(_nets[idx].ssid, 20, y + NL_ROW / 2);

        if (!_nets[idx].open) {
            tft.setTextDatum(MR_DATUM);
            tft.setTextColor(C(220, 200, 0), bg);
            tft.drawString("LOCK", 724, y + NL_ROW / 2);
        }
        // RSSI bars (4 bars, graduated height)
        int8_t rssi = _nets[idx].rssi;
        int str = (rssi > -55) ? 4 : (rssi > -65) ? 3 : (rssi > -75) ? 2 : 1;
        for (int b = 0; b < 4; b++) {
            uint16_t bc = (b < str) ? C(0, 200, 80) : C(50, 50, 50);
            int bh = 8 + b * 6;
            tft.fillRect(740 + b * 14, y + NL_ROW - 5 - bh, 10, bh, bc);
        }
    }

    // Footer scroll buttons
    int fy = NL_HDR + NL_VIS * NL_ROW;
    tft.fillRect(0, fy, 800, NL_FTR, C(12, 12, 12));
    if (_nScr > 0) {
        tft.fillRoundRect(10, fy + 8, 120, 36, 5, C(40, 80, 120));
        tft.setFont(&fonts::DejaVu18);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, C(40, 80, 120));
        tft.drawString("^ Up", 70, fy + 26);
    }
    if (_nScr + NL_VIS < _nCnt) {
        tft.fillRoundRect(140, fy + 8, 120, 36, 5, C(40, 80, 120));
        tft.setFont(&fonts::DejaVu18);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, C(40, 80, 120));
        tft.drawString("v Down", 200, fy + 26);
    }
    char cntBuf[24];
    snprintf(cntBuf, sizeof(cntBuf), "%d networks", _nCnt);
    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C(100, 100, 100), C(12, 12, 12));
    tft.drawString(cntBuf, 790, fy + NL_FTR / 2);
    tft.setTextDatum(TL_DATUM);
}

// Returns net index, or -1 for rescan
static int pickNetwork() {
    drainTouch();
    bool last = false;
    while (true) {
        uint16_t tx, ty;
        bool t = tft.getTouch(&tx, &ty);
        if (t && !last) {
            // Rescan button
            if (ty < NL_HDR && tx >= 668) return -1;
            // Network row
            if (ty >= NL_HDR && ty < NL_HDR + NL_VIS * NL_ROW) {
                int idx = _nScr + (ty - NL_HDR) / NL_ROW;
                if (idx < _nCnt) return idx;
            }
            // Scroll buttons
            int fy = NL_HDR + NL_VIS * NL_ROW;
            if (ty >= fy) {
                if (tx < 140 && _nScr > 0)               { _nScr--; drawNetList(); }
                else if (tx >= 140 && _nScr+NL_VIS<_nCnt){ _nScr++; drawNetList(); }
            }
        }
        last = t;
        delay(30);
    }
}

// ── On-screen keyboard ────────────────────────────────────────────────────────
// Three modes: 0=lowercase  1=uppercase  2=symbols/numbers
static const char* const KB_ROWS[3][3] = {
    { "qwertyuiop", "asdfghjkl", "zxcvbnm"    },
    { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"    },
    { "1234567890", "!@#$%^&*()", "-_=+.,;:?/" },
};

static char _pwd[65];
static int  _pwdLen      = 0;
static int  _kbMode      = 0;   // 0=lower  1=upper  2=symbols
static int  _kbLastAlpha = 0;   // remembers lower/upper when switching to symbols

static constexpr int KB_Y   = 100;   // keyboard top (below header + password bar)
static constexpr int KB_KH  = 95;    // row height incl. gap
static constexpr int KB_KW  = 74;    // key width
static constexpr int KB_GAP = 5;

// Horizontal margin to center a row of `len` keys
static int kbMargin(int len) {
    return (800 - (len * KB_KW + (len - 1) * KB_GAP)) / 2;
}

static void drawPwdBar(const char* ssid) {
    // Title bar with Back button
    tft.fillRect(0, 0, 800, 50, C(20, 60, 120));
    tft.fillRoundRect(8, 8, 80, 34, 5, C(55, 55, 110));
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, C(55, 55, 110));
    tft.drawString("< Back", 48, 25);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, C(20, 60, 120));
    char buf[56];
    snprintf(buf, sizeof(buf), "Password for: %.32s", ssid);
    tft.drawString(buf, 100, 25);

    // Password input field (shows plaintext — private device)
    tft.fillRect(0, 50, 800, 50, C(20, 20, 20));
    tft.fillRoundRect(8, 56, 784, 38, 5, C(45, 45, 45));
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, C(45, 45, 45));
    char disp[67];
    memcpy(disp, _pwd, _pwdLen);
    disp[_pwdLen] = '_';   // cursor
    disp[_pwdLen + 1] = '\0';
    tft.drawString(disp, 18, 75);
    tft.setTextDatum(TL_DATUM);
}

static void drawKeyboard() {
    tft.fillRect(0, KB_Y, 800, 480 - KB_Y, C(8, 8, 8));

    for (int row = 0; row < 3; row++) {
        const char* r = KB_ROWS[_kbMode][row];
        int len    = strlen(r);
        int margin = kbMargin(len);
        int y      = KB_Y + row * KB_KH;
        for (int k = 0; k < len; k++) {
            int x = margin + k * (KB_KW + KB_GAP);
            tft.fillRoundRect(x, y, KB_KW, KB_KH - KB_GAP, 5, C(62, 62, 68));
            tft.setFont(&fonts::DejaVu24);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_WHITE, C(62, 62, 68));
            char s[2] = { r[k], 0 };
            tft.drawString(s, x + KB_KW / 2, y + (KB_KH - KB_GAP) / 2);
        }
    }

    // Bottom row: [CAPS] [123/ABC] [SPACE] [DEL] [OK]
    // x: 5..90  95..185  190..545  550..660  665..795
    int y4 = KB_Y + 3 * KB_KH;
    int ky = y4 + (KB_KH - KB_GAP) / 2;

    // CAPS — highlighted when uppercase is active
    uint16_t capsBg = (_kbMode == 1) ? C(200, 140,  20) : C(50, 80, 130);
    tft.fillRoundRect(  5, y4,  85, KB_KH - KB_GAP, 5, capsBg);

    // 123 / ABC toggle
    uint16_t symBg = (_kbMode == 2) ? C(80, 60, 160) : C(50, 70, 110);
    const char* symL = (_kbMode == 2) ? "ABC" : "123";
    tft.fillRoundRect( 95, y4,  90, KB_KH - KB_GAP, 5, symBg);

    tft.fillRoundRect(190, y4, 355, KB_KH - KB_GAP, 5, C( 55,  55,  60));
    tft.fillRoundRect(550, y4, 110, KB_KH - KB_GAP, 5, C(130,  50,  50));
    tft.fillRoundRect(665, y4, 130, KB_KH - KB_GAP, 5, C( 30, 120,  50));

    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, capsBg); tft.drawString("CAPS",  47, ky);
    tft.setTextColor(TFT_WHITE, symBg);  tft.drawString(symL,   140, ky);
    tft.setTextColor(TFT_WHITE, C( 55,  55,  60)); tft.drawString("SPACE", 367, ky);
    tft.setTextColor(TFT_WHITE, C(130,  50,  50)); tft.drawString("DEL",   605, ky);
    tft.setTextColor(TFT_WHITE, C( 30, 120,  50)); tft.drawString("OK",    730, ky);
    tft.setTextDatum(TL_DATUM);
}

// Returns: printable char, ' '=space, '\b'=del, '\t'=mode, '\r'=OK, 27=back, 0=miss
static char kbHit(uint16_t tx, uint16_t ty) {
    // Back button in title bar
    if (ty >= 8 && ty <= 42 && tx >= 8 && tx <= 88) return 27;
    if (ty < KB_Y) return 0;

    int row = (int)(ty - KB_Y) / KB_KH;
    if (row < 0) return 0;

    if (row < 3) {
        const char* r = KB_ROWS[_kbMode][row];
        int len    = strlen(r);
        int margin = kbMargin(len);
        int ki = ((int)tx - margin) / (KB_KW + KB_GAP);
        if (ki >= 0 && ki < len) {
            int kx = margin + ki * (KB_KW + KB_GAP);
            if ((int)tx >= kx && (int)tx < kx + KB_KW) return r[ki];
        }
        return 0;
    }

    // Row 3 special keys
    if (tx >=   5 && tx <  90) return '\x01';  // CAPS toggle
    if (tx >=  95 && tx < 185) return '\x02';  // 123/ABC toggle
    if (tx >= 190 && tx < 545) return ' ';
    if (tx >= 550 && tx < 660) return '\b';
    if (tx >= 665 && tx < 795) return '\r';
    return 0;
}

// Returns true = OK, false = Back
static bool enterPassword(const char* ssid) {
    _pwdLen = 0; _pwd[0] = '\0'; _kbMode = 0;
    drawPwdBar(ssid);
    drawKeyboard();
    drainTouch();

    bool last = false;
    while (true) {
        uint16_t tx, ty;
        bool t = tft.getTouch(&tx, &ty);
        if (t && !last) {
            char k = kbHit(tx, ty);
            bool redrawPwd = false;
            if      (k == 27)    return false;
            else if (k == '\r')  return true;
            else if (k == '\b')  { if (_pwdLen > 0) { _pwdLen--; _pwd[_pwdLen] = '\0'; } redrawPwd = true; }
            else if (k == '\x01') {
                // CAPS: toggle between lower (0) and upper (1); no-op in symbols mode
                if (_kbMode != 2) { _kbMode = (_kbMode == 1) ? 0 : 1; _kbLastAlpha = _kbMode; }
                else              { _kbLastAlpha = (_kbLastAlpha == 1) ? 0 : 1; }
                drawKeyboard();
            }
            else if (k == '\x02') {
                // 123/ABC: toggle between symbols (2) and last alpha mode
                _kbMode = (_kbMode == 2) ? _kbLastAlpha : 2;
                drawKeyboard();
            }
            else if (k != 0 && _pwdLen < 64) { _pwd[_pwdLen++] = k; _pwd[_pwdLen] = '\0'; redrawPwd = true; }
            if (redrawPwd) drawPwdBar(ssid);
        }
        last = t;
        delay(30);
    }
}

// ── Connect + result feedback ─────────────────────────────────────────────────
static bool tryConnect(const char* ssid, const char* pass, uint32_t timeoutMs = 20000) {
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char buf[80];
    snprintf(buf, sizeof(buf), "Connecting to  %s ...", ssid);
    tft.drawString(buf, 400, 230);

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t t0 = millis(), dots = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > timeoutMs) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(C(255, 80, 80), TFT_BLACK);
            tft.drawString("Connection failed", 400, 210);
            tft.setFont(&fonts::DejaVu12);
            tft.setTextColor(C(180, 180, 180), TFT_BLACK);
            tft.drawString("Check password and try again", 400, 260);
            delay(2500);
            WiFi.disconnect(true);
            delay(200);
            tft.setTextDatum(TL_DATUM);
            return false;
        }
        // Animated dots
        char prog[32];
        int d = (millis() - t0) / 600 % 4;
        snprintf(prog, sizeof(prog), "%.*s", d, "...");
        tft.setTextColor(C(100, 100, 100), TFT_BLACK);
        tft.drawString(prog, 400, 270);
        delay(200);
    }

    snprintf(buf, sizeof(buf), "Connected!  %s", WiFi.localIP().toString().c_str());
    tft.setTextColor(C(0, 220, 100), TFT_BLACK);
    tft.drawString(buf, 400, 270);
    delay(1500);
    tft.setTextDatum(TL_DATUM);
    return true;
}

// ── Entry point ───────────────────────────────────────────────────────────────
inline void run() {
    while (true) {
        doScan();
        drawNetList();

        int sel = pickNetwork();
        if (sel < 0) continue;  // rescan requested

        if (_nets[sel].open) {
            _pwdLen = 0; _pwd[0] = '\0';
        } else {
            if (!enterPassword(_nets[sel].ssid)) continue;  // user pressed Back
        }

        if (tryConnect(_nets[sel].ssid, _pwd)) {
            NVSConfig::saveWiFi(_nets[sel].ssid, _pwd);
            return;
        }
        // Connection failed — loop back to network list
    }
}

} // namespace WiFiSetup

#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <time.h>
#include "HB9IIUdisplayInit.h"

extern LGFX tft;

namespace SpectrumDisplay {

// ── Layout (800 × 480) ────────────────────────────────────────────────────────
//   0..35   (36px)  Nav buttons (CHAT / WEATHER / WF)
//   36..60  (25px)  Frequency axis
//   61..334 (274px) Spectrum plot
//   335     (1px)   Separator
//   336..435(100px) Waterfall
//   436..479(44px)  Bandplan
static constexpr int BTN_TOP  = 0,   BTN_H  = 36;
static constexpr int FA_TOP   = 36,  FA_H   = 25;
static constexpr int PL_LEFT  = 44,  PL_W   = 756;   // left margin for dB labels
static constexpr int PL_TOP   = 61;
static int           PL_BOT   = 334;   // mutable — changes when waterfall is toggled
static int           PL_H     = 274;
static constexpr int WF_TOP   = 336, WF_H   = 100;
static constexpr int BP_TOP   = 436, BP_H   = 44;

// ── Frequency / level calibration ────────────────────────────────────────────
// Adjust to match the actual BATC FFT stream boundaries
static constexpr float FREQ_START  = 10490.5f;  // MHz, start of displayed span
static constexpr float FREQ_SPAN   = 9.0f;       // MHz, total displayed span
static constexpr float DB_RANGE    = 12.0f;      // dB full scale (0..DB_RANGE)
static constexpr int   SUB_ZERO_PX = 20;         // pixels below 0 dB line (sub-zero zone)

// Raw FFT threshold for signal detection
static constexpr uint16_t SIG_THRESHOLD = 16000;

// ── Spectrum color schemes — {fill R,G,B, peak R,G,B} ────────────────────────
static constexpr uint8_t SCHEMES[][6] = {
    {  0, 160,   0,   60, 255,  90},  // emerald green
    {  0, 140, 150,    0, 230, 245},  // teal  (GQRX-style)
    { 25,  85, 210,  110, 195, 255},  // azure blue
    {155,  25,   0,  255, 155,   0},  // fire  (deep red → gold peak)
    { 95,   0, 165,  215,  75, 255},  // violet
    {155,  85,   0,  255, 210,  40},  // amber
};
static constexpr uint8_t N_SCHEMES = sizeof(SCHEMES) / sizeof(SCHEMES[0]);

// ── Module state ─────────────────────────────────────────────────────────────
static LGFX_Sprite* _spec        = nullptr;
static LGFX_Sprite* _wf          = nullptr;
static bool         _wfVisible   = true;
static uint8_t      _colorScheme = 0;
static float        _beaconRaw   = 0.0f;  // EMA of beacon signal level (raw FFT units)
static int          _wfOffset    = 0;     // palette brightness offset [-80..+80]
static LGFX_Sprite* _wfMark      = nullptr; // 30×WF_H time-marker strip
static uint32_t     _wfMarkMs    = 0;       // millis of last tick drawn

// ── Color helpers ─────────────────────────────────────────────────────────────
inline uint16_t c8(uint8_t r, uint8_t g, uint8_t b) {
    return tft.color565(r, g, b);
}

// Waterfall palette: noise floor = deep blue, signals = yellow/red
inline uint16_t wfColor(uint16_t rawFFT) {
    int v = (rawFFT >> 8) + _wfOffset;
    if (v < 0) v = 0; else if (v > 255) v = 255;
    uint8_t r = 0, g = 0, b = 0;
    if      (v < 48)  {                           b = v * 5;         }
    else if (v < 112) { int n = v - 48;  g = n*4; b = 240;           }
    else if (v < 176) { int n = v - 112; r = n*4; g = 255; b = 255 - n*4; }
    else              { int n = v - 176; r = 255; g = 255 - n*4;     }
    return tft.color888(r, g, b);
}

// ── Coordinate mapping ────────────────────────────────────────────────────────
inline int dbToY(float db) {
    // 0 dB sits SUB_ZERO_PX above PL_BOT; DB_RANGE maps to PL_TOP
    int y = (PL_BOT - SUB_ZERO_PX) - (int)(db * (float)(PL_H - 1 - SUB_ZERO_PX) / DB_RANGE);
    if (y < PL_TOP) y = PL_TOP;
    if (y > PL_BOT) y = PL_BOT;
    return y;
}
inline float rawToDb(uint16_t v) {
    // wf.js calibration: scale_db=65536/20, 0 dB at v=65536/6≈10923
    return 20.0f * ((float)v / 65536.0f - 1.0f / 6.0f);
}
inline int freqToX(float mhz) {
    return PL_LEFT + (int)((mhz - FREQ_START) * PL_W / FREQ_SPAN);
}
inline float pxToFreq(int px) {   // px = 0..PL_W-1
    return FREQ_START + (float)px * FREQ_SPAN / PL_W;
}

// ── Static UI regions ────────────────────────────────────────────────────────


static void drawFreqAxis() {
    tft.fillRect(0, FA_TOP, 800, FA_H, TFT_BLACK);
    tft.setFont(&fonts::DejaVu12);
    tft.setTextColor(c8(155, 155, 155), TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    // Label every whole MHz inside the span (10491..10498)
    int firstMHz = (int)FREQ_START + 1;                         // 10491
    int lastMHz  = (int)(FREQ_START + FREQ_SPAN);               // 10499 (inclusive)
    for (int fMHz = firstMHz; fMHz <= lastMHz; fMHz++) {
        int x = freqToX((float)fMHz);
        char buf[12];
        snprintf(buf, sizeof(buf), "%.3f", fMHz / 1000.0f);    // "10.491" GHz
        tft.drawString(buf, x, FA_TOP + 3);
        tft.drawLine(x, FA_TOP + FA_H - 5, x, FA_TOP + FA_H - 1, c8(70, 70, 70));
    }
    tft.setTextDatum(TL_DATUM);
}

static void drawDbLabels() {
    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(MR_DATUM);
    for (int db = 0; db <= (int)DB_RANGE; db += 5) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ddB", db);
        tft.setTextColor(c8(110, 110, 110), TFT_BLACK);
        tft.drawString(buf, PL_LEFT - 2, dbToY((float)db));
    }
    tft.setTextDatum(TL_DATUM);
}

static void drawBandplan() {
    const uint16_t BG   = TFT_BLACK;
    const uint16_t BLUE = c8(30, 100, 220);
    tft.fillRect(0, BP_TOP, 800, BP_H, BG);

    int z2x = freqToX(10492.5f);
    int z3x = freqToX(10497.0f);
    int z4x = PL_LEFT + PL_W;

    // Zone dividers
    tft.drawFastVLine(z2x, BP_TOP, BP_H, c8(60, 60, 60));
    tft.drawFastVLine(z3x, BP_TOP, BP_H, c8(60, 60, 60));

    // ── Three channel rows — block width = channel bandwidth ─────────────────
    // Row 1 (top)    : 125 KS — 125 kHz wide, every 0.25 MHz
    // Row 2 (middle) : 333 KS — 333 kHz wide, every 0.50 MHz
    // Row 3 (bottom) : 1 MS   — 1.0 MHz wide, every 1.50 MHz
    const int R1Y = BP_TOP + 2,  R1H = 4;
    const int R2Y = BP_TOP + 8,  R2H = 5;
    const int R3Y = BP_TOP + 15, R3H = 7;

    for (float fc = 10492.75f; fc < 10499.5f; fc += 0.25f) {
        int x1 = freqToX(fc - 0.0625f), x2 = freqToX(fc + 0.0625f);
        if (x2 > x1) tft.fillRect(x1, R1Y, x2 - x1, R1H, BLUE);
    }
    for (float fc = 10492.75f; fc < 10499.5f; fc += 0.5f) {
        int x1 = freqToX(fc - 0.1665f), x2 = freqToX(fc + 0.1665f);
        if (x2 > x1) tft.fillRect(x1, R2Y, x2 - x1, R2H, BLUE);
    }
    for (float fc = 10493.25f; fc < 10497.0f; fc += 1.5f) {
        int x1 = freqToX(fc - 0.5f), x2 = freqToX(fc + 0.5f);
        if (x2 > x1) tft.fillRect(x1, R3Y, x2 - x1, R3H, BLUE);
    }

    // A71A beacon — 1.5 MS, red, in the 1 MS row
    {
        int x1 = freqToX(10490.75f), x2 = freqToX(10492.25f);
        tft.fillRect(x1, R3Y, x2 - x1, R3H, c8(180, 40, 40));
    }

    // Zone labels
    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(c8(220, 100, 100), BG);
    tft.drawString("A71A Beacon", freqToX(10491.5f), BP_TOP + 27);
    tft.setTextColor(c8(80, 140, 255), BG);
    tft.drawString("Wide & Narrow DATV", (z2x + z3x) / 2, BP_TOP + 27);
    tft.setTextColor(c8(80, 200, 120), BG);
    tft.drawString("Narrow DATV", (z3x + z4x) / 2, BP_TOP + 27);

    // Waterfall contrast buttons — lower-left (WF-) and lower-right (WF+)
    const uint16_t WF_DIM = c8(55, 55, 55);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(_wfOffset < 0 ? c8(220, 140, 40) : WF_DIM, BG);
    tft.drawString("WF-", 4, BP_TOP + 27);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(_wfOffset > 0 ? c8(100, 210, 255) : WF_DIM, BG);
    tft.drawString("WF+", 797, BP_TOP + 27);
    tft.setTextDatum(TL_DATUM);
}

// ── Nav button strip (y=25..60): CHAT | WEATHER | WF ─────────────────────────
static void drawNavButtons() {
    constexpr int BW = 800 / 3;   // 266px per button (last button gets the extra 2px)
    const uint16_t BG  = c8(15, 18, 28);
    const uint16_t DIV = c8(45, 50, 70);
    const uint16_t SEP = c8(35, 40, 60);

    tft.fillRect(0, BTN_TOP, 800, BTN_H, BG);

    // Top and bottom separator lines
    tft.drawFastHLine(0, BTN_TOP,              800, SEP);
    tft.drawFastHLine(0, BTN_TOP + BTN_H - 1,  800, SEP);

    // Vertical dividers between buttons
    tft.drawFastVLine(BW,     BTN_TOP + 5, BTN_H - 10, DIV);
    tft.drawFastVLine(BW * 2, BTN_TOP + 5, BTN_H - 10, DIV);

    tft.setFont(&fonts::DejaVu12);
    const int CY = BTN_TOP + BTN_H / 2;

    tft.setTextColor(c8(80, 140, 210), BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("< CHAT", 12, CY);

    tft.setTextColor(_wfVisible ? c8(0, 200, 100) : c8(80, 80, 80), BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("WATER FALL", BW + BW / 2, CY);

    tft.setTextColor(c8(80, 190, 110), BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("WEATHER >", 788, CY);

    tft.setTextDatum(TL_DATUM);
}

// ── Grid (drawn into spectrum sprite) ────────────────────────────────────────
static void drawGrid() {
    _spec->fillSprite(TFT_BLACK);
    uint16_t gridCol = c8(55, 105, 55);  // dark green grid

    // Horizontal dashed lines at 5 dB intervals
    for (int db = 0; db <= (int)DB_RANGE; db += 5) {
        int y = dbToY((float)db) - PL_TOP;
        for (int x = 0; x < PL_W; x += 8)
            _spec->drawFastHLine(x, y, 4, gridCol);
    }

    // Vertical dashed lines at whole-MHz intervals
    int firstMHz = (int)FREQ_START + 1;
    int lastMHz  = (int)(FREQ_START + FREQ_SPAN);
    for (int fMHz = firstMHz; fMHz <= lastMHz; fMHz++) {
        int x = freqToX((float)fMHz) - PL_LEFT;
        for (int y = 0; y < PL_H; y += 8)
            _spec->drawFastVLine(x, y, 4, gridCol);
    }
}

// ── Waterfall brightness thermometer (drawn in left margin x=0..PL_LEFT-1) ────
static void drawWfBrightnessBar() {
    if (!_wfVisible) return;
    const int IX = 3, IW = 8;
    const int IY = WF_TOP + 6, IH = WF_H - 12;
    const int MID = IY + IH / 2;

    tft.fillRect(0, WF_TOP, PL_LEFT - 1, WF_H, TFT_BLACK);
    tft.drawRect(IX, IY, IW, IH, c8(55, 55, 55));
    tft.drawFastHLine(IX, MID, IW, c8(90, 90, 90));  // zero mark

    if (_wfOffset != 0) {
        int barH = abs(_wfOffset) * (IH / 2 - 2) / 80;
        uint16_t col = (_wfOffset > 0) ? c8(0, 180, 80) : c8(210, 100, 0);
        if (_wfOffset > 0)
            tft.fillRect(IX + 1, MID - barH, IW - 2, barH, col);
        else
            tft.fillRect(IX + 1, MID + 1,    IW - 2, barH, col);
    }
}

// ── Symbol-rate snap table (mirrors wf.js align_symbolrate) ──────────────────
static float alignSR(float bw) {
    if      (bw < 0.022f) return 0.0f;
    else if (bw < 0.040f) return 0.025f;
    else if (bw < 0.059f) return 0.033f;
    else if (bw < 0.086f) return 0.066f;
    else if (bw < 0.185f) return 0.125f;
    else if (bw < 0.277f) return 0.250f;
    else if (bw < 0.388f) return 0.333f;
    else if (bw < 0.700f) return 0.500f;
    else if (bw < 1.2f)   return 1.000f;
    else if (bw < 1.6f)   return 1.500f;
    else if (bw < 2.2f)   return 2.000f;
    else                  return roundf(bw * 5.0f) / 5.0f;
}

// ── Sprite allocation ─────────────────────────────────────────────────────────
static void createSprites() {
    // setPsram(true) MUST be called before createSprite — default is false
    // spec = 756×274×2 = ~415 KB, wf = 756×100×2 = ~148 KB → must live in PSRAM
    _spec = new LGFX_Sprite(&tft);
    _spec->setPsram(true);
    _spec->setColorDepth(16);
    bool specOk = (bool)_spec->createSprite(PL_W, PL_H);
    Serial.printf("[spec] sprite %dx%d: %s\n", PL_W, PL_H, specOk ? "OK" : "FAILED");

    _wf = new LGFX_Sprite(&tft);
    _wf->setPsram(true);
    _wf->setColorDepth(16);
    bool wfOk = (bool)_wf->createSprite(PL_W, WF_H);
    Serial.printf("[wf]   sprite %dx%d: %s\n", PL_W, WF_H, wfOk ? "OK" : "FAILED");
    _wf->fillSprite(TFT_BLACK);

    _wfMark = new LGFX_Sprite(&tft);
    _wfMark->setPsram(true);
    _wfMark->setColorDepth(16);
    _wfMark->createSprite(30, WF_H);
    _wfMark->fillSprite(TFT_BLACK);
}

// ── Public API ────────────────────────────────────────────────────────────────

inline void init() {
    tft.fillScreen(TFT_BLACK);
    createSprites();
    drawFreqAxis();
    drawNavButtons();
    drawDbLabels();
    drawBandplan();
    tft.drawFastHLine(0, PL_BOT + 1, 800, c8(30, 30, 30));
    drawGrid();
    _spec->pushSprite(PL_LEFT, PL_TOP);
    _wf->pushSprite(PL_LEFT, WF_TOP);
    _wfMark->pushSprite(14, WF_TOP);
    drawWfBrightnessBar();
}

// Redraw all static UI elements after returning from another page.
// Does NOT reallocate sprites — they retain their last FFT frame.
inline void redraw() {
    tft.fillScreen(TFT_BLACK);
    drawFreqAxis();
    drawNavButtons();
    drawDbLabels();
    drawBandplan();
    tft.drawFastHLine(0, PL_BOT + 1, 800, c8(30, 30, 30));
    drawGrid();
    _spec->pushSprite(PL_LEFT, PL_TOP);
    if (_wfVisible) {
        _wf->pushSprite(PL_LEFT, WF_TOP);
        _wfMark->pushSprite(14, WF_TOP);
    }
    drawWfBrightnessBar();
}

inline void handleFFTData(uint8_t* payload, size_t length) {
    const size_t nVals = length / 2;
    if (nVals == 0 || nVals > 1000) return;

    // ── Decode bins; build per-pixel max for display ──────────────────────
    static uint16_t bins[1000];
    static uint16_t pxMax[PL_W];
    memset(pxMax, 0, sizeof(pxMax));
    for (size_t i = 0; i < nVals; i++) {
        uint16_t v = (uint16_t)payload[i*2] | ((uint16_t)payload[i*2+1] << 8);
        bins[i] = v;
        int px = (int)(i * PL_W / nVals);
        if (px < PL_W && v > pxMax[px]) pxMax[px] = v;
    }

    // ── Beacon reference level (A71A: 10490.75..10492.25 MHz, middle 40% of bins)
    {
        int b0  = (int)(0.25f / FREQ_SPAN * nVals);
        int b1  = (int)(1.75f / FREQ_SPAN * nVals);
        int bm0 = b0 + (int)(0.3f * (b1 - b0));
        int bm1 = b0 + (int)(0.7f * (b1 - b0));
        float sum = 0.0f; int cnt = 0;
        for (int i = bm0; i <= bm1 && i < (int)nVals; i++) { sum += bins[i]; cnt++; }
        if (cnt > 0) {
            float m = sum / cnt;
            _beaconRaw = (_beaconRaw < 1.0f) ? m : 0.95f * _beaconRaw + 0.05f * m;
        }
    }

    // ── Spectrum sprite ───────────────────────────────────────────────────
    drawGrid();
    const uint16_t COL_FILL = c8(SCHEMES[_colorScheme][0], SCHEMES[_colorScheme][1], SCHEMES[_colorScheme][2]);
    const uint16_t COL_PEAK = c8(SCHEMES[_colorScheme][3], SCHEMES[_colorScheme][4], SCHEMES[_colorScheme][5]);
    for (int px = 0; px < PL_W; px++) {
        if (pxMax[px] < 8192) continue;
        float db  = rawToDb(pxMax[px]);
        int sprY  = dbToY(db) - PL_TOP;
        int fillH = (PL_H - 1) - sprY;
        if (fillH > 0) _spec->drawFastVLine(px, sprY + 1, fillH, COL_FILL);
        if (sprY >= 0 && sprY < PL_H) _spec->drawPixel(px, sprY, COL_PEAK);
    }

    // ── Signal detection on raw bins — wf.js algorithm ───────────────────
    static constexpr float NOISE_LVL = 11000.0f;
    bool inSig      = false;
    int  sigStartBin = 0;

    for (int i = 2; i <= (int)nVals; i++) {
        float avg = (i < (int)nVals)
                  ? (bins[i-2] + bins[i-1] + bins[i]) / 3.0f
                  : 0.0f;  // flush at end

        if (!inSig && avg > SIG_THRESHOLD) {
            inSig        = true;
            sigStartBin  = i;
        } else if (inSig && avg < SIG_THRESHOLD) {
            inSig = false;
            int sigEndBin = i;

            // Mean of middle 40 % of bins
            int j0 = sigStartBin + (int)(0.3f * (sigEndBin - sigStartBin));
            int j1 = sigStartBin + (int)(0.7f * (sigEndBin - sigStartBin));
            float strength = 0.0f; int cnt = 0;
            for (int j = j0; j <= j1 && j < (int)nVals; j++) { strength += bins[j]; cnt++; }
            if (cnt == 0) continue;
            strength /= cnt;

            // Refine edges to 75 % above noise
            float thr = NOISE_LVL + 0.75f * (strength - NOISE_LVL);
            while (sigStartBin < sigEndBin && bins[sigStartBin] < thr) sigStartBin++;
            while (sigEndBin > sigStartBin && bins[sigEndBin]   < thr) sigEndBin--;

            // Center frequency — wf.js: 490.5 + ((mid+1)/nVals)*9.0
            float midBin  = sigStartBin + (sigEndBin - sigStartBin) / 2.0f;
            float freqMHz = FREQ_START + ((midBin + 1.0f) / (float)nVals) * FREQ_SPAN;

            // Symbol rate
            float bwMHz = (sigEndBin - sigStartBin) * FREQ_SPAN / (float)nVals;
            float sr    = alignSR(bwMHz);
            if (sr == 0.0f) continue;

            // Snap frequency: 1/40 MHz for ≥0.7 MS, 1/80 MHz for narrower
            float snap    = (sr >= 0.7f) ? 40.0f : 80.0f;
            float offset  = freqMHz - 10000.0f;
            float snapped = roundf(offset * snap) / snap;

            // Is this the A71A beacon?
            bool isBeacon = (snapped > 491.3f && snapped < 491.7f && sr >= 1.4f);

            // Line 1: frequency
            char freqStr[16];
            snprintf(freqStr, sizeof(freqStr), "'%.3f", snapped);

            // Line 2: symbol rate
            char srStr[12];
            if (sr < 0.7f)
                snprintf(srStr, sizeof(srStr), "%dKS", (int)roundf(sr*1000.0f));
            else
                snprintf(srStr, sizeof(srStr), "%.1fMS", roundf(sr*10.0f)/10.0f);

            // Line 3: dB relative to beacon
            char relStr[16] = "";
            if (isBeacon) {
                snprintf(relStr, sizeof(relStr), "BEACON");
            } else if (_beaconRaw > 1000.0f) {
                float relDb = 20.0f * ((strength - _beaconRaw) / 65536.0f);
                snprintf(relStr, sizeof(relStr), "%+.1fdB BCN", relDb);
            }

            // Place label above peak — room for 3 lines
            float sigDb  = rawToDb((uint16_t)roundf(strength));
            int   peakY  = dbToY(sigDb) - PL_TOP;
            int   labelY = peakY - 48;
            if (labelY < 2) labelY = 2;

            int sigCtrPx = (int)(midBin * PL_W / (float)nVals);

            _spec->setFont(&fonts::DejaVu12);
            _spec->setTextDatum(TC_DATUM);
            _spec->setTextColor(TFT_WHITE, TFT_BLACK);
            _spec->drawString(freqStr, sigCtrPx, labelY);
            _spec->drawString(srStr,   sigCtrPx, labelY + 15);
            if (relStr[0])
                _spec->drawString(relStr, sigCtrPx, labelY + 30);
            _spec->setTextDatum(TL_DATUM);
        }
    }

    _spec->pushSprite(PL_LEFT, PL_TOP);

    // ── Waterfall ─────────────────────────────────────────────────────────
    if (_wfVisible) {
        _wf->scroll(0, 1);
        for (int px = 0; px < PL_W; px++)
            _wf->drawPixel(px, 0, wfColor(pxMax[px]));
        _wf->pushSprite(PL_LEFT, WF_TOP);

        // Time-marker strip: scroll + inject a tick every 5 s
        _wfMark->scroll(0, 1);
        _wfMark->drawFastHLine(0, 0, 30, TFT_BLACK);   // clear new top row
        uint32_t now = millis();
        if (now - _wfMarkMs >= 5000) {
            _wfMarkMs = now;
            _wfMark->drawFastHLine(2, 0, 26, c8(200, 200, 200));
        }
        _wfMark->pushSprite(14, WF_TOP);
    }
}

// ── Spectrum color cycle (tap beacon area) ───────────────────────────────────
inline void cycleSpectrumColor() {
    _colorScheme = (_colorScheme + 1) % N_SCHEMES;
}

// ── Waterfall visibility toggle (tap lower-right corner) ─────────────────────
inline void toggleWaterfall() {
    _wfVisible = !_wfVisible;

    // Clear spectrum + separator + waterfall rows
    tft.fillRect(0, PL_TOP, 800, BP_TOP - PL_TOP, TFT_BLACK);

    PL_BOT = _wfVisible ? 334 : 434;
    PL_H   = _wfVisible ? 274 : 374;

    // Rebuild spectrum sprite at new height (PSRAM: max 410×756×2 ≈ 619 KB)
    _spec->deleteSprite();
    delete _spec;
    _spec = new LGFX_Sprite(&tft);
    _spec->setPsram(true);
    _spec->setColorDepth(16);
    _spec->createSprite(PL_W, PL_H);

    drawNavButtons();
    drawDbLabels();
    tft.drawFastHLine(0, PL_BOT + 1, 800, c8(30, 30, 30));
    if (_wfVisible) { _wf->pushSprite(PL_LEFT, WF_TOP); _wfMark->pushSprite(14, WF_TOP); drawWfBrightnessBar(); }
    drawBandplan();
    drawGrid();
    _spec->pushSprite(PL_LEFT, PL_TOP);
}

// ── Waterfall brightness (tap left = darker/more contrast, right = brighter) ──
inline void adjustWfBrightness(int delta) {
    _wfOffset += delta;
    if (_wfOffset < -80) _wfOffset = -80;
    if (_wfOffset >  80) _wfOffset =  80;
    drawWfBrightnessBar();
    drawBandplan();   // refresh WF-/WF+ corner symbol colours
}

} // namespace SpectrumDisplay

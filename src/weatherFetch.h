/**
 * weatherFetch.h — Open-Meteo background fetch
 *
 * Ported from ESP32_S3_PMX-500/include/weatherFetch.h.
 * Adapted: lat/lon read from NVSConfig::loadLocation() ("location" namespace)
 *          instead of PMX-500's "config"/"st_lat"/"st_lon".
 *
 * Public API:
 *   weather_fetch_start()   — spawn FreeRTOS task (safe to call multiple times)
 *   _wthr_fetch_data        — result struct (check _wthr_fetch_ready first)
 *   _wthr_fetch_ready       — set true when new data is available
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "nvs_config.h"
#include "weatherData.h"
#include "weatherIcons.h"

static volatile bool  _wthr_fetch_ready  = false;
static WeatherData    _wthr_fetch_data   = {};
static TaskHandle_t   _wthr_fetch_handle = nullptr;
static uint32_t       _wthr_last_fetch_ms = 0;

// ── Minimal JSON helpers ──────────────────────────────────────────────────────

static bool _wf_float(const char *j, const char *key, float *out) {
    char s[72]; snprintf(s, sizeof(s), "\"%s\":", key);
    const char *p = strstr(j, s);
    if (!p) return false;
    p += strlen(s);
    while (*p == ' ') p++;
    if (*p == '"' || *p == '[' || *p == '{') return false;
    *out = (float)atof(p);
    return true;
}

static bool _wf_int(const char *j, const char *key, int *out) {
    float f;
    if (!_wf_float(j, key, &f)) return false;
    *out = (int)roundf(f);
    return true;
}

static const char *_wf_section(const char *j, const char *key) {
    char s[72]; snprintf(s, sizeof(s), "\"%s\":{", key);
    return strstr(j, s);
}

static bool _wf_arr_f(const char *j, const char *key, int idx, float *out) {
    char s[72]; snprintf(s, sizeof(s), "\"%s\":[", key);
    const char *p = strstr(j, s);
    if (!p) return false;
    p += strlen(s);
    for (int i = 0; i < idx; i++) {
        while (*p && *p != ',' && *p != ']') p++;
        if (*p != ',') return false;
        p++;
    }
    while (*p == ' ') p++;
    if (*p == ']' || !*p) return false;
    *out = (float)atof(p);
    return true;
}

static bool _wf_arr_i(const char *j, const char *key, int idx, int *out) {
    float f; if (!_wf_arr_f(j, key, idx, &f)) return false;
    *out = (int)roundf(f); return true;
}

static bool _wf_arr_s(const char *j, const char *key, int idx,
                       char *out, size_t sz) {
    char s[72]; snprintf(s, sizeof(s), "\"%s\":[", key);
    const char *p = strstr(j, s);
    if (!p) return false;
    p += strlen(s);
    for (int i = 0; i < idx; i++) {
        if (*p == '"') { p++; p = strchr(p, '"'); if (!p) return false; p++; }
        while (*p && *p != ',' && *p != ']') p++;
        if (*p != ',') return false;
        p++;
    }
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    const char *e = strchr(p, '"');
    if (!e) return false;
    size_t len = (size_t)(e - p);
    if (len >= sz) len = sz - 1;
    memcpy(out, p, len); out[len] = '\0';
    return len > 0;
}

static void _wf_hour_label(const char *t, bool is_now, char *out, size_t sz) {
    if (is_now) { strlcpy(out, "Now", sz); return; }
    if (strlen(t) >= 13) snprintf(out, sz, "%c%ch", t[11], t[12]);
    else strlcpy(out, "?h", sz);
}

static void _wf_day_name(const char *d, char *out, size_t sz) {
    int y, m, day;
    if (sscanf(d, "%d-%d-%d", &y, &m, &day) == 3) {
        struct tm t = {}; t.tm_year = y-1900; t.tm_mon = m-1; t.tm_mday = day; t.tm_isdst=-1;
        mktime(&t);
        static const char *n[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        strlcpy(out, n[t.tm_wday], sz);
    } else strlcpy(out, "???", sz);
}

static void _wf_hhmm(const char *dt, char *out, size_t sz) {
    if (strlen(dt) >= 16) { strncpy(out, dt+11, sz > 5 ? 5 : sz-1); out[5]='\0'; }
    else strlcpy(out, "--:--", sz);
}

// ── Fetch task ────────────────────────────────────────────────────────────────
static void _wthr_fetch_body(float lat, float lon) {
    WeatherData d = {};

    char url[512];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f&timezone=auto"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "weather_code,surface_pressure,is_day"
        "&hourly=temperature_2m,precipitation_probability,weather_code,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
               "precipitation_probability_max,sunrise,sunset"
        "&forecast_days=6",
        lat, lon);

    Serial.printf("[WTHR] GET %s\n", url);
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.begin(client, url); http.setTimeout(10000);
    int code = http.GET();
    Serial.printf("[WTHR] HTTP %d\n", code);
    if (code != 200) { http.end(); return; }

    String body = http.getString();
    Serial.printf("[WTHR] body %u bytes\n", (unsigned)body.length());
    http.end();
    const char *j = body.c_str();

    // Current
    const char *cur = _wf_section(j, "current");
    if (cur) {
        float f; int iv;
        if (_wf_float(cur, "temperature_2m",      &f))  d.temp_c       = f;
        if (_wf_float(cur, "apparent_temperature", &f))  d.feels_like_c = f;
        if (_wf_int  (cur, "relative_humidity_2m", &iv)) d.humidity_pct = iv;
        if (_wf_float(cur, "surface_pressure",     &f))  d.pressure_hpa = (int)roundf(f);
        if (_wf_int  (cur, "weather_code",         &iv)) d.wmo_code     = iv;
        if (_wf_int  (cur, "is_day",               &iv)) d.is_day       = (iv == 1);
    }

    int base_hr = 0;
    if (cur) {
        const char *tp = strstr(cur, "\"time\":\"");
        if (tp) {
            tp += 8;
            if (tp[11] >= '0' && tp[11] <= '2' && tp[12] >= '0' && tp[12] <= '9')
                base_hr = (tp[11]-'0')*10 + (tp[12]-'0');
        }
    }
    struct tm tm_now; getLocalTime(&tm_now);

    const char *hr = _wf_section(j, "hourly");
    if (hr) {
        for (int i = 0; i < 7; i++) {
            int idx = base_hr + i;
            char t[24] = {};
            _wf_arr_s(hr, "time", idx, t, sizeof(t));
            _wf_hour_label(t, i == 0, d.hr_time[i], sizeof(d.hr_time[i]));
            float f; int iv;
            if (_wf_arr_f(hr, "temperature_2m",           idx, &f))  d.hr_temp[i]     = f;
            if (_wf_arr_i(hr, "precipitation_probability", idx, &iv)) d.hr_rain_pct[i] = iv;
            if (_wf_arr_i(hr, "weather_code",             idx, &iv)) d.hr_wmo[i]     = iv;
            if (_wf_arr_i(hr, "is_day",                   idx, &iv)) d.hr_is_day[i]  = (iv==1);
        }
    }

    const char *dy = _wf_section(j, "daily");
    if (dy) {
        for (int i = 0; i < 5; i++) {
            int idx = i + 1;
            char t[16] = {};
            _wf_arr_s(dy, "time", idx, t, sizeof(t));
            _wf_day_name(t, d.dy_name[i], sizeof(d.dy_name[i]));
            float f; int iv;
            if (_wf_arr_f(dy, "temperature_2m_min",           idx, &f))  d.dy_lo[i]       = f;
            if (_wf_arr_f(dy, "temperature_2m_max",           idx, &f))  d.dy_hi[i]       = f;
            if (_wf_arr_i(dy, "precipitation_probability_max", idx, &iv)) d.dy_rain_pct[i] = iv;
            if (_wf_arr_i(dy, "weather_code",                  idx, &iv)) d.dy_wmo[i]      = iv;
            if (i == 0) {
                char sr[24]={}, ss[24]={};
                _wf_arr_s(dy, "sunrise", 0, sr, sizeof(sr)); _wf_hhmm(sr, d.sunrise, sizeof(d.sunrise));
                _wf_arr_s(dy, "sunset",  0, ss, sizeof(ss)); _wf_hhmm(ss, d.sunset,  sizeof(d.sunset));
            }
        }
    }

    d.moon_phase_idx = moon_phase(tm_now.tm_year+1900, tm_now.tm_mon+1, tm_now.tm_mday);
    d.valid          = true;
    _wthr_fetch_data    = d;
    _wthr_last_fetch_ms = millis();
    Serial.printf("[WTHR] done — sunrise=%s sunset=%s moon=%d (%s)\n",
                  d.sunrise, d.sunset, d.moon_phase_idx, moon_phase_name(d.moon_phase_idx));
}

static void _wthr_fetch_task(void *) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WTHR] no WiFi — aborting");
        _wthr_fetch_ready  = true;
        _wthr_fetch_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    NVSConfig::LocationData loc = NVSConfig::loadLocation();
    if (!loc.valid || (loc.lat == 0.0f && loc.lon == 0.0f)) {
        Serial.println("[WTHR] no coordinates in NVS — skipping fetch");
        _wthr_fetch_ready  = true;
        _wthr_fetch_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    Serial.printf("[WTHR] coordinates: lat=%.4f lon=%.4f\n", loc.lat, loc.lon);
    _wthr_fetch_body(loc.lat, loc.lon);
    _wthr_fetch_ready  = true;
    _wthr_fetch_handle = nullptr;
    vTaskDelete(nullptr);
}

static void weather_fetch_start() {
    if (_wthr_fetch_handle) { Serial.println("[WTHR] fetch already running"); return; }
    Serial.println("[WTHR] fetch start");
    _wthr_fetch_ready   = false;
    _wthr_last_fetch_ms = millis();
    xTaskCreate(_wthr_fetch_task, "wthr_fetch", 12288, nullptr, 1, &_wthr_fetch_handle);
}

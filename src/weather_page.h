/**
 * weather_page.h — Full-screen weather page (800×480)
 *
 * Ported from ESP32_S3_PMX-500/include/weatherPage.h.
 * Adaptations:
 *   - Removed need_static_redraw (not used in this project)
 *   - City label reads timezone from NVSConfig::loadLocation() ("location" namespace)
 *     and strips the region prefix (e.g. "Europe/Zurich" → "Zurich")
 *   - Includes HB9IIUdisplayInit.h directly for LGFX_Sprite / tft
 */
#pragma once
#include <lvgl.h>
#include <LittleFS.h>
#include "HB9IIUdisplayInit.h"
#include "nvs_config.h"
#include "weatherIcons.h"
#include "weatherData.h"

// Forward declaration — defined in weatherFetch.h (included before this file)
static void weather_fetch_start();

// Forward declaration — defined later in this file
static void weather_page_update_data(const WeatherData &d);

LV_FONT_DECLARE(HB9IIU_JetBrains_Mono_Bold96);
LV_FONT_DECLARE(HB9IIU_JetBrains_Mono_Bold30);
LV_FONT_DECLARE(HB9IIU_JetBrains_Mono_Bold20);
LV_FONT_DECLARE(HB9IIU_JetBrains_Mono_Bold14);
LV_FONT_DECLARE(HB9IIU_JetBrains_Mono_Medium20);

// ─── Layout ──────────────────────────────────────────────────────────────────
static constexpr int      _WTHR_SCREEN_W = 800;
static constexpr int      _WTHR_SCREEN_H = 480;
static constexpr int      _WTHR_HDR_H   = 60;
static constexpr int      _WTHR_IMG_W   = 260;
static constexpr int      _WTHR_IMG_H   = _WTHR_SCREEN_H - _WTHR_HDR_H;
static constexpr uint32_t _WTHR_SLIDE_MS = 2500;

// ─── Slide data ───────────────────────────────────────────────────────────────
static const char *_wthr_slide_paths[] = {
    // ── Day (indices 0-10) ────────────────────────────────────────────────────
    "/weatherBackgrounds/01_sunny.jpg",            //  0
    "/weatherBackgrounds/02_partly_cloudy.jpg",    //  1
    "/weatherBackgrounds/03_mostly_cloudy.jpg",    //  2
    "/weatherBackgrounds/04_overcast.jpg",         //  3
    "/weatherBackgrounds/05_light_rain.jpg",       //  4
    "/weatherBackgrounds/06_heavy_rain.jpg",       //  5
    "/weatherBackgrounds/07_thunderstorm.jpg",     //  6
    "/weatherBackgrounds/08_snow.jpg",             //  7
    "/weatherBackgrounds/09_fog.jpg",              //  8
    "/weatherBackgrounds/10_hazy.jpg",             //  9
    "/weatherBackgrounds/11_windy.jpg",            // 10
    // ── Night (indices 11-21) ─────────────────────────────────────────────────
    "/weatherBackgrounds/12_clear_night.jpg",          // 11
    "/weatherBackgrounds/02_partly_cloudy_night.jpg",  // 12
    "/weatherBackgrounds/03_mostly_cloudy_night.jpg",  // 13
    "/weatherBackgrounds/04_overcast_night.jpg",       // 14
    "/weatherBackgrounds/05_light_rain_night.jpg",     // 15
    "/weatherBackgrounds/06_heavy_rain_night.jpg",     // 16
    "/weatherBackgrounds/07_thunderstorm_night.jpg",   // 17
    "/weatherBackgrounds/08_snow_night.jpg",           // 18
    "/weatherBackgrounds/09_fog_night.jpg",            // 19
    "/weatherBackgrounds/10_hazy_night.jpg",           // 20
    "/weatherBackgrounds/11_windy_night.jpg",          // 21
};
static const char *_wthr_condition_labels[] = {
    "Sunny", "Partly Cloudy", "Mostly Cloudy", "Overcast",
    "Light Rain", "Heavy Rain", "Thunderstorm", "Snow",
    "Fog", "Hazy", "Windy", "Clear Night",
};
static const char *_wthr_temp_labels[] = {
    "24", "22", "18", "16", "15", "12",
    "17", "-2", "10", "20", "19", "14",
};
static const char *_wthr_feels_like_labels[] = {
    "Feels like 24.3°C", "Feels like 23.1°C", "Feels like 18.7°C",
    "Feels like 16.4°C", "Feels like 15.0°C", "Feels like 11.8°C",
    "Feels like 16.2°C", "Feels like -6.5°C", "Feels like 10.9°C",
    "Feels like 20.0°C", "Feels like 18.3°C", "Feels like 13.6°C",
};
static const char *_wthr_humidity_labels[] = {
    "45%", "68%", "78%", "85%", "90%", "95%",
    "88%", "75%", "92%", "55%", "60%", "40%",
};
static const char *_wthr_pressure_labels[] = {
    "1018 hPa", "1012 hPa", "1008 hPa", "1005 hPa", "1002 hPa", "998 hPa",
    "995 hPa",  "1020 hPa", "1010 hPa", "1015 hPa", "1008 hPa", "1022 hPa",
};

static constexpr uint8_t _WTHR_SLIDE_COUNT =
    sizeof(_wthr_slide_paths) / sizeof(_wthr_slide_paths[0]);

// ─── State ────────────────────────────────────────────────────────────────────
static lv_obj_t    *_wthr_panel        = nullptr;
static lv_obj_t    *_wthr_img          = nullptr;
static lv_obj_t    *_wthr_temp_lbl     = nullptr;
static lv_obj_t    *_wthr_unit_lbl     = nullptr;
static lv_obj_t    *_wthr_cond_lbl     = nullptr;
static lv_obj_t    *_wthr_feels_lbl    = nullptr;
static lv_obj_t    *_wthr_city_lbl     = nullptr;
static lv_obj_t    *_wthr_humidity_lbl = nullptr;
static lv_obj_t    *_wthr_pressure_lbl = nullptr;
// Right pane — hourly (7 slots)
static lv_obj_t    *_wthr_hr_icon[7]     = {};
static lv_obj_t    *_wthr_hr_time_lbl[7] = {};
static lv_obj_t    *_wthr_hr_temp_lbl[7] = {};
static lv_obj_t    *_wthr_hr_rain_lbl[7] = {};
// Right pane — daily (5 rows)
static lv_obj_t    *_wthr_dy_icon[5]     = {};
static lv_obj_t    *_wthr_dy_name_lbl[5] = {};
static lv_obj_t    *_wthr_dy_lo_lbl[5]   = {};
static lv_obj_t    *_wthr_dy_hi_lbl[5]   = {};
static lv_obj_t    *_wthr_dy_rain_lbl[5] = {};
static uint8_t     *_wthr_hr_ibuf[7] = {};
static lv_img_dsc_t _wthr_hr_idsc[7] = {};
static uint8_t     *_wthr_dy_ibuf[5] = {};
static lv_img_dsc_t _wthr_dy_idsc[5] = {};
static uint8_t     *_wthr_sr_buf     = nullptr;
static lv_img_dsc_t _wthr_sr_dsc     = {};
static uint8_t     *_wthr_ss_buf     = nullptr;
static lv_img_dsc_t _wthr_ss_dsc     = {};
static lv_obj_t    *_wthr_sr_img      = nullptr;
static lv_obj_t    *_wthr_ss_img      = nullptr;
static lv_obj_t    *_wthr_sunrise_lbl = nullptr;
static lv_obj_t    *_wthr_sunset_lbl  = nullptr;
static lv_obj_t    *_wthr_moon_img    = nullptr;
static lv_obj_t    *_wthr_moon_lbl    = nullptr;
static uint8_t     *_wthr_moon_buf    = nullptr;
static lv_img_dsc_t _wthr_moon_dsc    = {};
static lv_timer_t  *_wthr_timer      = nullptr;
static LGFX_Sprite *_wthr_sprite    = nullptr;
static lv_img_dsc_t _wthr_img_dsc   = {};
static uint8_t      _wthr_slide_idx = 0;
static bool         weather_page_active = false;
static void       (*_wthr_close_cb)()  = nullptr;

// ─── Forward declaration ──────────────────────────────────────────────────────
static void weather_page_close();

// ─── Decode slide into sprite and refresh LVGL image widget ──────────────────
static void _wthr_load_slide()
{
    if (!_wthr_sprite || !_wthr_img) return;

    _wthr_sprite->fillSprite(TFT_BLACK);
    const char *path = _wthr_slide_paths[_wthr_slide_idx];
    if (LittleFS.exists(path))
        _wthr_sprite->drawJpgFile(LittleFS, path, 0, 0, _WTHR_IMG_W, _WTHR_IMG_H);

    // Byte-swap each pixel: LovyanGFX sprite stores in opposite byte order
    // to what LVGL's image renderer expects (confirmed: 0xF800 showed as blue)
    uint16_t *buf = (uint16_t *)_wthr_sprite->getBuffer();
    const int count = _WTHR_IMG_W * _WTHR_IMG_H;
    for (int i = 0; i < count; i++)
        buf[i] = __builtin_bswap16(buf[i]);

    lv_img_set_src(_wthr_img, &_wthr_img_dsc);
    lv_obj_invalidate(_wthr_img);

    if (_wthr_temp_lbl)     lv_label_set_text(_wthr_temp_lbl,     _wthr_temp_labels[_wthr_slide_idx]);
    if (_wthr_cond_lbl)     lv_label_set_text(_wthr_cond_lbl,     _wthr_condition_labels[_wthr_slide_idx]);
    if (_wthr_feels_lbl)    lv_label_set_text(_wthr_feels_lbl,    _wthr_feels_like_labels[_wthr_slide_idx]);
    if (_wthr_humidity_lbl) lv_label_set_text(_wthr_humidity_lbl, _wthr_humidity_labels[_wthr_slide_idx]);
    if (_wthr_pressure_lbl) lv_label_set_text(_wthr_pressure_lbl, _wthr_pressure_labels[_wthr_slide_idx]);
}

// ─── Timer callback ───────────────────────────────────────────────────────────
static void _wthr_timer_cb(lv_timer_t *)
{
    _wthr_slide_idx = (_wthr_slide_idx + 1) % _WTHR_SLIDE_COUNT;
    _wthr_load_slide();
}

// ─── Close ────────────────────────────────────────────────────────────────────
static void _wthr_on_close(lv_event_t *) { weather_page_close(); }

static void _wthr_ref_flash_cb(lv_timer_t *t) {
    lv_obj_t *btn = (lv_obj_t *)t->user_data;
    lv_obj_set_style_border_width(btn, 0, 0);
}

static void _wthr_on_refresh(lv_event_t *e) {
    weather_fetch_start();
    if (_wthr_temp_lbl)     lv_label_set_text(_wthr_temp_lbl,     "--");
    if (_wthr_cond_lbl)     lv_label_set_text(_wthr_cond_lbl,     "");
    if (_wthr_feels_lbl)    lv_label_set_text(_wthr_feels_lbl,    "");
    if (_wthr_humidity_lbl) lv_label_set_text(_wthr_humidity_lbl, "--");
    if (_wthr_pressure_lbl) lv_label_set_text(_wthr_pressure_lbl, "--");

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00CC66), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_timer_t *t = lv_timer_create(_wthr_ref_flash_cb, 400, btn);
    lv_timer_set_repeat_count(t, 1);
}

// ─── Header ───────────────────────────────────────────────────────────────────
static void _wthr_build_header(lv_obj_t *root)
{
    lv_obj_t *h = lv_obj_create(root);
    lv_obj_remove_style_all(h);
    lv_obj_set_pos(h, 0, 0);
    lv_obj_set_size(h, _WTHR_SCREEN_W, _WTHR_HDR_H);
    lv_obj_set_style_bg_color(h, lv_color_hex(0x2A2C38), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(h, 0, 0);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(h);
    lv_obj_remove_style_all(back);
    lv_obj_set_pos(back, 8, 8);
    lv_obj_set_size(back, 120, 44);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x3A3C4A), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x50526A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_add_event_cb(back, _wthr_on_close, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "< Back");
    lv_obj_set_style_text_font(back_lbl, &HB9IIU_JetBrains_Mono_Bold20, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(h);
    lv_label_set_text(title, "Weather Station");
    lv_obj_set_style_text_font(title, &HB9IIU_JetBrains_Mono_Bold20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE4F7EA), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *ref = lv_btn_create(h);
    lv_obj_remove_style_all(ref);
    lv_obj_set_pos(ref, _WTHR_SCREEN_W - 8 - 120, 8);
    lv_obj_set_size(ref, 120, 44);
    lv_obj_set_style_bg_color(ref, lv_color_hex(0x3A3C4A), 0);
    lv_obj_set_style_bg_opa(ref, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ref, lv_color_hex(0x50526A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ref, 10, 0);
    lv_obj_add_event_cb(ref, _wthr_on_refresh, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ref_lbl = lv_label_create(ref);
    lv_label_set_text(ref_lbl, "Refresh");
    lv_obj_set_style_text_font(ref_lbl, &HB9IIU_JetBrains_Mono_Bold20, 0);
    lv_obj_set_style_text_color(ref_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ref_lbl);

    lv_obj_add_event_cb(h, _wthr_on_close, LV_EVENT_CLICKED, nullptr);
}

// ─── Icon loader ──────────────────────────────────────────────────────────────
static void _wthr_load_icon(const char *path, uint8_t **buf,
                             lv_img_dsc_t *dsc, lv_obj_t *widget, uint16_t px = 44)
{
    if (*buf) { free(*buf); *buf = nullptr; }
    File f = LittleFS.open(path);
    if (!f) return;
    size_t sz = f.size();
    *buf = (uint8_t *)ps_malloc(sz);
    if (*buf) {
        f.read(*buf, sz);
        dsc->header.cf = LV_IMG_CF_RAW_ALPHA;
        dsc->header.w  = px;
        dsc->header.h  = px;
        dsc->data_size = sz;
        dsc->data      = *buf;
        if (widget) lv_img_set_src(widget, dsc);
    }
    f.close();
}

// ─── Right pane: hourly strip + 5-day forecast ───────────────────────────────
static void _wthr_build_right_pane(lv_obj_t *root)
{
    const int RX       = _WTHR_IMG_W;
    const int RW       = _WTHR_SCREEN_W - RX;
    const int HOURLY_H = 150;
    const int DAILY_Y  = _WTHR_HDR_H + HOURLY_H;
    const int N_HR     = 7;
    const int HCOL     = RW / N_HR;
    const int N_DY     = 5;
    const int DAILY_H  = _WTHR_SCREEN_H - DAILY_Y;
    const int ROW_H    = DAILY_H / N_DY;

    lv_obj_t *hbg = lv_obj_create(root);
    lv_obj_remove_style_all(hbg);
    lv_obj_set_pos(hbg, RX, _WTHR_HDR_H);
    lv_obj_set_size(hbg, RW, HOURLY_H);
    lv_obj_set_style_bg_color(hbg, lv_color_hex(0x141726), 0);
    lv_obj_set_style_bg_opa(hbg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(hbg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *dbg = lv_obj_create(root);
    lv_obj_remove_style_all(dbg);
    lv_obj_set_pos(dbg, RX, DAILY_Y);
    lv_obj_set_size(dbg, RW, _WTHR_SCREEN_H - DAILY_Y);
    lv_obj_set_style_bg_color(dbg, lv_color_hex(0x0F1120), 0);
    lv_obj_set_style_bg_opa(dbg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dbg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *sep = lv_obj_create(root);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, RX, DAILY_Y);
    lv_obj_set_size(sep, RW, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x2A2E4A), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    const char *h_times[] = { "Now", "+1h", "+2h", "+3h", "+4h", "+5h", "+6h" };
    const char *h_temps[] = { "--",  "--",  "--",  "--",  "--",  "--",  "--"  };
    const char *h_rains[] = { "",    "",    "",    "",    "",    "",    ""    };

    const int HR_TIME_Y = _WTHR_HDR_H + 22;
    const int HR_TEMP_Y = _WTHR_HDR_H + 88;
    const int HR_RAIN_Y = _WTHR_HDR_H + 120;

    for (int i = 0; i < N_HR; i++) {
        const int col_x = RX + i * HCOL;

        if (i > 0) {
            lv_obj_t *cs = lv_obj_create(root);
            lv_obj_remove_style_all(cs);
            lv_obj_set_pos(cs, col_x, _WTHR_HDR_H + 15);
            lv_obj_set_size(cs, 1, HOURLY_H - 20);
            lv_obj_set_style_bg_color(cs, lv_color_hex(0x2A2E4A), 0);
            lv_obj_set_style_bg_opa(cs, LV_OPA_COVER, 0);
        }

        auto _lbl = [&](lv_obj_t **out, int y, const lv_font_t *fnt,
                        uint32_t col, const char *txt) {
            *out = lv_label_create(root);
            lv_obj_set_style_text_font(*out, fnt, 0);
            lv_obj_set_style_text_color(*out, lv_color_hex(col), 0);
            lv_obj_set_style_text_align(*out, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(*out, HCOL);
            lv_obj_set_pos(*out, col_x, y);
            lv_label_set_text(*out, txt);
        };

        _lbl(&_wthr_hr_time_lbl[i], HR_TIME_Y, &HB9IIU_JetBrains_Mono_Bold14, 0x6878A0, h_times[i]);

        _wthr_hr_icon[i] = lv_img_create(root);
        lv_obj_set_pos(_wthr_hr_icon[i], col_x + (HCOL - 44) / 2, _WTHR_HDR_H + 41);
        lv_obj_clear_flag(_wthr_hr_icon[i], LV_OBJ_FLAG_CLICKABLE);

        _lbl(&_wthr_hr_temp_lbl[i], HR_TEMP_Y, &HB9IIU_JetBrains_Mono_Bold20, 0xFFFFFF, h_temps[i]);
        _lbl(&_wthr_hr_rain_lbl[i], HR_RAIN_Y, &HB9IIU_JetBrains_Mono_Bold14, 0x5090E0, h_rains[i]);
    }

    const char *d_names[] = { "---", "---", "---", "---", "---" };
    const char *d_los[]   = { "--",  "--",  "--",  "--",  "--"  };
    const char *d_his[]   = { "--",  "--",  "--",  "--",  "--"  };
    const char *d_rains[] = { "",    "",    "",    "",    ""    };

    const int DY_END = RX + 5 * HCOL;

    for (int i = 0; i < N_DY; i++) {
        const int y0   = DAILY_Y + i * ROW_H;
        const int ymid = y0 + ROW_H / 2;

        if (i > 0) {
            lv_obj_t *rs = lv_obj_create(root);
            lv_obj_remove_style_all(rs);
            lv_obj_set_pos(rs, RX + 4, y0);
            lv_obj_set_size(rs, 5 * HCOL - 8, 1);
            lv_obj_set_style_bg_color(rs, lv_color_hex(0x2A2E4A), 0);
            lv_obj_set_style_bg_opa(rs, LV_OPA_COVER, 0);
        }

        _wthr_dy_name_lbl[i] = lv_label_create(root);
        lv_obj_set_style_text_font(_wthr_dy_name_lbl[i], &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(_wthr_dy_name_lbl[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(_wthr_dy_name_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(_wthr_dy_name_lbl[i], HCOL);
        lv_obj_set_pos(_wthr_dy_name_lbl[i], RX + 0 * HCOL, ymid - 12);
        lv_label_set_text(_wthr_dy_name_lbl[i], d_names[i]);

        _wthr_dy_icon[i] = lv_img_create(root);
        lv_obj_set_pos(_wthr_dy_icon[i], RX + 1 * HCOL + (HCOL - 44) / 2, ymid - 22);
        lv_obj_clear_flag(_wthr_dy_icon[i], LV_OBJ_FLAG_CLICKABLE);

        _wthr_dy_rain_lbl[i] = lv_label_create(root);
        lv_obj_set_style_text_font(_wthr_dy_rain_lbl[i], &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(_wthr_dy_rain_lbl[i], lv_color_hex(0x5090E0), 0);
        lv_obj_set_style_text_align(_wthr_dy_rain_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(_wthr_dy_rain_lbl[i], HCOL);
        lv_obj_set_pos(_wthr_dy_rain_lbl[i], RX + 2 * HCOL, ymid - 12);
        lv_label_set_text(_wthr_dy_rain_lbl[i], d_rains[i]);

        _wthr_dy_lo_lbl[i] = lv_label_create(root);
        lv_obj_set_style_text_font(_wthr_dy_lo_lbl[i], &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(_wthr_dy_lo_lbl[i], lv_color_hex(0x6878A0), 0);
        lv_obj_set_style_text_align(_wthr_dy_lo_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(_wthr_dy_lo_lbl[i], HCOL);
        lv_obj_set_pos(_wthr_dy_lo_lbl[i], RX + 3 * HCOL, ymid - 12);
        lv_label_set_text(_wthr_dy_lo_lbl[i], d_los[i]);

        _wthr_dy_hi_lbl[i] = lv_label_create(root);
        lv_obj_set_style_text_font(_wthr_dy_hi_lbl[i], &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(_wthr_dy_hi_lbl[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(_wthr_dy_hi_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(_wthr_dy_hi_lbl[i], HCOL);
        lv_obj_set_pos(_wthr_dy_hi_lbl[i], RX + 4 * HCOL, ymid - 12);
        lv_label_set_text(_wthr_dy_hi_lbl[i], d_his[i]);
    }

    // ── Sunrise / Sunset / Moon box ───────────────────────────────────────────
    const int SS_X   = DY_END;
    const int SS_W   = 2 * HCOL;
    const int SS_SEC = DAILY_H / 3;

    lv_obj_t *vline = lv_obj_create(root);
    lv_obj_remove_style_all(vline);
    lv_obj_set_pos(vline, DY_END, DAILY_Y + 8);
    lv_obj_set_size(vline, 1, DAILY_H - 16);
    lv_obj_set_style_bg_color(vline, lv_color_hex(0x2A2E4A), 0);
    lv_obj_set_style_bg_opa(vline, LV_OPA_COVER, 0);

    for (int s = 1; s <= 2; s++) {
        lv_obj_t *hl = lv_obj_create(root);
        lv_obj_remove_style_all(hl);
        lv_obj_set_pos(hl, SS_X, DAILY_Y + s * SS_SEC);
        lv_obj_set_size(hl, SS_W, 1);
        lv_obj_set_style_bg_color(hl, lv_color_hex(0x2A2E4A), 0);
        lv_obj_set_style_bg_opa(hl, LV_OPA_COVER, 0);
    }

    const int ICON44_X = SS_X + (SS_W - 44) / 2;
    const int ICON32_X = SS_X + (SS_W - 32) / 2;

    auto _ss_hdr = [&](const char *txt, int sec_y) {
        lv_obj_t *l = lv_label_create(root);
        lv_obj_set_style_text_font(l, &HB9IIU_JetBrains_Mono_Bold14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x6878A0), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, SS_W);
        lv_obj_set_pos(l, SS_X, sec_y + 10);
        lv_label_set_text(l, txt);
    };
    auto _ss_time = [&](lv_obj_t **out, const char *txt, uint32_t col, int sec_y) {
        *out = lv_label_create(root);
        lv_obj_set_style_text_font(*out, &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(*out, lv_color_hex(col), 0);
        lv_obj_set_style_text_align(*out, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(*out, SS_W);
        lv_obj_set_pos(*out, SS_X, sec_y + 64);
        lv_label_set_text(*out, txt);
    };

    const int SR_Y = DAILY_Y;
    _ss_hdr("Sunrise", SR_Y);
    _wthr_sr_img = lv_img_create(root);
    lv_obj_set_pos(_wthr_sr_img, ICON44_X, SR_Y + 26);
    lv_obj_clear_flag(_wthr_sr_img, LV_OBJ_FLAG_CLICKABLE);
    _ss_time(&_wthr_sunrise_lbl, "--:--", 0xFFDDA0, SR_Y);

    const int SS_Y = DAILY_Y + SS_SEC;
    _ss_hdr("Sunset", SS_Y);
    _wthr_ss_img = lv_img_create(root);
    lv_obj_set_pos(_wthr_ss_img, ICON44_X, SS_Y + 26);
    lv_obj_clear_flag(_wthr_ss_img, LV_OBJ_FLAG_CLICKABLE);
    _ss_time(&_wthr_sunset_lbl, "--:--", 0xFFAA60, SS_Y);

    const int MN_Y = DAILY_Y + SS_SEC * 2;
    _ss_hdr("Moon", MN_Y);
    _wthr_moon_img = lv_img_create(root);
    lv_obj_set_pos(_wthr_moon_img, ICON32_X, MN_Y + 28);
    lv_obj_clear_flag(_wthr_moon_img, LV_OBJ_FLAG_CLICKABLE);
    _wthr_moon_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(_wthr_moon_lbl, &HB9IIU_JetBrains_Mono_Bold14, 0);
    lv_obj_set_style_text_color(_wthr_moon_lbl, lv_color_hex(0xAABBDD), 0);
    lv_obj_set_style_text_align(_wthr_moon_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_wthr_moon_lbl, SS_W);
    lv_obj_set_pos(_wthr_moon_lbl, SS_X, MN_Y + 60);
    lv_label_set_text(_wthr_moon_lbl, "Wax Crescent");
}

// ─── Public API ───────────────────────────────────────────────────────────────

static void weather_page_open(void (*close_ready_cb)())
{
    if (_wthr_panel) return;
    Serial.println("[WTHR] page open");

    lv_obj_t *_open_toast = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_open_toast);
    lv_obj_set_size(_open_toast, 280, 56);
    lv_obj_align(_open_toast, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_open_toast, lv_color_hex(0x1A1C28), 0);
    lv_obj_set_style_bg_opa(_open_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(_open_toast, 14, 0);
    lv_obj_set_style_border_color(_open_toast, lv_color_hex(0x00CC66), 0);
    lv_obj_set_style_border_width(_open_toast, 2, 0);
    lv_obj_set_style_border_opa(_open_toast, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_open_toast, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *_open_toast_lbl = lv_label_create(_open_toast);
    lv_label_set_text(_open_toast_lbl, "Loading weather...");
    lv_obj_set_style_text_font(_open_toast_lbl, &HB9IIU_JetBrains_Mono_Bold20, 0);
    lv_obj_set_style_text_color(_open_toast_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(_open_toast_lbl);
    lv_refr_now(lv_disp_get_default());

    _wthr_close_cb  = close_ready_cb;
    _wthr_slide_idx = 0;
    weather_page_active = true;
    _wthr_sprite = new LGFX_Sprite(&tft);
    _wthr_sprite->setPsram(true);
    _wthr_sprite->setColorDepth(16);
    _wthr_sprite->createSprite(_WTHR_IMG_W, _WTHR_IMG_H);

    _wthr_img_dsc.header.cf          = LV_IMG_CF_TRUE_COLOR;
    _wthr_img_dsc.header.always_zero = 0;
    _wthr_img_dsc.header.reserved    = 0;
    _wthr_img_dsc.header.w           = _WTHR_IMG_W;
    _wthr_img_dsc.header.h           = _WTHR_IMG_H;
    _wthr_img_dsc.data_size          = _WTHR_IMG_W * _WTHR_IMG_H * sizeof(lv_color_t);
    _wthr_img_dsc.data               = (const uint8_t *)_wthr_sprite->getBuffer();

    _wthr_panel = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_wthr_panel);
    lv_obj_set_pos(_wthr_panel, 0, 0);
    lv_obj_set_size(_wthr_panel, _WTHR_SCREEN_W, _WTHR_SCREEN_H);
    lv_obj_set_style_bg_color(_wthr_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_wthr_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_wthr_panel, 0, 0);
    lv_obj_clear_flag(_wthr_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_wthr_panel, _wthr_on_close, LV_EVENT_CLICKED, nullptr);
    lv_obj_move_foreground(_wthr_panel);

    _wthr_img = lv_img_create(_wthr_panel);
    lv_img_set_src(_wthr_img, &_wthr_img_dsc);
    lv_obj_set_pos(_wthr_img, 0, _WTHR_HDR_H);
    lv_obj_clear_flag(_wthr_img, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *right = lv_obj_create(_wthr_panel);
    lv_obj_remove_style_all(right);
    lv_obj_set_pos(right, _WTHR_IMG_W, _WTHR_HDR_H);
    lv_obj_set_size(right, _WTHR_SCREEN_W - _WTHR_IMG_W, _WTHR_IMG_H);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    _wthr_build_right_pane(_wthr_panel);

    for (int i = 0; i < 7; i++)
        _wthr_load_icon("/weatherIcons/not-available.png",
                        &_wthr_hr_ibuf[i], &_wthr_hr_idsc[i], _wthr_hr_icon[i]);
    for (int i = 0; i < 5; i++)
        _wthr_load_icon("/weatherIcons/not-available.png",
                        &_wthr_dy_ibuf[i], &_wthr_dy_idsc[i], _wthr_dy_icon[i]);

    _wthr_load_icon("/weatherIcons/sunrise.png", &_wthr_sr_buf,   &_wthr_sr_dsc,   _wthr_sr_img);
    _wthr_load_icon("/weatherIcons/sunset.png",  &_wthr_ss_buf,   &_wthr_ss_dsc,   _wthr_ss_img);
    struct tm _tm; getLocalTime(&_tm);
    int _phase = moon_phase(_tm.tm_year + 1900, _tm.tm_mon + 1, _tm.tm_mday);
    _wthr_load_icon(moon_icon_path(_phase), &_wthr_moon_buf, &_wthr_moon_dsc, _wthr_moon_img, 32);
    if (_wthr_moon_lbl) lv_label_set_text(_wthr_moon_lbl, moon_phase_name(_phase));

    lv_obj_t *pill = lv_obj_create(_wthr_panel);
    lv_obj_remove_style_all(pill);
    lv_obj_set_pos(pill, 10, 368);
    lv_obj_set_size(pill, 240, 34);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x080F20), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
    lv_obj_set_style_radius(pill, 10, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // City label — use actual city name stored by geolocation
    {
        NVSConfig::LocationData _loc = NVSConfig::loadLocation();
        _wthr_city_lbl = lv_label_create(_wthr_panel);
        lv_obj_set_style_text_font(_wthr_city_lbl, &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(_wthr_city_lbl, lv_color_hex(0xE4F7EA), 0);
        lv_obj_set_style_text_align(_wthr_city_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(_wthr_city_lbl, _WTHR_IMG_W);
        lv_obj_set_pos(_wthr_city_lbl, 0, _WTHR_HDR_H + 8);
        lv_label_set_text(_wthr_city_lbl, (_loc.valid && _loc.city[0]) ? _loc.city : "---");
    }

    _wthr_temp_lbl = lv_label_create(_wthr_panel);
    lv_obj_set_style_text_font(_wthr_temp_lbl, &HB9IIU_JetBrains_Mono_Bold96, 0);
    lv_obj_set_style_text_color(_wthr_temp_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(_wthr_temp_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_wthr_temp_lbl, "--");
    lv_obj_set_width(_wthr_temp_lbl, _WTHR_IMG_W);
    lv_obj_set_pos(_wthr_temp_lbl, 0, 160);

    _wthr_unit_lbl = lv_label_create(_wthr_panel);
    lv_obj_set_style_text_font(_wthr_unit_lbl, &HB9IIU_JetBrains_Mono_Bold30, 0);
    lv_obj_set_style_text_color(_wthr_unit_lbl, lv_color_hex(0xCCDDFF), 0);
    lv_label_set_text(_wthr_unit_lbl, "°C");
    lv_obj_set_pos(_wthr_unit_lbl, 192, 168);

    _wthr_cond_lbl = lv_label_create(_wthr_panel);
    lv_obj_set_style_text_font(_wthr_cond_lbl, &HB9IIU_JetBrains_Mono_Bold30, 0);
    lv_obj_set_style_text_color(_wthr_cond_lbl, lv_color_hex(0xE2EAF4), 0);
    lv_obj_set_style_text_align(_wthr_cond_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_wthr_cond_lbl, "");
    lv_obj_set_width(_wthr_cond_lbl, _WTHR_IMG_W);
    lv_obj_set_pos(_wthr_cond_lbl, 0, 330);

    _wthr_feels_lbl = lv_label_create(_wthr_panel);
    lv_obj_set_style_text_font(_wthr_feels_lbl, &HB9IIU_JetBrains_Mono_Medium20, 0);
    lv_obj_set_style_text_color(_wthr_feels_lbl, lv_color_hex(0x8090B0), 0);
    lv_obj_set_style_text_align(_wthr_feels_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_wthr_feels_lbl, "");
    lv_obj_set_width(_wthr_feels_lbl, _WTHR_IMG_W);
    lv_obj_set_pos(_wthr_feels_lbl, 0, 374);

    static constexpr int _STRIP_Y  = 405;
    static constexpr int _STRIP_H  = 75;
    static constexpr int _COL_W    = 130;

    lv_obj_t *strip = lv_obj_create(_wthr_panel);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 0, _STRIP_Y);
    lv_obj_set_size(strip, _WTHR_IMG_W, _STRIP_H);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x080F20), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_80, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    static constexpr int _VAL_Y = _STRIP_Y + 14;
    static constexpr int _SUB_Y = _VAL_Y + 26;

    auto _make_val = [&](lv_obj_t **out, int col, int xoff = 0) {
        *out = lv_label_create(_wthr_panel);
        lv_obj_set_style_text_font(*out, &HB9IIU_JetBrains_Mono_Bold20, 0);
        lv_obj_set_style_text_color(*out, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(*out, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(*out, _COL_W);
        lv_obj_set_pos(*out, col * _COL_W + xoff, _VAL_Y);
    };
    auto _make_sub = [&](const char *text, int col, int xoff = 0) {
        lv_obj_t *l = lv_label_create(_wthr_panel);
        lv_obj_set_style_text_font(l, &HB9IIU_JetBrains_Mono_Bold14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x6878A0), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, _COL_W);
        lv_obj_set_pos(l, col * _COL_W + xoff, _SUB_Y);
        lv_label_set_text(l, text);
    };
    _make_val(&_wthr_humidity_lbl, 0);      _make_sub("Humidity", 0);
    _make_val(&_wthr_pressure_lbl, 1, -5);  _make_sub("Pressure", 1, -5);

    _wthr_build_header(_wthr_panel);

    if (_wthr_fetch_data.valid) {
        weather_page_update_data(_wthr_fetch_data);
    } else {
        _wthr_slide_idx = 3;
        _wthr_load_slide();
        lv_label_set_text(_wthr_temp_lbl,     "--");
        lv_label_set_text(_wthr_cond_lbl,     "");
        lv_label_set_text(_wthr_feels_lbl,    "");
        lv_label_set_text(_wthr_humidity_lbl, "--");
        lv_label_set_text(_wthr_pressure_lbl, "--");
    }

    lv_obj_del(_open_toast);
}

static void weather_page_close()
{
    if (!_wthr_panel) return;
    if (_wthr_timer) { lv_timer_del(_wthr_timer); _wthr_timer = nullptr; }
    lv_obj_del(_wthr_panel);
    _wthr_panel        = nullptr;
    _wthr_img          = nullptr;
    _wthr_temp_lbl     = nullptr;
    _wthr_unit_lbl     = nullptr;
    _wthr_cond_lbl     = nullptr;
    _wthr_feels_lbl    = nullptr;
    _wthr_city_lbl     = nullptr;
    _wthr_humidity_lbl = nullptr;
    _wthr_pressure_lbl = nullptr;
    for (int i = 0; i < 7; i++) { if (_wthr_hr_ibuf[i]) { free(_wthr_hr_ibuf[i]); _wthr_hr_ibuf[i] = nullptr; } }
    for (int i = 0; i < 5; i++) { if (_wthr_dy_ibuf[i]) { free(_wthr_dy_ibuf[i]); _wthr_dy_ibuf[i] = nullptr; } }
    if (_wthr_sr_buf)   { free(_wthr_sr_buf);   _wthr_sr_buf   = nullptr; }
    if (_wthr_ss_buf)   { free(_wthr_ss_buf);   _wthr_ss_buf   = nullptr; }
    if (_wthr_moon_buf) { free(_wthr_moon_buf); _wthr_moon_buf = nullptr; }
    _wthr_sr_img = nullptr; _wthr_ss_img = nullptr;
    _wthr_moon_img = nullptr; _wthr_moon_lbl = nullptr;
    _wthr_sunrise_lbl = nullptr; _wthr_sunset_lbl = nullptr;
    for (int i = 0; i < 7; i++) {
        _wthr_hr_icon[i]     = nullptr;
        _wthr_hr_time_lbl[i] = nullptr;
        _wthr_hr_temp_lbl[i] = nullptr;
        _wthr_hr_rain_lbl[i] = nullptr;
    }
    for (int i = 0; i < 5; i++) {
        _wthr_dy_icon[i]     = nullptr;
        _wthr_dy_name_lbl[i] = nullptr;
        _wthr_dy_lo_lbl[i]   = nullptr;
        _wthr_dy_hi_lbl[i]   = nullptr;
        _wthr_dy_rain_lbl[i] = nullptr;
    }
    if (_wthr_sprite) {
        _wthr_sprite->deleteSprite();
        delete _wthr_sprite;
        _wthr_sprite = nullptr;
    }
    weather_page_active = false;
    if (_wthr_close_cb) { _wthr_close_cb(); _wthr_close_cb = nullptr; }
}

// ─── Push real API data into all LVGL widgets ─────────────────────────────────
static void weather_page_update_data(const WeatherData &d)
{
    if (!_wthr_panel) return;
    if (_wthr_timer) { lv_timer_del(_wthr_timer); _wthr_timer = nullptr; }
    if (!d.valid) { Serial.println("[WTHR] update_data: invalid data, skipping"); return; }
    Serial.printf("[WTHR] update_data: temp=%.1f wmo=%d slide=%d\n",
                  d.temp_c, d.wmo_code, wmo_to_slide(d.wmo_code, d.is_day));
    _wthr_slide_idx = wmo_to_slide(d.wmo_code, d.is_day);
    _wthr_load_slide();

    char buf[48];

    snprintf(buf, sizeof(buf), "%d", (int)roundf(d.temp_c));
    lv_label_set_text(_wthr_temp_lbl, buf);

    lv_label_set_text(_wthr_cond_lbl, wmo_to_condition_str(d.wmo_code));

    snprintf(buf, sizeof(buf), "Feels like %.1f°C", d.feels_like_c);
    lv_label_set_text(_wthr_feels_lbl, buf);

    snprintf(buf, sizeof(buf), "%d%%", d.humidity_pct);
    lv_label_set_text(_wthr_humidity_lbl, buf);

    snprintf(buf, sizeof(buf), "%d hPa", d.pressure_hpa);
    lv_label_set_text(_wthr_pressure_lbl, buf);

    for (int i = 0; i < 7; i++) {
        lv_label_set_text(_wthr_hr_time_lbl[i], d.hr_time[i]);

        float _t = (i == 0) ? d.temp_c : d.hr_temp[i];
        snprintf(buf, sizeof(buf), "%d°", (int)roundf(_t));
        lv_label_set_text(_wthr_hr_temp_lbl[i], buf);

        if (d.hr_rain_pct[i] > 0) snprintf(buf, sizeof(buf), "%d%%", d.hr_rain_pct[i]);
        else buf[0] = '\0';
        lv_label_set_text(_wthr_hr_rain_lbl[i], buf);

        _wthr_load_icon(wmo_to_icon_path(d.hr_wmo[i], d.hr_is_day[i]),
                        &_wthr_hr_ibuf[i], &_wthr_hr_idsc[i], _wthr_hr_icon[i]);
    }

    for (int i = 0; i < 5; i++) {
        lv_label_set_text(_wthr_dy_name_lbl[i], d.dy_name[i]);

        snprintf(buf, sizeof(buf), "%d°", (int)roundf(d.dy_lo[i]));
        lv_label_set_text(_wthr_dy_lo_lbl[i], buf);

        snprintf(buf, sizeof(buf), "%d°", (int)roundf(d.dy_hi[i]));
        lv_label_set_text(_wthr_dy_hi_lbl[i], buf);

        if (d.dy_rain_pct[i] > 0) snprintf(buf, sizeof(buf), "%d%%", d.dy_rain_pct[i]);
        else buf[0] = '\0';
        lv_label_set_text(_wthr_dy_rain_lbl[i], buf);

        _wthr_load_icon(wmo_to_icon_path(d.dy_wmo[i], true),
                        &_wthr_dy_ibuf[i], &_wthr_dy_idsc[i], _wthr_dy_icon[i]);
    }

    lv_label_set_text(_wthr_sunrise_lbl, d.sunrise);
    lv_label_set_text(_wthr_sunset_lbl,  d.sunset);

    _wthr_load_icon(moon_icon_path(d.moon_phase_idx),
                    &_wthr_moon_buf, &_wthr_moon_dsc, _wthr_moon_img, 32);
    lv_label_set_text(_wthr_moon_lbl, moon_phase_name(d.moon_phase_idx));
}

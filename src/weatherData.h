#pragma once

struct WeatherData {
    // ── Current conditions ────────────────────────────────────────────────────
    float  temp_c;           // temperature_2m
    float  feels_like_c;     // apparent_temperature
    int    humidity_pct;     // relative_humidity_2m
    int    pressure_hpa;     // surface_pressure (rounded)
    int    wmo_code;         // weather_code
    bool   is_day;           // is_day (1=day, 0=night)

    // ── Hourly — 7 slots from current hour ───────────────────────────────────
    char   hr_time[7][6];    // "14h", "+1h" … displayed label
    float  hr_temp[7];
    int    hr_rain_pct[7];   // precipitation_probability (0 = hide)
    int    hr_wmo[7];
    bool   hr_is_day[7];

    // ── Daily — 5 days (today+1 … today+5) ──────────────────────────────────
    char   dy_name[5][4];    // "Mon", "Tue" …
    float  dy_lo[5];         // temperature_2m_min
    float  dy_hi[5];         // temperature_2m_max
    int    dy_rain_pct[5];   // precipitation_probability_max
    int    dy_wmo[5];

    // ── Sun & Moon ────────────────────────────────────────────────────────────
    char   sunrise[6];       // "06:42"
    char   sunset[6];        // "20:18"
    int    moon_phase_idx;   // 0-7  (computed locally from date)

    bool   valid;            // true once a successful fetch has completed
};

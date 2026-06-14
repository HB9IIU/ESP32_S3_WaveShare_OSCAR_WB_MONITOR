#pragma once
#include <math.h>

// ── Moon phase ────────────────────────────────────────────────────────────────
// Returns 0–7: 0=new, 1=waxing crescent, 2=first quarter, 3=waxing gibbous,
//              4=full, 5=waning gibbous, 6=last quarter, 7=waning crescent
static int moon_phase(int year, int month, int day)
{
    // Approximate Julian Day Number
    double jd = 367.0*year
              - (int)(7*(year+(int)((month+9)/12))/4)
              + (int)(275*month/9)
              + day - 730530.0;
    double cycle = fmod(jd / 29.53058868, 1.0);
    if (cycle < 0) cycle += 1.0;
    return (int)(cycle * 8 + 0.5) % 8;
}

static const char *moon_phase_name(int phase)
{
    static const char *names[] = {
        "New Moon", "Wax Crescent", "First Quarter", "Wax Gibbous",
        "Full Moon", "Wan Gibbous",  "Last Quarter",  "Wan Crescent",
    };
    return names[phase & 7];
}

static const char *moon_icon_path(int phase)
{
    static const char *icons[] = {
        "/weatherIcons/moon-new.png",
        "/weatherIcons/moon-waxing-crescent.png",
        "/weatherIcons/moon-first-quarter.png",
        "/weatherIcons/moon-waxing-gibbous.png",
        "/weatherIcons/moon-full.png",
        "/weatherIcons/moon-waning-gibbous.png",
        "/weatherIcons/moon-last-quarter.png",
        "/weatherIcons/moon-waning-crescent.png",
    };
    return icons[phase & 7];
}

// ── WMO code → human-readable condition string ────────────────────────────────
static const char *wmo_to_condition_str(int code)
{
    if (code == 0)          return "Clear Sky";
    if (code <= 2)          return "Partly Cloudy";
    if (code == 3)          return "Overcast";
    if (code <= 48)         return "Foggy";
    if (code <= 55)         return "Drizzle";
    if (code <= 57)         return "Freezing Drizzle";
    if (code <= 63)         return "Rain";
    if (code == 65)         return "Heavy Rain";
    if (code <= 67)         return "Freezing Rain";
    if (code <= 73)         return "Snow";
    if (code == 75)         return "Heavy Snow";
    if (code == 77)         return "Snow Grains";
    if (code <= 81)         return "Rain Showers";
    if (code == 82)         return "Heavy Showers";
    if (code <= 86)         return "Snow Showers";
    if (code == 95)         return "Thunderstorm";
    if (code <= 99)         return "Storm + Hail";
    return "N/A";
}

// ── WMO code → JPEG background slide index ────────────────────────────────────
// Day slides 0-10, Night slides 11-21 (mirror of day, same order)
static uint8_t wmo_to_slide(int code, bool is_day)
{
    int d, n;
    if      (code == 0)   { d=0;  n=11; }  // sunny / clear night
    else if (code <= 2)   { d=1;  n=12; }  // partly cloudy
    else if (code == 3)   { d=3;  n=14; }  // overcast
    else if (code <= 48)  { d=8;  n=19; }  // fog
    else if (code <= 63)  { d=4;  n=15; }  // drizzle / light-moderate rain
    else if (code <= 67)  { d=5;  n=16; }  // heavy / freezing rain
    else if (code <= 77)  { d=7;  n=18; }  // snow
    else if (code <= 81)  { d=4;  n=15; }  // light rain showers
    else if (code == 82)  { d=5;  n=16; }  // violent rain showers
    else if (code <= 86)  { d=7;  n=18; }  // snow showers
    else                  { d=6;  n=17; }  // thunderstorm / hail
    return is_day ? (uint8_t)d : (uint8_t)n;
}

/**
 * wmo_to_icon_path()
 *
 * Maps an Open-Meteo WMO weather code + is_day flag to the matching
 * /weatherIcons/<name>.png path on LittleFS.
 *
 * Returns a pointer to a static buffer — use immediately, do not store.
 *
 * WMO code reference (Open-Meteo):
 *   0        Clear sky
 *   1–2      Mainly clear / Partly cloudy
 *   3        Overcast
 *   45/48    Fog / Rime fog
 *   51/53/55 Drizzle L/M/H
 *   56/57    Freezing drizzle L/H
 *   61/63/65 Rain L/M/H
 *   66/67    Freezing rain L/H
 *   71/73/75 Snow L/M/H
 *   77       Snow grains
 *   80/81/82 Rain showers L/M/violent
 *   85/86    Snow showers L/H
 *   95       Thunderstorm
 *   96/99    Thunderstorm + hail L/H
 */
static const char *wmo_to_icon_path(int code, bool is_day)
{
    static char path[48];
    const char *name;

    switch (code) {
        case 0:                     name = is_day ? "clear-day" : "clear-night";               break;
        case 1: case 2:             name = is_day ? "partly-cloudy-day" : "partly-cloudy-night"; break;
        case 3:                     name = "overcast";          break;
        case 45: case 48:           name = "fog";               break;
        case 51: case 53: case 55:  name = "drizzle";           break;
        case 56: case 57:           name = "sleet";             break;
        case 61: case 63:           name = "rain";              break;
        case 65:                    name = "extreme-rain";      break;
        case 66: case 67:           name = "sleet";             break;
        case 71: case 73: case 77:  name = "snow";              break;
        case 75:                    name = "extreme-snow";      break;
        case 80: case 81:           name = "rain";              break;
        case 82:                    name = "extreme-rain";      break;
        case 85:                    name = "snow";              break;
        case 86:                    name = "extreme-snow";      break;
        case 95:                    name = "thunderstorms";     break;
        case 96:                    name = "thunderstorms-rain"; break;
        case 99:                    name = "hail";              break;
        default:                    name = "not-available";     break;
    }

    snprintf(path, sizeof(path), "/weatherIcons/%s.png", name);
    return path;
}

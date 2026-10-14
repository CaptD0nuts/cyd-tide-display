// Drawing: an animated beach cross-section whose water line follows the
// tide, a 36-hour tide curve, and the next high/low along the bottom.
#include "scene.h"
#include <math.h>

#define SCENE_H   150
#define CHART_Y   150
#define CHART_H   58
#define INFO_Y    208

// Water line height on screen for the lowest / highest level in the data.
#define LVL_LOW_Y  128
#define LVL_HIGH_Y 52
// Beach slope: sand starts at SAND_X0 on the bottom edge and rises to the right.
#define SAND_X0    130
#define SAND_SLOPE 0.62f
#define POLE_X     22

// Fallback sunrise/sunset (local hours) until the station position is known.
#define DEFAULT_SUNRISE 6.8f
#define DEFAULT_SUNSET  18.3f

static TFT_eSPI *s_tft;
static TFT_eSprite *s_scene, *s_chart;

struct Rgb { float r, g, b; };

static uint16_t rgb(Rgb c, float k = 1) {
  return s_tft->color565(constrain(c.r * k, 0, 255), constrain(c.g * k, 0, 255), constrain(c.b * k, 0, 255));
}

static Rgb mix(Rgb a, Rgb b, float u) {
  return {a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u};
}

static float s_lat = NAN, s_lon = NAN;

void sceneSetLocation(float lat, float lon) {
  s_lat = lat;
  s_lon = lon;
}

// Sunrise and sunset (local hours) on the local day containing t, from the
// standard sunrise equation; within a couple of minutes at mid latitudes.
// Cached per day since the time-lapse asks every frame.
static void sunTimes(double t, float &rise, float &set) {
  static int cachedDay = -1;
  static float cRise, cSet;
  rise = DEFAULT_SUNRISE;
  set = DEFAULT_SUNSET;
  if (isnan(s_lat)) return;
  time_t tt = (time_t)t;
  struct tm lt;
  localtime_r(&tt, &lt);
  int day = lt.tm_year * 400 + lt.tm_yday;
  if (day != cachedDay) {
    lt.tm_hour = 12;
    lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    time_t noon = mktime(&lt);
    double jd = noon / 86400.0 + 2440587.5;
    double j = round(jd - 2451545.0 + s_lon / 360.0) - s_lon / 360.0;
    double m = fmod(357.5291 + 0.98560028 * j, 360.0) * DEG_TO_RAD;
    double c = 1.9148 * sin(m) + 0.02 * sin(2 * m) + 0.0003 * sin(3 * m);
    double l = fmod(m * RAD_TO_DEG + c + 180 + 102.9372, 360.0) * DEG_TO_RAD;
    double transit = 2451545.0 + j + 0.0053 * sin(m) - 0.0069 * sin(2 * l);
    double sinDec = sin(l) * sin(23.4397 * DEG_TO_RAD);
    double cosDec = cos(asin(sinDec));
    double phi = s_lat * DEG_TO_RAD;
    double cosW = (sin(-0.833 * DEG_TO_RAD) - sin(phi) * sinDec) / (cos(phi) * cosDec);
    double w = acos(constrain(cosW, -1.0, 1.0)) * RAD_TO_DEG;
    // Julian dates to hours after local midnight.
    double midnight = noon - 12 * 3600.0;
    cRise = ((transit - w / 360.0 - 2440587.5) * 86400.0 - midnight) / 3600.0;
    cSet = ((transit + w / 360.0 - 2440587.5) * 86400.0 - midnight) / 3600.0;
    cachedDay = day;
    Serial.printf("sun: rise %.2f set %.2f (local hours)\n", cRise, cSet);
  }
  rise = cRise;
  set = cSet;
}

struct MoonPos {
  double elong;  // degrees from the sun: 0 new, 90 first quarter, 180 full, 270 last quarter
  double alt;    // geocentric altitude, degrees (NAN until the station position is known)
  double az;     // azimuth, degrees east of north
};

// Moon from low-precision lunar theory with the main periodic terms. Phase
// times land within about an hour of USNO's, rise/set within a few minutes.
static MoonPos moonPos(double t) {
  MoonPos r;
  double d = t / 86400.0 + 2440587.5 - 2451545.0;
  double ms = fmod(357.5291 + 0.98560028 * d, 360.0) * DEG_TO_RAD;
  double ls = 280.4665 + 0.98564736 * d + 1.9148 * sin(ms) + 0.02 * sin(2 * ms);
  double mm = fmod(134.963 + 13.064993 * d, 360.0) * DEG_TO_RAD;
  double lm0 = 218.316 + 13.176396 * d;
  double d0 = fmod(lm0 - ls, 360.0) * DEG_TO_RAD;
  double lm = lm0 + 6.289 * sin(mm) + 1.274 * sin(2 * d0 - mm) + 0.658 * sin(2 * d0) - 0.186 * sin(ms) -
              0.059 * sin(2 * d0 - 2 * mm) - 0.057 * sin(2 * d0 - ms - mm) + 0.053 * sin(2 * d0 + mm);
  double e = fmod(lm - ls, 360.0);
  r.elong = e < 0 ? e + 360 : e;
  r.alt = r.az = NAN;
  if (isnan(s_lat)) return r;

  double f = fmod(93.272 + 13.229350 * d, 360.0) * DEG_TO_RAD;
  double lam = lm * DEG_TO_RAD, beta = 5.128 * sin(f) * DEG_TO_RAD, eps = 23.4397 * DEG_TO_RAD;
  double ra = atan2(sin(lam) * cos(eps) - tan(beta) * sin(eps), cos(lam));
  double dec = asin(sin(beta) * cos(eps) + cos(beta) * sin(eps) * sin(lam));
  double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  double ha = (gmst + s_lon) * DEG_TO_RAD - ra;
  double phi = s_lat * DEG_TO_RAD;
  r.alt = asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(ha)) * RAD_TO_DEG;
  r.az = fmod(atan2(sin(ha), cos(ha) * sin(phi) - tan(dec) * cos(phi)) * RAD_TO_DEG + 540.0, 360.0);
  return r;
}

// Moon disk lit to match the phase: waxing lit on the right, waning on the
// left (as seen from the northern hemisphere).
static void drawMoon(TFT_eSprite &s, int cx, int cy, int r, double elong, uint16_t dark) {
  uint16_t lit = s_tft->color565(235, 235, 215);
  s.fillCircle(cx, cy, r, dark);
  float c = cosf(elong * DEG_TO_RAD);
  bool waxing = elong < 180;
  for (int dy = -r; dy <= r; ++dy) {
    float hw = sqrtf((float)(r * r - dy * dy));
    float term = hw * c;  // terminator offset across this row
    int x0 = waxing ? cx + (int)roundf(term) : cx - (int)roundf(hw);
    int x1 = waxing ? cx + (int)roundf(hw) : cx - (int)roundf(term);
    if (x1 > x0) s.drawFastHLine(x0, cy + dy, x1 - x0 + 1, lit);
  }
}

static float localHour(double t) {
  time_t tt = (time_t)t;
  struct tm lt;
  localtime_r(&tt, &lt);
  return lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
}

// Sky color through the day plus a 0..1 daylight factor for dimming the scene.
static Rgb skyColor(float h, float rise, float set, float &light) {
  const struct { float h; Rgb c; float light; } keys[] = {
      {0, {8, 10, 35}, 0.35f},
      {rise - 0.7f, {8, 10, 35}, 0.35f},
      {rise, {235, 125, 70}, 0.7f},
      {rise + 0.8f, {90, 165, 235}, 1.0f},
      {set - 0.6f, {90, 165, 235}, 1.0f},
      {set, {240, 110, 60}, 0.7f},
      {set + 0.8f, {8, 10, 35}, 0.35f},
      {24, {8, 10, 35}, 0.35f},
  };
  for (size_t i = 1; i < sizeof keys / sizeof keys[0]; ++i) {
    if (h <= keys[i].h) {
      float u = (h - keys[i - 1].h) / (keys[i].h - keys[i - 1].h);
      light = keys[i - 1].light + (keys[i].light - keys[i - 1].light) * u;
      return mix(keys[i - 1].c, keys[i].c, u);
    }
  }
  light = 0.35f;
  return keys[0].c;
}

static float s_lo, s_hi;  // level range the scene and chart are scaled to

static void setRange(const TideData &d) {
  s_lo = d.minV;
  s_hi = d.maxV;
  if (s_hi - s_lo < 0.3f) {  // flat data: keep the scale sane
    float mid = (s_lo + s_hi) / 2;
    s_lo = mid - 0.15f;
    s_hi = mid + 0.15f;
  }
}

static float levelY(float v) {
  return LVL_LOW_Y + (v - s_lo) / (s_hi - s_lo) * (LVL_HIGH_Y - LVL_LOW_Y);
}

static float sandY(float x) {
  return SCENE_H - (x - SAND_X0) * SAND_SLOPE;
}

static void fmtTime(time_t t, char *out, size_t n, bool shortForm) {
  struct tm lt;
  localtime_r(&t, &lt);
  int h12 = lt.tm_hour % 12 ? lt.tm_hour % 12 : 12;
  if (shortForm) snprintf(out, n, "%d:%02d%c", h12, lt.tm_min, lt.tm_hour < 12 ? 'a' : 'p');
  else snprintf(out, n, "%d:%02d %s", h12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

void sceneBegin(TFT_eSPI &tft) {
  s_tft = &tft;
  s_scene = new TFT_eSprite(&tft);
  s_scene->setColorDepth(8);
  s_scene->createSprite(320, SCENE_H);
  s_chart = new TFT_eSprite(&tft);
  s_chart->setColorDepth(8);
  s_chart->createSprite(320, CHART_H);
  tft.fillScreen(TFT_BLACK);
}

static void shadowText(TFT_eSprite &s, const char *txt, int x, int y, int font, uint16_t color) {
  s.setTextColor(TFT_BLACK);
  s.drawString(txt, x + 1, y + 1, font);
  s.setTextColor(color);
  s.drawString(txt, x, y, font);
}

void sceneDraw(const TideData &d, double t, bool lapse, bool stale, uint32_t ms) {
  TFT_eSprite &s = *s_scene;
  setRange(d);
  float h = localHour(t);
  float rise, set;
  sunTimes(t, rise, set);
  static bool loggedMoon = false;
  if (!loggedMoon) {
    MoonPos m = moonPos(t);
    Serial.printf("moon: elongation %.1f deg, %.0f%% lit, %s, alt %.1f az %.0f\n", m.elong,
                  (1 - cos(m.elong * DEG_TO_RAD)) / 2 * 100, m.elong < 180 ? "waxing" : "waning", m.alt, m.az);
    loggedMoon = true;
  }
  float light;
  Rgb sky = skyColor(h, rise, set, light);

  // Sky, darker at the top.
  for (int y = 0; y < SCENE_H; ++y) s.drawFastHLine(0, y, 320, rgb(sky, 0.65f + 0.35f * y / SCENE_H));

  if (light < 0.6f) {  // stars, twinkling
    for (int i = 0; i < 28; ++i) {
      int x = (i * 97 + 13) % 320, y = (i * 53 + 7) % 70;
      if (((ms / 400) + i * 7) % 11) s.drawPixel(x, y, TFT_WHITE);
    }
  }
  if (h > rise - 0.3f && h < set + 0.3f) {
    float u = (h - (rise - 0.3f)) / (set - rise + 0.6f);
    int x = 20 + u * 260, y = 105 - sinf(u * PI) * 85;
    s.fillCircle(x, y, 10, rgb({255, 220, 90}));
  }
  // Moon wherever it really is, day or night: east on the left, west on the
  // right, like the sun. Rises/sets at +0.125 deg geocentric (USNO's convention).
  MoonPos m = moonPos(t);
  if (isnan(m.alt)) {
    // No station position yet: fall back to riding the night sky.
    if (light < 0.6f) {
      float hn = h < rise ? h + 24 : h;
      float u = (hn - (set + 0.3f)) / (24 + rise - set - 0.6f);
      drawMoon(s, 20 + u * 260, 105 - sinf(u * PI) * 85, 9, m.elong, rgb(sky, 1.1f));
    }
  } else if (m.alt > 0.125) {
    int x = 10 + (m.az - 60) / 240 * 300, y = max(12, (int)(105 - m.alt * 1.15));
    drawMoon(s, x, y, 9, m.elong, light < 0.6f ? rgb(sky, 1.1f) : rgb(sky, 0.65f + 0.35f * y / SCENE_H));
  }

  // Water: two sine swells riding on the tide level.
  float level = tideLevel(d, t);
  float wl = levelY(level);
  float k = 0.45f + 0.55f * light;
  uint16_t deep = rgb({10, 70, 110}, k), mid = rgb({25, 135, 165}, k), crest = rgb({170, 230, 235}, k);
  for (int x = 0; x < 320; ++x) {
    float top = wl + 2.5f * sinf(x * 0.045f + ms * 0.0021f) + 1.5f * sinf(x * 0.11f - ms * 0.0033f);
    int ty = (int)top;
    s.drawFastVLine(x, ty, SCENE_H - ty, mid);
    if (ty + 18 < SCENE_H) s.drawFastVLine(x, ty + 18, SCENE_H - ty - 18, deep);
    s.drawPixel(x, ty, crest);
  }

  // Beach, with swash running up and back at the water's edge.
  uint16_t sand = rgb({225, 200, 140}, k), wet = rgb({170, 145, 95}, k);
  s.fillTriangle(SAND_X0, SCENE_H, 319, SCENE_H, 319, sandY(319), sand);
  float shoreX = SAND_X0 + (SCENE_H - wl) / SAND_SLOPE;
  float swash = 6 + 5 * sinf(ms * 0.0016f);
  for (int j = 1; j <= 3; ++j)
    s.drawLine(shoreX - 4, wl + j, shoreX + 14, wl - 14 * SAND_SLOPE + j, wet);
  for (int j = 0; j < 2; ++j)
    s.drawLine(shoreX - 2, wl - j, shoreX + swash, wl - swash * SAND_SLOPE - j, TFT_WHITE);

  // Umbrella on the dry sand.
  int ux = 306, uy = sandY(ux);
  s.drawFastVLine(ux, uy - 26, 26, rgb({90, 70, 50}, k));
  s.fillTriangle(ux - 15, uy - 22, ux + 13, uy - 22, ux - 1, uy - 34, rgb({220, 50, 50}, k));
  s.fillTriangle(ux - 6, uy - 22, ux + 4, uy - 22, ux - 1, uy - 34, rgb({245, 240, 230}, k));

  // Tide staff with a mark every half foot.
  s.fillRect(POLE_X, 30, 4, SCENE_H - 30, rgb({120, 90, 60}, k));
  s.setTextDatum(ML_DATUM);
  for (float v = ceilf(s_lo * 2) / 2; v <= s_hi + 0.001f; v += 0.5f) {
    int y = levelY(v);
    s.drawFastHLine(POLE_X - 3, y, 10, TFT_WHITE);
    char lbl[8];
    snprintf(lbl, sizeof lbl, "%.1f", v);
    shadowText(s, lbl, POLE_X + 9, y, 1, TFT_WHITE);
  }

  // Readout.
  char buf[40];
  float rate = (tideLevel(d, t + 1800) - tideLevel(d, t - 1800));  // ft per hour
  s.setTextDatum(TL_DATUM);
  snprintf(buf, sizeof buf, "%.2f ft", level);
  shadowText(s, buf, 52, 6, 4, TFT_WHITE);
  if (fabsf(rate) < 0.02f) snprintf(buf, sizeof buf, "SLACK");
  else snprintf(buf, sizeof buf, "%s %.2f ft/hr", rate > 0 ? "RISING" : "FALLING", fabsf(rate));
  shadowText(s, buf, 52, 34, 2, rate > 0.02f ? rgb({120, 255, 160}) : rate < -0.02f ? rgb({255, 190, 90}) : TFT_WHITE);
  // Arrow next to the trend.
  int ax = 52 + s.textWidth(buf, 2) + 8, ay = 42;
  if (rate > 0.02f) s.fillTriangle(ax, ay + 5, ax + 10, ay + 5, ax + 5, ay - 5, rgb({120, 255, 160}));
  else if (rate < -0.02f) s.fillTriangle(ax, ay - 5, ax + 10, ay - 5, ax + 5, ay + 5, rgb({255, 190, 90}));

  s.setTextDatum(TR_DATUM);
  fmtTime((time_t)t, buf, sizeof buf, false);
  shadowText(s, buf, 314, 6, 2, lapse ? TFT_YELLOW : TFT_WHITE);
  if (lapse) shadowText(s, "TIME-LAPSE", 314, 24, 2, TFT_YELLOW);
  else if (stale) shadowText(s, "NO SIGNAL", 314, 24, 2, TFT_RED);

  s.pushSprite(0, 0);
}

void chartDraw(const TideData &d, double t) {
  TFT_eSprite &s = *s_chart;
  setRange(d);
  const double a = t - 12 * 3600.0, b = t + 24 * 3600.0;
  auto xOf = [&](double tt) { return (int)((tt - a) / (b - a) * 319); };
  auto yOf = [&](float v) { return (int)(CHART_H - 12 - (v - s_lo) / (s_hi - s_lo) * (CHART_H - 26)); };

  s.fillSprite(s_tft->color565(6, 14, 30));
  s.setTextColor(s_tft->color565(110, 130, 160));
  s.setTextDatum(TL_DATUM);

  // Midnight lines with the day name.
  time_t ta = (time_t)a;
  struct tm lt;
  localtime_r(&ta, &lt);
  lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
  lt.tm_isdst = -1;
  for (int i = 1; i <= 3; ++i) {
    struct tm m = lt;
    m.tm_mday += i;
    time_t mid = mktime(&m);
    if (mid <= a || mid >= b) continue;
    int x = xOf(mid);
    s.drawFastVLine(x, 0, CHART_H, s_tft->color565(40, 50, 75));
    char day[4];
    strftime(day, sizeof day, "%a", &m);
    s.drawString(day, x + 3, 2, 1);
  }

  uint16_t fill = s_tft->color565(20, 90, 120), line = s_tft->color565(90, 220, 240);
  for (int x = 0; x < 320; ++x) {
    double tt = a + (b - a) * x / 319;
    if (!tideCovers(d, tt)) continue;
    int y = yOf(tideLevel(d, tt));
    s.drawFastVLine(x, y, CHART_H - y, fill);
    s.drawFastVLine(x, y - 1, 2, line);
  }

  s.setTextColor(TFT_WHITE);
  for (int i = 0; i < d.nx; ++i) {
    const Extreme &e = d.x[i];
    if (e.t < a || e.t > b) continue;
    int x = xOf(e.t), y = yOf(e.v);
    s.fillCircle(x, y, 2, TFT_WHITE);
    char lbl[12];
    fmtTime(e.t, lbl, sizeof lbl, true);
    int w = s.textWidth(lbl, 1);
    int lx = constrain(x - w / 2, 0, 319 - w);
    s.drawString(lbl, lx, e.high ? y - 11 : y + 4, 1);
  }

  int xn = xOf(t);
  s.drawFastVLine(xn, 0, CHART_H, TFT_YELLOW);
  if (tideCovers(d, t)) s.fillCircle(xn, yOf(tideLevel(d, t)), 3, TFT_YELLOW);

  s.pushSprite(0, CHART_Y);
}

static char last1[64], last2[64], last3[48];  // what the info lines show now

void infoDraw(const TideData &d, double t, const char *station) {
  char l1[64] = "", l2[64] = "", tm[12];
  const Extreme *n1 = tideNext(d, t), *n2 = tideNext(d, t, 1);
  if (n1) {
    long mins = (long)((n1->t - t) / 60);
    fmtTime(n1->t, tm, sizeof tm, false);
    snprintf(l1, sizeof l1, "NEXT %s %s  %.1f ft  in %ldh %02ldm", n1->high ? "HIGH" : "LOW", tm, n1->v, mins / 60, mins % 60);
  } else {
    strlcpy(l1, "no upcoming high/low in data", sizeof l1);
  }
  if (n2) {
    fmtTime(n2->t, tm, sizeof tm, false);
    snprintf(l2, sizeof l2, "then %s %s  %.1f ft", n2->high ? "HIGH" : "LOW", tm, n2->v);
  }
  if (!strcmp(l1, last1) && !strcmp(l2, last2) && !strcmp(station, last3)) return;
  strlcpy(last1, l1, sizeof last1);
  strlcpy(last2, l2, sizeof last2);
  strlcpy(last3, station, sizeof last3);

  TFT_eSPI &tft = *s_tft;
  tft.fillRect(0, INFO_Y, 320, 240 - INFO_Y, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(n1 && n1->high ? TFT_CYAN : TFT_ORANGE, TFT_BLACK);
  tft.drawString(l1, 4, INFO_Y + 1, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(l2, 4, INFO_Y + 17, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(station, 316, INFO_Y + 21, 1);
}

void sceneMessage(const char *line1, const char *line2, const char *line3) {
  TFT_eSPI &tft = *s_tft;
  last1[0] = last2[0] = last3[0] = 0;  // info lines get wiped: repaint them next time
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(line1, 160, 90, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(line2, 160, 130, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(line3, 160, 155, 2);
}

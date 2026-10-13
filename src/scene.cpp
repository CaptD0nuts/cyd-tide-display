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

// Rough sunrise/sunset (local hours) for the sky and sun; mid-October on the
// Gulf coast. Cosmetic only.
#define SUNRISE 6.8f
#define SUNSET  18.3f

static TFT_eSPI *s_tft;
static TFT_eSprite *s_scene, *s_chart;

struct Rgb { float r, g, b; };

static uint16_t rgb(Rgb c, float k = 1) {
  return s_tft->color565(constrain(c.r * k, 0, 255), constrain(c.g * k, 0, 255), constrain(c.b * k, 0, 255));
}

static Rgb mix(Rgb a, Rgb b, float u) {
  return {a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u};
}

static float localHour(double t) {
  time_t tt = (time_t)t;
  struct tm lt;
  localtime_r(&tt, &lt);
  return lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
}

// Sky color through the day plus a 0..1 daylight factor for dimming the scene.
static Rgb skyColor(float h, float &light) {
  static const struct { float h; Rgb c; float light; } keys[] = {
      {0, {8, 10, 35}, 0.35f},
      {SUNRISE - 0.7f, {8, 10, 35}, 0.35f},
      {SUNRISE, {235, 125, 70}, 0.7f},
      {SUNRISE + 0.8f, {90, 165, 235}, 1.0f},
      {SUNSET - 0.6f, {90, 165, 235}, 1.0f},
      {SUNSET, {240, 110, 60}, 0.7f},
      {SUNSET + 0.8f, {8, 10, 35}, 0.35f},
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
  float light;
  Rgb sky = skyColor(h, light);

  // Sky, darker at the top.
  for (int y = 0; y < SCENE_H; ++y) s.drawFastHLine(0, y, 320, rgb(sky, 0.65f + 0.35f * y / SCENE_H));

  if (light < 0.6f) {  // stars, twinkling
    for (int i = 0; i < 28; ++i) {
      int x = (i * 97 + 13) % 320, y = (i * 53 + 7) % 70;
      if (((ms / 400) + i * 7) % 11) s.drawPixel(x, y, TFT_WHITE);
    }
  }
  if (h > SUNRISE - 0.3f && h < SUNSET + 0.3f) {
    float u = (h - (SUNRISE - 0.3f)) / (SUNSET - SUNRISE + 0.6f);
    int x = 20 + u * 260, y = 105 - sinf(u * PI) * 85;
    s.fillCircle(x, y, 10, rgb({255, 220, 90}));
  } else {
    float hn = h < SUNRISE ? h + 24 : h;
    float u = (hn - (SUNSET + 0.3f)) / (24 + SUNRISE - SUNSET - 0.6f);
    int x = 20 + u * 260, y = 105 - sinf(u * PI) * 85;
    s.fillCircle(x, y, 8, rgb({230, 230, 210}));
    s.fillCircle(x + 4, y - 2, 7, rgb(sky, 0.7f));  // crescent
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

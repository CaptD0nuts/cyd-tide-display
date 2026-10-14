#pragma once
#include <TFT_eSPI.h>
#include "tide.h"

void sceneBegin(TFT_eSPI &tft);
// Station position, for real sunrise/sunset. Until it's set the sky uses fixed times.
void sceneSetLocation(float lat, float lon);
// Measured wave height (ft) and period (s) driving the swell; NAN = no
// current reading, which draws a default 1 ft / 5 s swell and says so.
void sceneSetSurf(float heightFt, float periodS);
// Measured water temperature (F); NAN = no current reading, not shown.
void sceneSetWater(float waterF);
// Measured wind (mph, degrees true it blows from); speed NAN = not shown,
// gust or direction NAN = left out.
void sceneSetWind(float mph, float gustMph, float dirDeg);
// One animation frame of the beach (top 150 px). t = the time being shown,
// lapse = time-lapse replay running, stale = last refresh failed.
void sceneDraw(const TideData &d, double t, bool lapse, bool stale, uint32_t ms);
// Tide curve from t-12h to t+24h with the highs/lows marked.
void chartDraw(const TideData &d, double t);
// Next high/low lines along the bottom; only repaints when the text changes.
void infoDraw(const TideData &d, double t, const char *station);
// Full-screen message (setup, errors).
void sceneMessage(const char *line1, const char *line2, const char *line3 = "");

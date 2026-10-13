#pragma once
#include <Arduino.h>
#include <time.h>

#define MAX_SAMPLES  120
#define MAX_EXTREMES 16

struct Extreme {
  time_t t;
  float v;     // feet above MLLW
  bool high;
};

struct TideData {
  time_t t0;               // UTC epoch of v[0]; samples are one hour apart
  int n;
  float v[MAX_SAMPLES];    // feet above MLLW
  int nx;
  Extreme x[MAX_EXTREMES]; // NOAA's predicted highs and lows, in time order
  float minV, maxV;
  time_t fetchedAt;
};

// Fetches hourly predictions plus highs/lows. d is only changed on success.
bool tideFetch(const char *station, TideData &d);
// Station name and position from NOAA's metadata API.
bool tideStationInfo(const char *station, char *name, size_t n, float &lat, float &lon);
bool tideCovers(const TideData &d, double t);
// Smooth level at time t (Catmull-Rom through the hourly samples).
float tideLevel(const TideData &d, double t);
// First high/low after t, skipping `skip` of them; nullptr if none.
const Extreme *tideNext(const TideData &d, double t, int skip = 0);

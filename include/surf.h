#pragma once
#include <Arduino.h>
#include <time.h>

struct Surf {
  float heightFt;  // significant wave height
  float periodS;   // dominant period, or average period when the buoy has no dominant one
  time_t obsTime;  // UTC time of the buoy reading
};

// Latest wave reading from an NDBC buoy's realtime feed. s is only changed
// on success.
bool surfFetch(const char *buoy, Surf &s);

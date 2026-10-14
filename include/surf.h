#pragma once
#include <Arduino.h>
#include <time.h>

struct Surf {
  float heightFt;  // significant wave height
  float periodS;   // dominant period, or average period when the buoy has no dominant one
  time_t obsTime;  // UTC time of the wave reading, 0 = none yet
  float waterF;    // sea surface temperature
  time_t waterTime; // UTC time of the temperature reading, 0 = none yet
};

// Latest wave and water temperature readings from an NDBC buoy's realtime
// feed. Each part of s is only changed when a reading for it was found;
// returns true if either was.
bool surfFetch(const char *buoy, Surf &s);

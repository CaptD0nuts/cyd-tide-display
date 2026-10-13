#pragma once

// NOAA asks callers to identify themselves with an application name.
#define USER_AGENT "cyd-tide-display (github.com/CaptD0nuts)"
#define NOAA_APP   "cyd-tide-display"

// Alabama Point, Perdido Pass (Orange Beach, AL). Any NOAA tide prediction
// station id works; change it on the setup page.
#define DEFAULT_STATION "8730667"
// POSIX TZ string for the clock and the time labels. US Central with DST.
#define DEFAULT_TZ "CST6CDT,M3.2.0,M11.1.0"

#define FETCH_HOURS    96          // hourly predictions pulled, starting at 00:00 UTC yesterday
#define REFRESH_S      (6 * 3600)  // refetch this often
#define RETRY_MS       120000      // after a failed fetch
#define FRAME_MS       33

// Tap the screen: replay the next LAPSE_HOURS in LAPSE_MS.
#define LAPSE_HOURS 24
#define LAPSE_MS    24000

#define SETUP_AP_NAME  "TIDE-SETUP"
#define PORTAL_HOLD_MS 3000        // hold the screen this long while powering on to reopen setup

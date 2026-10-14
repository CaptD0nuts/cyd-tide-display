// Measured waves and water temperature from an NDBC buoy. The realtime2 file
// holds 45 days of 10-minute rows, newest first, but wave columns are only
// filled every 30-60 minutes, so read rows until both have turned up and hang
// up.
#include "surf.h"
#include "config.h"
#include "tide.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define NDBC_REALTIME "https://www.ndbc.noaa.gov/data/realtime2/"
#define MAX_ROWS 30  // about five hours of rows

bool surfFetch(const char *buoy, Surf &s) {
  char url[96];
  snprintf(url, sizeof url, NDBC_REALTIME "%s.txt", buoy);
  // Public read-only data, so the certificate isn't checked.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  http.setUserAgent(USER_AGENT);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("ndbc http %d\n", code);
    http.end();
    return false;
  }

  Stream &body = http.getStream();
  bool gotWave = false, gotTemp = false;
  for (int rows = 0; rows < MAX_ROWS && !(gotWave && gotTemp);) {
    String line = body.readStringUntil('\n');
    if (line.length() == 0) break;  // end of data or timeout
    if (line[0] == '#') continue;   // header lines
    ++rows;
    // #YY MM DD hh mm WDIR WSPD GST WVHT DPD APD MWD PRES ATMP WTMP ...
    int y, mo, d, h, mi;
    char wdir[8], wspd[8], gst[8], wvht[8], dpd[8], apd[8], mwd[8], pres[8], atmp[8], wtmp[8];
    if (sscanf(line.c_str(), "%d %d %d %d %d %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s", &y, &mo, &d, &h, &mi, wdir,
               wspd, gst, wvht, dpd, apd, mwd, pres, atmp, wtmp) != 15)
      continue;
    char stamp[20];
    snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi);
    if (!gotWave && strcmp(wvht, "MM")) {
      s.heightFt = atof(wvht) * 3.28084f;
      s.periodS = strcmp(dpd, "MM") ? atof(dpd) : strcmp(apd, "MM") ? atof(apd) : NAN;
      s.obsTime = parseUtc(stamp);
      gotWave = true;
    }
    if (!gotTemp && strcmp(wtmp, "MM")) {
      s.waterF = atof(wtmp) * 9 / 5 + 32;
      s.waterTime = parseUtc(stamp);
      gotTemp = true;
    }
  }
  http.end();  // hang up on the rest of the file
  if (gotWave) Serial.printf("ndbc %s: waves %.1f ft, period %.1f s\n", buoy, s.heightFt, s.periodS);
  else Serial.printf("ndbc %s: no wave reading in the last %d rows\n", buoy, MAX_ROWS);
  if (gotTemp) Serial.printf("ndbc %s: water %.1f F\n", buoy, s.waterF);
  else Serial.printf("ndbc %s: no water temperature in the last %d rows\n", buoy, MAX_ROWS);
  return gotWave || gotTemp;
}

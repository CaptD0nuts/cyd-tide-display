// NOAA CO-OPS tide predictions: hourly levels for the animation and the
// official high/low times for the labels. All times are fetched in UTC.
#include "tide.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

#define NOAA_DATA "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter"
#define NOAA_META "https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations/"

// newlib has no timegm.
time_t parseUtc(const char *s) {
  int y, mo, d, h, mi;
  if (!s || sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) return 0;
  y -= mo <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + doe - 719468;
  return (time_t)days * 86400 + h * 3600 + mi * 60;
}

static bool getJson(const char *url, JsonDocument &doc, JsonDocument &filter) {
  // Public read-only data, so the certificate isn't checked.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be parsed as a stream
  http.setTimeout(15000);
  http.setUserAgent(USER_AGENT);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("noaa http %d\n", code);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("noaa json: %s\n", err.c_str());
    return false;
  }
  return true;
}

static bool fetchPredictions(const char *station, const char *date, const char *interval, JsonDocument &doc) {
  char url[288];
  snprintf(url, sizeof url,
           NOAA_DATA "?product=predictions&application=" NOAA_APP
                     "&station=%s&begin_date=%s&range=%d&datum=MLLW&time_zone=gmt&units=english&interval=%s&format=json",
           station, date, FETCH_HOURS, interval);
  JsonDocument filter;
  JsonObject f = filter["predictions"].add<JsonObject>();
  f["t"] = true;
  f["v"] = true;
  f["type"] = true;
  if (!getJson(url, doc, filter)) return false;
  if (!doc["predictions"].is<JsonArray>()) {
    Serial.println("noaa: no predictions in reply (bad station id?)");
    return false;
  }
  return true;
}

bool tideFetch(const char *station, TideData &d) {
  time_t start = time(nullptr) - 24 * 3600;
  struct tm g;
  gmtime_r(&start, &g);
  char date[9];
  strftime(date, sizeof date, "%Y%m%d", &g);

  static TideData w;  // scratch, copied over d only when both requests worked
  w = {};
  {
    JsonDocument doc;
    if (!fetchPredictions(station, date, "h", doc)) return false;
    for (JsonObject p : doc["predictions"].as<JsonArray>()) {
      if (w.n >= MAX_SAMPLES) break;
      time_t t = parseUtc(p["t"]);
      if (w.n == 0) w.t0 = t;
      if (t != w.t0 + (time_t)w.n * 3600) {
        Serial.println("noaa: hourly samples not evenly spaced");
        return false;
      }
      w.v[w.n++] = atof(p["v"] | "0");
    }
  }
  if (w.n < 48) {
    Serial.printf("noaa: only %d hourly samples\n", w.n);
    return false;
  }
  {
    JsonDocument doc;
    if (!fetchPredictions(station, date, "hilo", doc)) return false;
    for (JsonObject p : doc["predictions"].as<JsonArray>()) {
      if (w.nx >= MAX_EXTREMES) break;
      const char *type = p["type"] | "";
      w.x[w.nx++] = {parseUtc(p["t"]), (float)atof(p["v"] | "0"), type[0] == 'H'};
    }
  }
  w.minV = w.maxV = w.v[0];
  for (int i = 1; i < w.n; ++i) {
    w.minV = fminf(w.minV, w.v[i]);
    w.maxV = fmaxf(w.maxV, w.v[i]);
  }
  w.fetchedAt = time(nullptr);
  d = w;
  Serial.printf("noaa: %d hourly, %d highs/lows, range %.2f..%.2f ft\n", d.n, d.nx, d.minV, d.maxV);
  return true;
}

bool tideStationInfo(const char *station, char *name, size_t n, float &lat, float &lon) {
  char url[128];
  snprintf(url, sizeof url, NOAA_META "%s.json", station);
  JsonDocument filter;
  filter["stations"][0]["name"] = true;
  filter["stations"][0]["lat"] = true;
  filter["stations"][0]["lng"] = true;
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  JsonObject s = doc["stations"][0];
  if (!s["name"].is<const char *>() || !s["lat"].is<float>() || !s["lng"].is<float>()) return false;
  strlcpy(name, s["name"], n);
  lat = s["lat"];
  lon = s["lng"];
  return true;
}

bool tideCovers(const TideData &d, double t) {
  return d.n > 1 && t >= d.t0 && t <= d.t0 + (double)(d.n - 1) * 3600;
}

float tideLevel(const TideData &d, double t) {
  if (d.n == 0) return 0;
  double f = (t - d.t0) / 3600.0;
  int i = (int)floor(f);
  if (i < 0) return d.v[0];
  if (i >= d.n - 1) return d.v[d.n - 1];
  float u = f - i;
  float p0 = d.v[max(i - 1, 0)], p1 = d.v[i], p2 = d.v[i + 1], p3 = d.v[min(i + 2, d.n - 1)];
  return 0.5f * (2 * p1 + (p2 - p0) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u +
                 (3 * p1 - p0 - 3 * p2 + p3) * u * u * u);
}

const Extreme *tideNext(const TideData &d, double t, int skip) {
  for (int i = 0; i < d.nx; ++i)
    if (d.x[i].t > t && skip-- == 0) return &d.x[i];
  return nullptr;
}

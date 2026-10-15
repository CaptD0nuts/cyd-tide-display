# Tide Display CYD

An animated tide display for the **Cheap Yellow Display** (ESP32-2432S028R, 2.8" 320x240 touch). It pulls NOAA tide predictions and live readings from an NDBC buoy over WiFi, then draws a beach scene whose water line rises and falls with the real tide. No server or other hardware needed.

- **Beach scene (top):** a cross-section of beach with an umbrella and a tide staff marked every half foot. The water line follows the tide, and swells roll in at the buoy's measured wave height and period, with swash running up the sand.
- **Real sky:** the sun and moon sit at their actual positions (east on the left, west on the right) and touch the horizon at their real rise and set times. The moon is lit to match its phase. The sky color moves through the actual twilight stages, and stars come out as it darkens.
- **Readouts:** the current tide height in feet; whether it's rising, falling or slack (with a ft/hr rate and an arrow); waves (height and period); wind (direction, speed and gusts); water temperature; and the clock.
- **Tide chart (middle):** the curve from 12 hours back to 24 hours ahead, with midnight lines, day names, and the predicted highs and lows marked.
- **Bottom line:** the next high and low tides, and the station name.
- **Time-lapse:** tap the screen to replay the next 24 hours in 24 seconds. Tap again to stop.

## Data sources

- **Tides:** [NOAA CO-OPS](https://tidesandcurrents.noaa.gov) hourly predictions plus its official high/low times. The default station is `8730667` (Alabama Point, Orange Beach, AL). The station's position from NOAA's metadata API drives the sun and moon.
- **Waves, water temperature, wind:** an [NDBC](https://www.ndbc.noaa.gov) buoy's realtime feed. The default buoy is `42012` (Orange Beach, about 14 nm offshore). Readings older than 3 hours aren't shown; the swell falls back to a default 1 ft / 5 s.
- Tides refresh every 6 hours (retrying after 2 minutes on failure), the buoy every 30 minutes. The clock comes from NTP.

## Hardware

Any ESP32-2432S028R "Cheap Yellow Display" (the version with the ILI9341 screen and XPT2046 resistive touch). Plain ESP32, 4 MB flash. The ESP32 only does 2.4 GHz WiFi.

## Build and flash

Uses [PlatformIO](https://platformio.org).

```sh
pio run -t upload --upload-port COM4    # your serial port
```

## Setup

On first boot (or when its saved WiFi fails), the board opens a hotspot called `TIDE-SETUP`. Join it from your phone, open `http://192.168.4.1` (turn mobile data off if the page won't load), choose **Configure WiFi**, pick your network, and fill in these fields on the same page:

| Field | Default | Notes |
|---|---|---|
| NOAA tide station id | `8730667` | Any NOAA tide prediction station |
| NDBC buoy id | `42012` | Any NDBC station; upper-cased automatically |
| POSIX time zone | `CST6CDT,M3.2.0,M11.1.0` | US Central with daylight saving |

To reopen the setup page later, hold a finger on the screen while plugging the board in, and keep holding for 3 seconds.

## Related

Two other projects run on the same kind of board:

- [Sea Tracker CYD](https://github.com/CaptD0nuts/cyd-sea-tracker) shows sharks, sea turtles and other animals tagged by [OCEARCH](https://www.ocearch.org/tracker/) on a sonar scope, with details for the nearest one.
- [Flight Tracker CYD](https://github.com/CaptD0nuts/Flight-Tracker-CYD) shows nearby aircraft from the [adsb.lol](https://adsb.lol) feed on a radar scope, with the flight, route, airline, altitude and speed of the nearest one.

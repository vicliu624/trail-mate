# Trail Mate uConsole Linux

This app shell is the desktop-class Linux handheld target for ClockworkPi
uConsole-style devices with an AIO2 capability module.

It intentionally does not launch the compact Cardputer Zero app grid. The
default shell is a GTK desktop window backed by Linux app services and
UI-independent presentation models. The older LVGL path remains available
through explicit SDL or raw Linux framebuffer fallbacks, but it is no longer the
primary uConsole interface.

- compact menu bar for primary workspaces
- bottom status bar for AIO2/LoRa/GPS/storage state
- central operational workspace
- Overview workspace with location/map context, hardware state, message status,
  team activity timeline, and runtime details
- Hardware and Data workspaces are clickable runtime inspection surfaces;
  Settings is a writable GTK control plane for identity, radio, GPS, map,
  chat policy, network, and privacy configuration
- SQLite-backed Linux local state
- online XYZ map tile fetch with local file cache

The uConsole UI consumes Linux app services through `LinuxAppServices` and
UI-independent presentation models. GTK is the production Linux UI direction;
LVGL is retained for bring-up/fallback. AIO2 support belongs below
platform/runtime adapters and must be reported through honest capability state;
it should not become a UI layout concept. Overview panels must show real stored
or runtime state only; empty hardware, message, location, and team states should
remain explicit instead of being filled with demo data. Linux does not expose
BLE as a product capability.

## Build

Install Linux build dependencies first:

```sh
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libgtk-4-dev libsqlite3-dev libcurl4-openssl-dev gdal-bin unzip \
  ca-certificates
```

```sh
cmake --preset linux-uconsole-debug
cmake --build --preset linux-uconsole-debug-build
```

## Package

Build the standard Debian package with:

```sh
cmake --preset linux-uconsole-release
cmake --build --preset linux-uconsole-deb
```

The release preset stamps the package as `0.1.25~alpha`. Override it for another
release with:

```sh
cmake --preset linux-uconsole-release -DTRAIL_MATE_UCONSOLE_PACKAGE_VERSION=0.1.25~alpha
```

The package installs the app as:

```text
/usr/bin/trailmate-uconsole
/usr/share/applications/trailmate-uconsole.desktop
/usr/share/pixmaps/trailmate-uconsole.png
/usr/share/icons/hicolor/256x256/apps/trailmate-uconsole.png
```

## Run

```sh
trailmate-uconsole
```

The default GTK desktop window is `1180x600`, leaving room for the window
decorations and desktop panel on the uConsole `1280x720` landscape session.
Override it with:

```sh
TRAIL_MATE_UCONSOLE_WIDTH=960 TRAIL_MATE_UCONSOLE_HEIGHT=540 trailmate-uconsole
```

Local state is stored in SQLite:

```text
$TRAIL_MATE_SETTINGS_ROOT/trailmate.sqlite3
```

If `TRAIL_MATE_SETTINGS_ROOT` is not set, the default root is
`$HOME/.trailmate_cardputer_zero`.

The GTK map page uses real GPS/NMEA input when available. Configure a serial GPS
or an NMEA file with:

```sh
TRAIL_MATE_GPS_DEVICE=/dev/ttyUSB0 trailmate-uconsole
TRAIL_MATE_GPS_NMEA_FILE=/path/to/feed.nmea trailmate-uconsole
```

On uConsole, the GTK shell also auto-detects the ClockworkPI uConsole CDC ACM
serial endpoint under `/dev/serial/by-id/usb-ClockworkPI_uConsole_*` and treats
it as the default GPS/NMEA candidate. If that endpoint exists but no valid NMEA
fix has arrived yet, the UI reports the endpoint instead of claiming that GPS is
missing.

For bench testing without a GPS receiver, provide an explicit map center:

```sh
TRAIL_MATE_MAP_LAT=31.2304 TRAIL_MATE_MAP_LNG=121.4737 trailmate-uconsole
```

Downloaded base tiles are cached in the same XYZ layout used by the SD-card map
runtime. The GTK map renders those tiles as one continuous landscape map surface;
individual tile cache entries are not exposed as bordered UI cards:

```text
$TRAIL_MATE_SD_ROOT/maps/base/osm/{z}/{x}/{y}.png
$TRAIL_MATE_SD_ROOT/maps/base/terrain/{z}/{x}/{y}.png
$TRAIL_MATE_SD_ROOT/maps/base/satellite/{z}/{x}/{y}.jpg
```

If `TRAIL_MATE_SD_ROOT` is not set, it defaults to
`$TRAIL_MATE_SETTINGS_ROOT/sdcard`.

Contour overlays are transparent PNG tiles above the active base map. Enable
or hide them from the map toolbar or Settings -> Map -> Contour overlay. The
map toolbar also has **Fill visible**, which fills the current visible viewport
by querying NASA CMR for `NASADEM_HGT`, downloading missing DEM archives with
the stored Earthdata token, extracting HGT/TIF files, and generating contour PNG
tiles through GDAL. Earthdata tokens are stored in the same SQLite settings
database as Linux local state. Generated contour tiles are written to:

```text
$TRAIL_MATE_SD_ROOT/maps/contour/{major|minor}-{interval}/{z}/{x}/{y}.png
```

For compatibility with Trail Mate Center's working cache, the GTK map also
checks:

```text
$TRAIL_MATE_SD_ROOT/contours/tiles/{major|minor}-{interval}/{z}/{x}/{y}.png
```

DEM source files and temporary contour work files are kept under:

```text
$TRAIL_MATE_SD_ROOT/maps/dem
$TRAIL_MATE_CACHE_ROOT/contour-work
```

To run the LVGL/SDL fallback window explicitly:

```sh
trailmate-uconsole --sdl
```

For raw framebuffer bring-up without the desktop session:

```sh
sudo trailmate-uconsole --fbdev /dev/fb0
```

The raw framebuffer path defaults to the physical `/dev/fb0` geometry currently
reported as `720x1280`; the desktop session rotates that panel into a `1280x720`
workspace.

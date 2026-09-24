# Search boundaries

These static datasets ship with the page. Browsing does not call a geocoder or
download geometry from the original provider. They support geographic discovery;
generalized coastlines can omit small islands or points close to the shore.
The current-map search remains available for such locations.

## Countries and regions

`countries.json` derives from Natural Earth 1:50m Admin 0 Countries, revision
`ca96624a56bd078437bca8184e78163e5039ad19`. Natural Earth data is public domain.
Coordinates are rounded to five decimal places; source geometry and names are
otherwise preserved. The GeoJSON stores the source URL and SHA-256.
Regenerate with `python tools/geocaching/build_country_boundaries.py`.

- Source: https://www.naturalearthdata.com/downloads/50m-cultural-vectors/50m-admin-0-countries-2/
- Terms: https://www.naturalearthdata.com/about/terms-of-use/

## Oceans and seas

`seas.json` derives from Flanders Marine Institute (2018), IHO Sea Areas,
version 3, https://doi.org/10.14284/323. Licensed under CC BY 4.0:
https://creativecommons.org/licenses/by/4.0/ . Provider:
https://www.marineregions.org/ . It includes the provider's Southern Ocean
extension to the IHO 1953 publication. These are geographic water bodies, not
territorial-sea, EEZ, high-seas or navigational determinations.

The shipped geometry uses Alvin Bryan's web conversion at commit
`721a4b82c8db3fc5c01deb340100faa962ef809d`:
https://github.com/alvinometric/oceans-seas.geojson . Its published Mapshaper
recipe retains 0.5% of vertices. All 101 sea names were matched against the
official Marine Regions WFS catalog to restore the MRGIDs and verify coverage.
The conversion's MIT notice is retained in `LICENSE-oceans-seas.txt`; the source
data remains CC BY 4.0. Further changes here: five-decimal coordinate rounding,
selected Chinese display names, reduced attributes. The source hash is recorded
in the GeoJSON. Coastlines are approximate, not suitable for navigation.

Rebuild with Shapely 2.1.2 and `python tools/geocaching/build_sea_boundaries.py
oceans-seas.geo.json iho-names.json`. Obtain the second file from the official
WFS URL above with `propertyName=name,mrgid`. The script also supports processing
the full official export (SHAPE-ZIP needs pyshp 2.3.1); that alternative applies
0.01-degree topology-preserving simplification instead.

## Query behavior

The directory receives the selected region's bounding box. The worker applies
point-in-polygon filtering before the 500-result display limit. A filtered-out
page retains its continuation cursor. Up to five empty filtered pages are
advanced per action before yielding; Load more continues from there. Counts are
loaded matches from reachable directories, not global or exhaustive totals.

Browser location is optional and uses low-accuracy mode with a ten-second timeout.
The center is rounded to 0.01 degrees before an approximately 50 km-wide initial
viewport is queried. The directory sees this search rectangle, not the raw browser
position. Denied/unavailable location leaves country, sea and map searches usable.

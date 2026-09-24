"""Convert a Marine Regions IHO Sea Areas WFS export to web search geometry.

Requires shapely==2.1.2 and pyshp==2.3.1. Pass a GeoJSON or SHAPE-ZIP export.
The source and output hashes are retained for provenance; no network at runtime.
"""
import hashlib
import json
from pathlib import Path
import sys
from shapely.geometry import shape, mapping

SOURCE = "https://geo.vliz.be/geoserver/MarineRegions/wfs?service=WFS&version=1.0.0&request=GetFeature&typeName=iho&outputFormat=application/json"
WEB_SOURCE = "https://github.com/alvinometric/oceans-seas.geojson/blob/721a4b82c8db3fc5c01deb340100faa962ef809d/oceans-seas.geo.json"
ZH = {"North Pacific Ocean":"北太平洋", "South Pacific Ocean":"南太平洋",
      "North Atlantic Ocean":"北大西洋", "South Atlantic Ocean":"南大西洋",
      "Indian Ocean":"印度洋", "Arctic Ocean":"北冰洋", "Southern Ocean":"南大洋",
      "Mediterranean Sea":"地中海", "Mediterranean Sea - Western Basin":"地中海西部",
      "Mediterranean Sea - Eastern Basin":"地中海东部", "South China Sea":"南海",
      "East China Sea":"东海", "Eastern China Sea":"东海", "Yellow Sea":"黄海", "Sea of Japan":"日本海", "Japan Sea":"日本海",
      "Philippine Sea":"菲律宾海", "Caribbean Sea":"加勒比海", "North Sea":"北海",
      "Baltic Sea":"波罗的海", "Black Sea":"黑海", "Red Sea":"红海",
      "Arabian Sea":"阿拉伯海", "Bay of Bengal":"孟加拉湾", "Bering Sea":"白令海",
      "Coral Sea":"珊瑚海", "Tasman Sea":"塔斯曼海", "Gulf of Mexico":"墨西哥湾"}


def rounded(value):
    return [rounded(item) for item in value] if isinstance(value, (list, tuple)) else round(value, 5)


def main():
    path = Path(sys.argv[1])
    with path.open("rb") as stream:
        source_hash = hashlib.file_digest(stream,"sha256").hexdigest()
    if path.suffix == ".zip":
        import shapefile
        reader = shapefile.Reader(str(path))
        items = ({"properties": item.record.as_dict(), "geometry": item.shape.__geo_interface__}
                 for item in reader.iterShapeRecords())
    else:
        items = json.loads(path.read_bytes())["features"]
    # Optional official name/MRGID export accompanies a pinned web conversion.
    names = None
    if len(sys.argv)>2:
        names = {item["properties"]["name"]:item["properties"]["mrgid"]
                 for item in json.loads(Path(sys.argv[2]).read_bytes())["features"]}
    features = []
    for item in items:
        props = {key.lower():value for key,value in item["properties"].items()}
        geometry = shape(item["geometry"])
        if names is None:
            geometry = geometry.simplify(0.01, preserve_topology=True)
        if geometry.is_empty or geometry.geom_type not in ("Polygon", "MultiPolygon"):
            raise ValueError(f"Invalid sea geometry: {props['name']}")
        coordinates = mapping(geometry)["coordinates"]
        name = props["name"]
        mrgid = names[name] if names is not None else props["mrgid"]
        features.append({"type":"Feature", "id":f"sea-{mrgid}",
                         "properties":{"name":name, "zh":ZH.get(name,name), "mrgid":mrgid,
                                       "kind":"sea"},
                         "geometry":{"type":geometry.geom_type, "coordinates":rounded(coordinates)}})
    output = Path(__file__).resolve().parents[2] / "site/geocaching/data/seas.json"
    if names is not None and set(names)!={item["properties"]["name"] for item in features}:
        raise ValueError("Converted dataset does not cover the official sea catalog")
    output.write_text(json.dumps({"type":"FeatureCollection", "source":WEB_SOURCE if names is not None else SOURCE.replace("application/json","SHAPE-ZIP") if path.suffix==".zip" else SOURCE,
                                 "sourceSha256":source_hash,
                                 "license":"CC-BY-4.0", "simplification":"mapshaper 0.5% retained vertices" if names is not None else "0.01 degree topology preserving",
                                 "features":features},ensure_ascii=False,separators=(",",":")),encoding="utf-8")
    print(f"Wrote {len(features)} sea areas, {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()

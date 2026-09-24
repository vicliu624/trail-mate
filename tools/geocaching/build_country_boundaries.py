"""Reproduce the vendored Natural Earth 1:50m country search geometry."""
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

REVISION = "ca96624a56bd078437bca8184e78163e5039ad19"
URL = f"https://raw.githubusercontent.com/nvkelso/natural-earth-vector/{REVISION}/geojson/ne_50m_admin_0_countries.geojson"
ROOT = Path(__file__).resolve().parents[2]


def rounded(value):
    return [rounded(item) for item in value] if isinstance(value, list) else round(value, 5)


def main():
    raw = urlopen(URL, timeout=60).read()
    source = json.loads(raw)
    features = []
    for feature in source["features"]:
        properties = feature["properties"]
        features.append({"type": "Feature", "id": properties["ADM0_A3"],
                         "properties": {"name": properties["NAME_EN"], "zh": properties["NAME_ZH"],
                                        "iso2": properties["ISO_A2_EH"]},
                         "geometry": {"type": feature["geometry"]["type"],
                                      "coordinates": rounded(feature["geometry"]["coordinates"])}})
    output = ROOT / "site/geocaching/data/countries.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"type": "FeatureCollection", "source": URL,
                                 "sourceSha256": hashlib.sha256(raw).hexdigest(),
                                 "features": features}, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"Wrote {len(features)} country/region boundaries, {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()

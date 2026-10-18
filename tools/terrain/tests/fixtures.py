"""Small synthetic datasets for the pipeline tests: a DEM tile in the Copernicus layout and a Natural Earth-style
land zip. Nothing here touches the network; tests put the files straight into the download cache."""

from __future__ import annotations

import json
import shutil
import tempfile
import zipfile
from pathlib import Path

import numpy as np
import rasterio
import shapefile
from rasterio.transform import from_origin

from terrain.dem import COPDEM_DATASET, copdem_tile_name
from terrain.landmask import LAND_FILE, NATURAL_EARTH_DATASET

# A map that fits inside the single tile N30 E030 (30..31 N, 30..31 E), margin included.
BOUNDS = {"min_lon": 30.2, "min_lat": 30.2, "max_lon": 30.6, "max_lat": 30.5}
# A detail region in the middle of the map.
DETAIL_RING = [[30.3, 30.3], [30.5, 30.3], [30.5, 30.4], [30.3, 30.4], [30.3, 30.3]]
SEA_EDGE_LON = 30.3  # west of this longitude the synthetic land polygon ends: it is sea there


def elevation_at(lon, lat):
    """The synthetic ground: a plane that rises to the east and the north, plus a round hill."""
    lon = np.asarray(lon, dtype=np.float64)
    lat = np.asarray(lat, dtype=np.float64)
    hill = 120.0 * np.exp(-(((lon - 30.45) / 0.05) ** 2 + ((lat - 30.35) / 0.04) ** 2))
    return (100.0 + 300.0 * (lon - 30.0) + 200.0 * (lat - 30.0) + hill).astype(np.float32)


def write_dem_tile(path: Path, lat: int = 30, lon: int = 30, pixels: int = 720) -> Path:
    """A 1 x 1 degree GeoTIFF with the synthetic ground, like a GLO-30 tile (fewer pixels)."""
    step = 1.0 / pixels
    lons = lon + (np.arange(pixels) + 0.5) * step
    lats = lat + 1.0 - (np.arange(pixels) + 0.5) * step
    data = elevation_at(*np.meshgrid(lons, lats))
    path.parent.mkdir(parents=True, exist_ok=True)
    with rasterio.open(path, "w", driver="GTiff", width=pixels, height=pixels, count=1, dtype="float32",
                       crs="EPSG:4326", transform=from_origin(lon, lat + 1.0, step, step)) as dst:
        dst.write(data, 1)
    return path


def write_land_zip(path: Path, west_lon: float = SEA_EDGE_LON, east_lon: float = 31.5) -> Path:
    """A shapefile in a zip with one land polygon: everything between ``west_lon`` and ``east_lon`` (a coast
    running north-south). The ring is clockwise, as shapefile exteriors are."""
    path.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="land_"))
    try:
        base = work / "ne_10m_land"
        writer = shapefile.Writer(str(base))
        writer.field("name", "C")
        writer.poly([[[west_lon, 29.0], [west_lon, 31.5], [east_lon, 31.5], [east_lon, 29.0], [west_lon, 29.0]]])
        writer.record("land")
        writer.close()
        with zipfile.ZipFile(path, "w") as archive:
            for suffix in (".shp", ".shx", ".dbf"):
                archive.write(base.with_suffix(suffix), f"ne_10m_land{suffix}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return path


def seed_cache(cache_root: Path, *, tile=(30, 30), land: bool = True) -> None:
    """Puts the synthetic DEM tile and land zip where the pipeline's download cache looks for them."""
    write_dem_tile(cache_root / COPDEM_DATASET / f"{copdem_tile_name(*tile)}.tif", *tile)
    if land:
        write_land_zip(cache_root / NATURAL_EARTH_DATASET / LAND_FILE)


def write_map_json(folder: Path, **extra) -> Path:
    map_file = folder / "map.json"
    body = {"map_id": "test:map.synthetic", "package": "synthetic", "bounds": BOUNDS,
            "detail_regions": [DETAIL_RING], **extra}
    map_file.write_text(json.dumps(body), encoding="utf-8")
    return map_file

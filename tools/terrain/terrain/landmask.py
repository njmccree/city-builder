"""The land/sea mask, from the Natural Earth 10 m land polygons.

``ne_10m_land.zip`` (public domain, https://www.naturalearthdata.com/downloads/10m-physical-vectors/10m-land/) holds
one shapefile of land polygons. They are read with pyshp, cut to the map's lon/lat bounds, projected into the
map's world frame and rasterized onto any grid: a cell is land if its center lies in a land polygon.
Coordinates and arrays follow the pipeline's convention: ``[row, column]``, row 0 at the south.
"""

from __future__ import annotations

import zipfile
from pathlib import Path

import numpy as np
import shapefile
import shapely
from rasterio.features import rasterize
from rasterio.transform import Affine
from shapely.geometry import box, shape

from .cache import Source
from .projection import WorldFrame

NATURAL_EARTH_DATASET = "natural-earth"
LAND_FILE = "ne_10m_land.zip"
LAND_URL = "https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip"
# Edges of the polygons are straight in lon/lat but bend once projected: add vertices so they follow.
_SEGMENT_DEG = 0.02


class LandMaskError(RuntimeError):
    """The land polygons cannot be read."""


def land_source(pins: dict[str, dict] | None = None) -> Source:
    pin = (pins or {}).get(LAND_FILE, {})
    return Source(NATURAL_EARTH_DATASET, LAND_FILE, LAND_URL, pin.get("size"), pin.get("sha256"))


def load_land_polygons(zip_path: Path, bounds: tuple[float, float, float, float], margin_deg: float = 0.25) -> list:
    """The land polygons that touch the bounds (plus a margin), cut to that box, as shapely geometries in lon/lat."""
    min_lon, min_lat, max_lon, max_lat = bounds
    window = box(min_lon - margin_deg, min_lat - margin_deg, max_lon + margin_deg, max_lat + margin_deg)
    polygons = []
    try:
        with shapefile.Reader(str(zip_path)) as reader:
            for item in reader.iterShapes(bbox=window.bounds):
                if item.shapeType == shapefile.NULL:
                    continue
                geometry = shape(item.__geo_interface__)
                if not geometry.is_valid:
                    geometry = geometry.buffer(0)
                clipped = geometry.intersection(window)
                if not clipped.is_empty:
                    polygons.append(shapely.segmentize(clipped, _SEGMENT_DEG))
    except (OSError, zipfile.BadZipFile, shapefile.ShapefileException) as error:
        raise LandMaskError(f"{zip_path}: cannot read the land polygons: {error}") from None
    return polygons


def project_geometry(geometry, transformer):
    """The shapely geometry with its lon/lat coordinates run through a pyproj transformer."""
    return shapely.transform(geometry, lambda coords: np.column_stack(transformer.transform(coords[:, 0], coords[:, 1])))


def rasterize_land(polygons: list, frame: WorldFrame, x0: float, z0: float, cols: int, rows: int,
                   cell_m: float) -> np.ndarray:
    """A bool ``[row, column]`` array: True where the center of the cell lies on land. The grid's south-west
    corner is world (x0, z0)."""
    if not polygons:
        return np.zeros((rows, cols), dtype=bool)
    to_projected = frame.lonlat_to_projected()
    projected = [project_geometry(polygon, to_projected) for polygon in polygons]
    transform = Affine(cell_m, 0.0, frame.origin_x + x0, 0.0, -cell_m, frame.origin_y + z0 + rows * cell_m)
    burned = rasterize([(geometry, 1) for geometry in projected], out_shape=(rows, cols), transform=transform,
                       fill=0, dtype="uint8", all_touched=False)
    return np.flipud(burned).astype(bool)

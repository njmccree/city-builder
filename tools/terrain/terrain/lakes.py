"""Lakes: the Natural Earth lake polygons, each with the water level the DEM shows.

Natural Earth (public domain) draws the lakes of the world as polygons at 1:10 million. A lake becomes an entry of
``lakes.json`` with its outline in world meters and a water level taken from the DEM (the Copernicus DEM flattens
water, so the median ground height inside the outline is the surface). Lakes that a historical correction replaces
with a valley (``fill_reservoir``: Lake Nasser, say) are skipped, and so are lakes smaller than ``min_area_km2``.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import shapefile
import shapely
from rasterio.features import rasterize
from rasterio.transform import Affine
from shapely.geometry import MultiPolygon, Polygon, box, shape

from .corrections import Correction
from .landmask import project_geometry
from .projection import WorldFrame
from .riverdem import DemSource
from .rivermodel import RiverError, RiverParams, slug

_MAX_CELLS = 4_000_000  # the most cells read to find one lake's level


def load_ne_lakes(zip_path: Path, bounds: tuple[float, float, float, float], margin_deg: float = 0.3) -> list[dict]:
    """The lake polygons touching the bounds plus a margin, as ``{"name", "geometry"}`` (shapely, lon/lat)."""
    min_lon, min_lat, max_lon, max_lat = bounds
    window = box(min_lon - margin_deg, min_lat - margin_deg, max_lon + margin_deg, max_lat + margin_deg)
    lakes = []
    try:
        with shapefile.Reader(str(zip_path)) as reader:
            for item in reader.iterShapeRecords(bbox=window.bounds):
                if item.shape.shapeType == shapefile.NULL:
                    continue
                geometry = shape(item.shape.__geo_interface__)
                if not geometry.is_valid:
                    geometry = geometry.buffer(0)
                clipped = geometry.intersection(window)
                if clipped.is_empty:
                    continue
                record = item.record.as_dict()
                name = str(record.get("name") or "").strip() or f"lake {record.get('ne_id', len(lakes))}"
                lakes.append({"name": name, "geometry": clipped})
    except (OSError, shapefile.ShapefileException) as error:
        raise RiverError(f"{zip_path}: cannot read the lake polygons: {error}") from None
    return lakes


def reservoir_polygons(corrections: list[Correction]) -> list:
    """The areas that ``fill_reservoir`` corrections turn back into valleys (even-odd over each one's rings)."""
    out = []
    for correction in corrections:
        if correction.op == "fill_reservoir" and correction.rings:
            area = Polygon(correction.rings[0])
            for hole in correction.rings[1:]:
                area = area.symmetric_difference(Polygon(hole))
            out.append(area.buffer(0))
    return out


def _level(geometry, dem: DemSource) -> float | None:
    min_x, min_z, max_x, max_z = geometry.bounds
    cell = max(90.0, math.sqrt(max((max_x - min_x) * (max_z - min_z), 1.0) / _MAX_CELLS))
    x0, z0 = math.floor(min_x / cell) * cell, math.floor(min_z / cell) * cell
    cols, rows = int(math.ceil((max_x - x0) / cell)), int(math.ceil((max_z - z0) / cell))
    ground = dem.window(x0, z0, max(cols, 1), max(rows, 1), cell)
    transform = Affine(cell, 0.0, x0, 0.0, -cell, z0 + rows * cell)
    mask = np.flipud(rasterize([(geometry, 1)], out_shape=(rows, cols), transform=transform, fill=0,
                               dtype="uint8", all_touched=True)).astype(bool)
    values = ground[mask & ~np.isnan(ground)]
    return float(np.median(values)) if values.size else None


def build_lakes(features: list[dict], frame: WorldFrame, dem: DemSource, params: RiverParams,
                reservoirs: list) -> tuple[list[dict], list[str]]:
    """(the ``lakes`` array of lakes.json, notes about lakes left out)."""
    to_projected = frame.lonlat_to_projected()
    shift = np.array([frame.origin_x, frame.origin_y])
    documents: list[dict] = []
    notes: list[str] = []
    used: set[str] = set()
    world_box = box(0.0, 0.0, frame.width_m, frame.height_m)
    for feature in features:
        world = shapely.transform(project_geometry(shapely.segmentize(feature["geometry"], 0.02), to_projected),
                                  lambda c: c - shift)
        world = world.intersection(world_box)
        for polygon in shapely.get_parts(world):
            if not isinstance(polygon, Polygon) or polygon.is_empty:
                continue
            area_km2 = polygon.area / 1e6
            if area_km2 < params.lakes_min_area_km2:
                continue
            if any(polygon.intersection(r).area > 0.3 * polygon.area for r in reservoirs):
                notes.append(f"{feature['name']}: skipped, a correction replaces this reservoir with a valley")
                continue
            level = _level(polygon, dem)
            if level is None:
                notes.append(f"{feature['name']}: skipped, no DEM data under it")
                continue
            simple = polygon.simplify(params.lakes_simplify_m, preserve_topology=True)
            if simple.is_empty or simple.geom_type != "Polygon":
                continue
            base = slug(feature["name"])
            lake_id, n = base, 2
            while lake_id in used:
                lake_id, n = f"{base}_{n}", n + 1
            used.add(lake_id)

            def ring(r) -> list:
                return [[round(float(x), 1), round(float(z), 1)] for x, z in shapely.get_coordinates(r)]

            documents.append({"id": lake_id, "name": feature["name"], "water_level_m": round(level, 2),
                              "area_km2": round(area_km2, 2),
                              "rings": [ring(simple.exterior), *[ring(i) for i in simple.interiors]]})
    return documents, notes

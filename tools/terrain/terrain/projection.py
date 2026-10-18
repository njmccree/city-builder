"""The map's projection and world frame (docs/architecture.md section 15.3).

The projection is Lambert azimuthal equal-area centered on ``map.json``'s ``projection`` (``lat_0``, ``lon_0``),
or on the middle of ``bounds`` if there is none, on the WGS 84 ellipsoid. The **world frame** is meters from the
south-west corner of the box that encloses the projected ``bounds`` (x east, z north): the same frame the game uses
for region-level positions. Every step that turns lon/lat into map positions uses ``make_to_world`` so they all agree.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from pyproj import Transformer

from .corrections import ToWorld

_EDGE_SAMPLES = 41


class ProjectionError(ValueError):
    """map.json does not say enough to project the map."""


def _bounds(map_def: dict) -> tuple[float, float, float, float]:
    bounds = map_def.get("bounds")
    keys = ("min_lon", "min_lat", "max_lon", "max_lat")
    if not isinstance(bounds, dict) or not all(isinstance(bounds.get(k), (int, float)) for k in keys):
        raise ProjectionError("map.json needs 'bounds' with min_lon, min_lat, max_lon and max_lat")
    min_lon, min_lat, max_lon, max_lat = (float(bounds[k]) for k in keys)
    if not (-180.0 <= min_lon < max_lon <= 180.0 and -90.0 <= min_lat < max_lat <= 90.0):
        raise ProjectionError("map.json 'bounds' must satisfy min < max inside -180..180 and -90..90")
    return min_lon, min_lat, max_lon, max_lat


def projection_center(map_def: dict) -> tuple[float, float]:
    """(lat_0, lon_0): from 'projection' if given, else the middle of the bounds."""
    projection = map_def.get("projection")
    if isinstance(projection, dict) and "lat_0" in projection and "lon_0" in projection:
        return float(projection["lat_0"]), float(projection["lon_0"])
    min_lon, min_lat, max_lon, max_lat = _bounds(map_def)
    return (min_lat + max_lat) / 2.0, (min_lon + max_lon) / 2.0


@dataclass(frozen=True)
class WorldFrame:
    """The map's projected frame: the projection and where the world frame's origin sits in it.

    ``crs`` is the PROJ string of the Lambert azimuthal equal-area projection. A projected point (X, Y) is the
    world position ``(X - origin_x, Y - origin_y)``. ``width_m`` and ``height_m`` are the size of the box that
    encloses the projected bounds."""

    crs: str
    lat_0: float
    lon_0: float
    origin_x: float
    origin_y: float
    width_m: float
    height_m: float
    bounds: tuple[float, float, float, float]  # min_lon, min_lat, max_lon, max_lat

    def lonlat_to_projected(self) -> Transformer:
        return Transformer.from_crs("EPSG:4326", self.crs, always_xy=True)

    def to_world(self) -> ToWorld:
        transformer = self.lonlat_to_projected()

        def to_world(lon: float, lat: float) -> tuple[float, float]:
            x, y = transformer.transform(lon, lat)
            return x - self.origin_x, y - self.origin_y

        return to_world


def world_frame(map_def: dict) -> WorldFrame:
    """The frame of the map described by ``map_def`` (needs ``bounds``; ``projection`` is optional)."""
    min_lon, min_lat, max_lon, max_lat = _bounds(map_def)
    lat_0, lon_0 = projection_center(map_def)
    crs = f"+proj=laea +lat_0={lat_0} +lon_0={lon_0} +datum=WGS84 +units=m +no_defs"
    transformer = Transformer.from_crs("EPSG:4326", crs, always_xy=True)
    # The box around the projected lon/lat rectangle: sample its four edges, since they bend.
    xs: list[float] = []
    ys: list[float] = []
    for i in range(_EDGE_SAMPLES):
        f = i / (_EDGE_SAMPLES - 1)
        lon = min_lon + (max_lon - min_lon) * f
        lat = min_lat + (max_lat - min_lat) * f
        for corner_lon, corner_lat in ((lon, min_lat), (lon, max_lat), (min_lon, lat), (max_lon, lat)):
            x, y = transformer.transform(corner_lon, corner_lat)
            xs.append(x)
            ys.append(y)
    origin_x, origin_y = min(xs), min(ys)
    if not (math.isfinite(origin_x) and math.isfinite(origin_y)):
        raise ProjectionError("the map bounds cannot be projected")
    return WorldFrame(crs, lat_0, lon_0, origin_x, origin_y, max(xs) - origin_x, max(ys) - origin_y,
                      (min_lon, min_lat, max_lon, max_lat))


def make_to_world(map_def: dict) -> ToWorld:
    """A function (lon, lat) -> (x, z) in the world frame of this map."""
    return world_frame(map_def).to_world()


def world_size(map_def: dict) -> tuple[float, float]:
    """(width, height) in meters of the box around the projected bounds."""
    frame = world_frame(map_def)
    return frame.width_m, frame.height_m

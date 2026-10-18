"""The rivers preview: rivers, lakes and floodplains drawn over the hillshade, for the designer to check by eye."""

from __future__ import annotations

import numpy as np
from rasterio.features import rasterize
from rasterio.transform import Affine
from shapely.geometry import LineString, Point, Polygon

from .previews import shaded_image

_FLOODPLAIN = np.array([120, 190, 235], dtype=np.float32)
_LAKE = np.array([30, 90, 200], dtype=np.uint8)
_RIVER = np.array([10, 40, 170], dtype=np.uint8)
_NODE = np.array([255, 220, 0], dtype=np.uint8)


def rivers_image(ground: np.ndarray, land: np.ndarray, cell_m: float, rivers: list[dict], lakes: list[dict]) -> np.ndarray:
    """An (height, width, 3) uint8 image, north up: hillshade, floodplains tinted, lakes and rivers in blue (rivers at
    least two pixels wide, wider where the river is), and the junctions between reaches as yellow dots. ``rivers`` and
    ``lakes`` are the documents written to rivers.json and lakes.json (world meters)."""
    rows, cols = ground.shape
    image = shaded_image(ground, land, cell_m, min_width=cols, max_width=max(cols, 4000), z_factor=3.0).astype(np.float32)
    top = rows * cell_m
    transform = Affine(cell_m, 0.0, 0.0, 0.0, -cell_m, top)

    def burn(shapes) -> np.ndarray:
        shapes = list(shapes)
        if not shapes:
            return np.zeros((rows, cols), dtype=bool)
        return rasterize(shapes, out_shape=(rows, cols), transform=transform, fill=0, dtype="uint8",
                         all_touched=True).astype(bool)

    plain = burn((Polygon(ring), 1) for river in rivers for reach in river["reaches"] for ring in reach["floodplain"]
                 if len(ring) >= 4)
    image[plain] = image[plain] * 0.6 + _FLOODPLAIN * 0.4
    lake = burn((Polygon(lk["rings"][0], lk["rings"][1:]), 1) for lk in lakes)
    image[lake] = _LAKE
    channel = burn((LineString([(v["x"], v["z"]) for v in reach["vertices"]]).buffer(max(reach["vertices"][0]["width_m"] / 2,
                                                                                         1.1 * cell_m)), 1)
                   for river in rivers for reach in river["reaches"])
    image[channel] = _RIVER
    ends = burn((Point(*(reach["vertices"][i]["x"], reach["vertices"][i]["z"])).buffer(2.5 * cell_m), 1)
                for river in rivers for reach in river["reaches"] for i in (0, -1))
    image[ends] = _NODE
    return np.clip(image, 0, 255).astype(np.uint8)

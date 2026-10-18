"""Derived map layers: fertility, vegetation, wetness and mineral deposits (docs/architecture.md section 15.8).

Everything here is rules, written down so the designer can read and tune them (the weights are parameters in
``map.json`` under ``"layers"``, with the defaults below). The rules are **ancient-correct**: modern cropland is
irrigated far beyond the floodplains that fed the early cities, so the land-cover class "cropland" is NOT treated
as fertile, and fertility comes from the river, the floodplain and the water, not from today's farms.

Inputs (all on the region grid, see ``corrections.Grid``): the ground ``dem``, the ``land`` mask, the ``rivers``
(after the corrections), and optionally a ``floodplain`` mask (rasterized floodplain polygons) and a ``landcover``
class grid (ESA WorldCover codes). Outputs are floats 0..1; ``to_u8`` makes the 0..255 rasters of the package.

Wetness (0..1)
    1 on water. Otherwise the largest of: closeness to a river, ``exp(-distance / river_wetness_scale_m)``;
    closeness to the sea, ``coast_wetness * exp(-distance / coast_wetness_scale_m)``; and the land-cover wetness of
    the class (wetland and mangrove are high; the delta's marshes matter).
Fertility (0..1), on land only
    ``clamp(base + river_weight * near_river + floodplain_weight * in_floodplain + wetness_weight * wetness)``
    where ``base`` is the land-cover value (``landcover_fertility``; ``unknown_land_fertility`` without land
    cover) and ``near_river = exp(-height_above_river / river_height_scale_m) * exp(-distance / river_distance_scale_m)``:
    land that is high above the water, or far from the channel, gets little, whatever it grows today.
Vegetation (0..1), on land only
    the land-cover value (``landcover_vegetation``) when there is land cover, else ``0.8 * fertility + 0.2 *
    wetness``; plus ``riparian_weight * near_river`` (the green strip along a river).
Deposits (one 0..1 layer per resource)
    authored polygons with a ``richness`` (soft edges, ``deposit_soft_edge_m``), and generic stone filled in
    where the geology polygons say limestone or sandstone (``generic_stone_richness`` scaled by value noise).
Fertility overrides
    authored polygons with ``mode`` set, add or mul and a ``value``, applied in order after the rules.
"""

from __future__ import annotations

import json
import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field, fields
from pathlib import Path

import numpy as np

from .corrections import (CONFIDENCES, CorrectionError, Grid, RiverLine, ToWorld, _feather_weight, _rings_of,
                          _window, points_in_rings)

# ESA WorldCover class codes.
TREE_COVER, SHRUBLAND, GRASSLAND, CROPLAND, BUILT_UP, BARE, SNOW_ICE, WATER, WETLAND, MANGROVES, MOSS = (
    10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 100)


class LayerError(ValueError):
    """A layers parameter or overlay is wrong; the message names the file and the feature."""


@dataclass
class LayerParams:
    """The tunable numbers of the rules. ``map.json`` may override any of them under ``"layers"``."""

    river_height_scale_m: float = 6.0       # fertility fades with height above the river over about this much
    river_distance_scale_m: float = 6000.0  # ... and with distance from the channel over about this much
    river_weight: float = 0.45
    floodplain_weight: float = 0.35
    wetness_weight: float = 0.15
    unknown_land_fertility: float = 0.12    # the base where there is no land-cover data
    riparian_weight: float = 0.35
    river_wetness_scale_m: float = 1500.0
    coast_wetness: float = 0.5
    coast_wetness_scale_m: float = 1500.0
    deposit_soft_edge_m: float = 2000.0
    generic_stone_richness: float = 0.5
    noise_scale_m: float = 8000.0
    # Land-cover class -> value. Cropland is deliberately low: today's irrigation says nothing about ancient soil.
    landcover_fertility: dict = field(default_factory=lambda: {
        TREE_COVER: 0.40, SHRUBLAND: 0.20, GRASSLAND: 0.30, CROPLAND: 0.12, BUILT_UP: 0.12, BARE: 0.04,
        SNOW_ICE: 0.0, WATER: 0.0, WETLAND: 0.60, MANGROVES: 0.50, MOSS: 0.08})
    landcover_vegetation: dict = field(default_factory=lambda: {
        TREE_COVER: 0.90, SHRUBLAND: 0.55, GRASSLAND: 0.45, CROPLAND: 0.20, BUILT_UP: 0.08, BARE: 0.03,
        SNOW_ICE: 0.0, WATER: 0.0, WETLAND: 0.70, MANGROVES: 0.85, MOSS: 0.12})
    landcover_wetness: dict = field(default_factory=lambda: {WATER: 1.0, WETLAND: 0.90, MANGROVES: 0.85})
    # Geology class -> the deposit layer that generic stone goes into.
    geology_layers: dict = field(default_factory=lambda: {"limestone": "stone_limestone",
                                                          "sandstone": "stone_sandstone"})

    @staticmethod
    def from_json(overrides: dict | None) -> "LayerParams":
        params = LayerParams()
        if not overrides:
            return params
        if not isinstance(overrides, dict):
            raise LayerError("map.json 'layers' must be an object")
        known = {f.name for f in fields(LayerParams)}
        for key, value in overrides.items():
            if key not in known:
                raise LayerError(f"map.json 'layers': unknown parameter '{key}' (known: {', '.join(sorted(known))})")
            current = getattr(params, key)
            if isinstance(current, dict):
                if not isinstance(value, dict):
                    raise LayerError(f"map.json 'layers': '{key}' must be an object")
                merged = dict(current)
                for k, v in value.items():
                    merged[int(k) if isinstance(next(iter(current)), int) else str(k)] = v
                setattr(params, key, merged)
            else:
                if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                    raise LayerError(f"map.json 'layers': '{key}' must be a number")
                setattr(params, key, float(value))
        for key in ("river_height_scale_m", "river_distance_scale_m", "river_wetness_scale_m",
                    "coast_wetness_scale_m", "noise_scale_m"):
            if not getattr(params, key) > 0.0:
                raise LayerError(f"map.json 'layers': '{key}' must be greater than 0")
        if params.deposit_soft_edge_m < 0.0:
            raise LayerError("map.json 'layers': 'deposit_soft_edge_m' must not be negative")
        return params


@dataclass
class LayerSet:
    """The results, floats 0..1 on the region grid."""

    fertility: Grid
    vegetation: Grid
    wetness: Grid
    deposits: dict[str, Grid] = field(default_factory=dict)


def to_u8(values: np.ndarray) -> np.ndarray:
    """0..1 floats as the 0..255 bytes of a package layer (scale 1/255)."""
    return np.rint(np.clip(values, 0.0, 1.0) * 255.0).astype(np.uint8)


# --- Distances ----------------------------------------------------------------------------------------------------


def nearest_source(sources: np.ndarray, cell_m: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """For every cell, the distance in meters to the nearest ``True`` cell of ``sources`` and that cell's row and
    column. Jump flooding: about log2(size) passes of vectorized numpy, and exact enough for rules whose scales are
    kilometers (ties and the rare miss are within a cell). Cells with no source anywhere get ``inf`` and -1.
    """
    rows, cols = sources.shape
    near_row = np.full(sources.shape, -1, dtype=np.int64)
    near_col = np.full(sources.shape, -1, dtype=np.int64)
    source_rows, source_cols = np.nonzero(sources)
    near_row[source_rows, source_cols] = source_rows
    near_col[source_rows, source_cols] = source_cols
    if source_rows.size == 0:
        return np.full(sources.shape, np.inf), near_row, near_col
    grid_r, grid_c = np.indices(sources.shape)
    step = 1 << max(int(math.ceil(math.log2(max(rows, cols, 2)))) - 1, 0)
    first = True
    while step >= 1 or first:
        for dr in (-step, 0, step):
            for dc in (-step, 0, step):
                if dr == 0 and dc == 0:
                    continue
                # Look at the neighbor `step` cells away and adopt its nearest source if that is closer.
                cand_r = np.full(sources.shape, -1, dtype=np.int64)
                cand_c = np.full(sources.shape, -1, dtype=np.int64)
                r0, r1 = max(0, -dr), min(rows, rows - dr)
                c0, c1 = max(0, -dc), min(cols, cols - dc)
                if r0 >= r1 or c0 >= c1:
                    continue
                cand_r[r0:r1, c0:c1] = near_row[r0 + dr:r1 + dr, c0 + dc:c1 + dc]
                cand_c[r0:r1, c0:c1] = near_col[r0 + dr:r1 + dr, c0 + dc:c1 + dc]
                valid = cand_r >= 0
                cand_dist = np.where(valid, (cand_r - grid_r) ** 2 + (cand_c - grid_c) ** 2, np.iinfo(np.int64).max)
                have = near_row >= 0
                cur_dist = np.where(have, (near_row - grid_r) ** 2 + (near_col - grid_c) ** 2, np.iinfo(np.int64).max)
                better = valid & (cand_dist < cur_dist)
                near_row = np.where(better, cand_r, near_row)
                near_col = np.where(better, cand_c, near_col)
        first = False
        if step == 1:
            break
        step //= 2
    distance = np.where(near_row >= 0, np.hypot(near_row - grid_r, near_col - grid_c) * cell_m, np.inf)
    return distance, near_row, near_col


def rasterize_rivers(grid: Grid, rivers: Sequence[RiverLine]) -> np.ndarray:
    """The cells that a river passes through (polylines sampled at half-cell steps)."""
    mask = np.zeros(grid.shape, dtype=bool)
    for river in rivers:
        for a, b in zip(river.points[:-1], river.points[1:]):
            length = float(np.hypot(*(b - a)))
            count = max(int(math.ceil(length / (0.5 * grid.cell_m))), 1)
            t = np.linspace(0.0, 1.0, count + 1)
            xs = a[0] + (b[0] - a[0]) * t
            zs = a[1] + (b[1] - a[1]) * t
            cols = np.floor((xs - grid.origin_x) / grid.cell_m).astype(int)
            rows = np.floor((zs - grid.origin_z) / grid.cell_m).astype(int)
            ok = (rows >= 0) & (rows < grid.shape[0]) & (cols >= 0) & (cols < grid.shape[1])
            mask[rows[ok], cols[ok]] = True
    return mask


# --- Fertility, vegetation, wetness -------------------------------------------------------------------------------


def _lookup(landcover: np.ndarray, table: dict, default: float) -> np.ndarray:
    out = np.full(landcover.shape, default, dtype=np.float64)
    for code, value in table.items():
        out[landcover == code] = value
    return out


def derive_layers(dem: Grid, land: Grid, rivers: Sequence[RiverLine], params: LayerParams, *,
                  floodplain: Grid | None = None, landcover: Grid | None = None) -> LayerSet:
    """Fertility, vegetation and wetness by the rules in this module's docstring (deposits are separate)."""
    shape = dem.shape
    for name, grid in (("land", land), ("floodplain", floodplain), ("landcover", landcover)):
        if grid is not None and grid.shape != shape:
            raise LayerError(f"the {name} grid must have the same shape as the ground {shape}, not {grid.shape}")
    is_land = land.data.astype(bool)
    cell = dem.cell_m

    river_cells = rasterize_rivers(dem, rivers)
    river_distance, near_row, near_col = nearest_source(river_cells, cell)
    if river_cells.any():
        river_level = dem.data[np.clip(near_row, 0, shape[0] - 1), np.clip(near_col, 0, shape[1] - 1)]
        height_above = np.maximum(dem.data - river_level, 0.0)
        near_river = np.exp(-height_above / params.river_height_scale_m) * np.exp(-river_distance / params.river_distance_scale_m)
        river_close = np.exp(-river_distance / params.river_wetness_scale_m)
    else:
        near_river = np.zeros(shape)
        river_close = np.zeros(shape)

    sea_distance, _, _ = nearest_source(~is_land, cell)
    coast_close = params.coast_wetness * np.exp(-sea_distance / params.coast_wetness_scale_m)
    wetness = np.maximum(river_close, coast_close)
    if landcover is not None:
        wetness = np.maximum(wetness, _lookup(landcover.data, params.landcover_wetness, 0.0))
    wetness = np.where(is_land, wetness, 1.0)

    if landcover is not None:
        base_fertility = _lookup(landcover.data, params.landcover_fertility, params.unknown_land_fertility)
    else:
        base_fertility = np.full(shape, params.unknown_land_fertility)
    in_floodplain = floodplain.data.astype(np.float64) if floodplain is not None else np.zeros(shape)
    fertility = (base_fertility + params.river_weight * near_river + params.floodplain_weight * in_floodplain +
                 params.wetness_weight * wetness)
    fertility = np.where(is_land, np.clip(fertility, 0.0, 1.0), 0.0)

    if landcover is not None:
        vegetation = _lookup(landcover.data, params.landcover_vegetation, 0.1)
    else:
        vegetation = 0.8 * fertility + 0.2 * np.where(is_land, wetness, 0.0)
    vegetation = np.where(is_land, np.clip(vegetation + params.riparian_weight * near_river, 0.0, 1.0), 0.0)

    def grid_like(values: np.ndarray) -> Grid:
        return Grid(values, dem.origin_x, dem.origin_z, dem.cell_m)

    return LayerSet(grid_like(fertility), grid_like(vegetation), grid_like(np.clip(wetness, 0.0, 1.0)))


# --- Authored overlays --------------------------------------------------------------------------------------------


@dataclass
class OverlayFeature:
    file: str
    index: int
    properties: dict
    rings: list[np.ndarray]

    @property
    def label(self) -> str:
        name = self.properties.get("name")
        return f"{self.file} feature {self.index}" + (f" ({name})" if name else "")


def _load_overlay(path: Path | str, to_world: ToWorld, check: Callable[[OverlayFeature], None]) -> list[OverlayFeature]:
    path = Path(path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise LayerError(f"{path}: cannot read the file: {error.strerror or error}") from None
    except ValueError as error:
        raise LayerError(f"{path}: not valid JSON: {error}") from None
    if not isinstance(data, dict) or data.get("type") != "FeatureCollection" or not isinstance(data.get("features"), list):
        raise LayerError(f"{path.name}: not a GeoJSON FeatureCollection")
    features: list[OverlayFeature] = []
    for index, raw in enumerate(data["features"]):
        properties = raw.get("properties") if isinstance(raw, dict) else None
        where = f"{path.name} feature {index}"
        if not isinstance(properties, dict):
            raise LayerError(f"{where}: a feature needs 'properties'")
        source = properties.get("source")
        if not isinstance(source, str) or not source.strip():
            raise LayerError(f"{where}: missing property 'source' (a citable reference)")
        if properties.get("confidence") not in CONFIDENCES:
            raise LayerError(f"{where}: property 'confidence' must be one of {', '.join(CONFIDENCES)}")
        try:
            rings = _rings_of(raw.get("geometry") or {}, to_world, where)
        except CorrectionError as error:
            raise LayerError(str(error)) from None
        feature = OverlayFeature(path.name, index, properties, rings)
        check(feature)
        features.append(feature)
    return features


def _number(feature: OverlayFeature, key: str, low: float | None = None, high: float | None = None) -> float:
    value = feature.properties.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise LayerError(f"{feature.label}: property '{key}' must be a number")
    if (low is not None and value < low) or (high is not None and value > high):
        raise LayerError(f"{feature.label}: property '{key}' must be between {low} and {high}")
    return float(value)


def layer_name(resource_id: str) -> str:
    """The package layer name of a resource id: the part after the mod prefix, without a leading ``resource.``
    (``base:resource.ore_copper`` and ``base:ore_copper`` both give ``ore_copper``)."""
    name = resource_id.split(":", 1)[-1]
    return name[len("resource."):] if name.startswith("resource.") else name


def load_deposits(path: Path | str, to_world: ToWorld) -> list[OverlayFeature]:
    """Authored deposits and geology. A deposit has ``resource`` and ``richness`` (0..1); a geology polygon has
    ``geology`` (limestone, sandstone, alluvium, ...). Every feature has ``source`` and ``confidence``."""

    def check(feature: OverlayFeature) -> None:
        has_resource = "resource" in feature.properties
        has_geology = "geology" in feature.properties
        if has_resource == has_geology:
            raise LayerError(f"{feature.label}: a feature needs either 'resource' (with 'richness') or 'geology'")
        if has_resource:
            if not isinstance(feature.properties["resource"], str) or not feature.properties["resource"]:
                raise LayerError(f"{feature.label}: property 'resource' must be a resource id")
            _number(feature, "richness", 0.0, 1.0)
        elif not isinstance(feature.properties["geology"], str) or not feature.properties["geology"]:
            raise LayerError(f"{feature.label}: property 'geology' must be a name such as limestone")

    return _load_overlay(path, to_world, check)


def load_fertility_overrides(path: Path | str, to_world: ToWorld) -> list[OverlayFeature]:
    def check(feature: OverlayFeature) -> None:
        if feature.properties.get("mode") not in ("set", "add", "mul"):
            raise LayerError(f"{feature.label}: property 'mode' must be set, add or mul")
        _number(feature, "value")
        if feature.properties["mode"] == "set":
            _number(feature, "value", 0.0, 1.0)
        if "feather_m" in feature.properties:
            _number(feature, "feather_m", 0.0)

    return _load_overlay(path, to_world, check)


def apply_fertility_overrides(fertility: Grid, overrides: Sequence[OverlayFeature]) -> None:
    """Applies the overrides to the fertility grid in place, in order, then clamps to 0..1."""
    for feature in overrides:
        rows, cols = _window(fertility, feature.rings)
        x, z = fertility.centers(rows, cols)
        inside = points_in_rings(x, z, feature.rings)
        weight = _feather_weight(x, z, feature.rings, inside, float(feature.properties.get("feather_m", 0.0)))
        window = fertility.data[rows, cols]
        value = float(feature.properties["value"])
        mode = feature.properties["mode"]
        target = value if mode == "set" else (window + value if mode == "add" else window * value)
        fertility.data[rows, cols] = window + (target - window) * weight
    np.clip(fertility.data, 0.0, 1.0, out=fertility.data)


def _noise(grid: Grid, seed: int, scale_m: float) -> np.ndarray:
    """Smooth value noise 0..1 on the grid, from an integer hash (no random number generator): the same seed gives
    the same values on any machine."""
    def lattice(ix: np.ndarray, iz: np.ndarray) -> np.ndarray:
        h = (ix.astype(np.uint64) * np.uint64(0x9E3779B97F4A7C15)) ^ (iz.astype(np.uint64) * np.uint64(0xC2B2AE3D27D4EB4F))
        h ^= np.uint64(seed & 0xFFFFFFFFFFFFFFFF)
        h ^= h >> np.uint64(33)
        h *= np.uint64(0xFF51AFD7ED558CCD)
        h ^= h >> np.uint64(33)
        return (h >> np.uint64(11)).astype(np.float64) / float(1 << 53)

    cols = (np.arange(grid.shape[1]) + 0.5) * grid.cell_m / scale_m
    rows = (np.arange(grid.shape[0]) + 0.5) * grid.cell_m / scale_m
    x, z = np.meshgrid(cols, rows)
    ix, iz = np.floor(x).astype(np.int64), np.floor(z).astype(np.int64)
    fx, fz = x - ix, z - iz
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    top = lattice(ix, iz + 1) * (1 - sx) + lattice(ix + 1, iz + 1) * sx
    bottom = lattice(ix, iz) * (1 - sx) + lattice(ix + 1, iz) * sx
    return bottom * (1 - sz) + top * sz


def rasterize_deposits(template: Grid, land: Grid, features: Sequence[OverlayFeature], params: LayerParams,
                       seed: int = 0) -> dict[str, Grid]:
    """One 0..1 grid per resource layer: the authored deposits (soft edged) and the generic stone of the geology
    polygons, combined with the larger value where they overlap. Only land gets deposits."""
    layers: dict[str, np.ndarray] = {}

    def grid_for(name: str) -> np.ndarray:
        return layers.setdefault(name, np.zeros(template.shape))

    noise = None
    for feature in features:
        rows, cols = _window(template, feature.rings)
        x, z = template.centers(rows, cols)
        inside = points_in_rings(x, z, feature.rings)
        if "resource" in feature.properties:
            weight = _feather_weight(x, z, feature.rings, inside, params.deposit_soft_edge_m)
            value = float(feature.properties["richness"]) * weight
            target = grid_for(layer_name(feature.properties["resource"]))
        else:
            name = params.geology_layers.get(feature.properties["geology"])
            if name is None:
                continue  # alluvium and the like: no generic stone
            if noise is None:
                noise = _noise(template, seed, params.noise_scale_m)
            value = np.where(inside, params.generic_stone_richness * (0.6 + 0.4 * noise[rows, cols]), 0.0)
            target = grid_for(name)
        target[rows, cols] = np.maximum(target[rows, cols], value)
    out: dict[str, Grid] = {}
    for name in sorted(layers):
        out[name] = Grid(np.where(land.data.astype(bool), layers[name], 0.0), template.origin_x, template.origin_z,
                         template.cell_m)
    return out

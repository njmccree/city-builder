"""Historical correction overlays (docs/architecture.md section 15.7).

The modern terrain data shows works that did not exist when a map starts: canals, reservoirs, drained marshes. The
designer authors **GeoJSON overlays** that undo them, and this module applies the overlays, in order, to the
pipeline's working data: the DEM, the land mask and the set of rivers. Every feature says where it comes from
(``source``) and how sure that is (``confidence``); a feature without both is an error, and a report lists what each
feature did so the designer can review it.

Coordinates in the GeoJSON are lon/lat as GeoJSON has them. The pipeline hands in the function that turns them
into the map's world frame (meters from the south-west corner of the projected bounds, x east and z north, like
the rest of the pipeline); this module never needs a map library.

Features (``properties`` has ``op``, ``source``, ``confidence``, optionally ``name``, plus the operation's own):

``set_water``      polygon; ``level_m``, optional ``bed_depth_m`` (default 2). Makes the polygon water: the ground
                   inside is lowered to at most ``level_m - bed_depth_m`` and the land mask is cleared. A sea is
                   ``level_m`` 0.
``set_land``       polygon; ``min_elevation_m``. Raises the ground inside to at least that and sets the land mask
                   (filling a canal or a drowned coast).
``elevation_offset``  polygon; ``meters``, optional ``feather_m``. Adds the offset, fading in over ``feather_m``
                   inside the polygon's edge.
``flatten_to``     polygon; ``elevation_m``, optional ``feather_m``. Pulls the ground toward the elevation, fully in
                   the middle and fading toward the edge.
``remove_river``   ``match``: ``{"name": ...}`` or ``{"id": ...}``; optional polygon. Removes the matching river, or
                   only the parts inside the polygon.
``river_path``     line string; ``name``, ``width_m``, ``depth_m`` (a number or one per vertex), optional ``id``.
                   Adds a river, or replaces the one with that name.
``fill_reservoir`` polygon; ``downstream`` and ``upstream`` as [lon, lat] points, ``profile`` "valley_interpolate",
                   optional ``downstream_elevation_m`` / ``upstream_elevation_m`` (else sampled from the ground),
                   ``bank_rise_m`` (default 8), ``channel_width_m`` and ``channel_depth_m`` (default none). Replaces
                   a reservoir's flat surface with a valley that falls from the upstream to the downstream
                   elevation along the line between the two points, rising to the banks, and sets the land mask.
"""

from __future__ import annotations

import json
import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

CONFIDENCES = ("low", "medium", "high")
OPERATIONS = ("set_water", "set_land", "elevation_offset", "flatten_to", "remove_river", "river_path",
              "fill_reservoir")

ToWorld = Callable[[float, float], tuple[float, float]]


class CorrectionError(ValueError):
    """A correction file or feature is wrong; the message names the file and the feature."""


# --- The working data ---------------------------------------------------------------------------------------------


@dataclass
class Grid:
    """A raster in the world frame: ``data[row, col]`` with row 0 at the south, cell (col, row) centered at
    ``(origin_x + (col + 0.5) * cell_m, origin_z + (row + 0.5) * cell_m)``."""

    data: np.ndarray
    origin_x: float = 0.0
    origin_z: float = 0.0
    cell_m: float = 1.0

    @property
    def shape(self) -> tuple[int, int]:
        return self.data.shape  # type: ignore[return-value]

    def centers(self, rows: slice, cols: slice) -> tuple[np.ndarray, np.ndarray]:
        """The x and z of the cell centers in a window, as 2D arrays shaped like that window."""
        xs = self.origin_x + (np.arange(*cols.indices(self.shape[1])) + 0.5) * self.cell_m
        zs = self.origin_z + (np.arange(*rows.indices(self.shape[0])) + 0.5) * self.cell_m
        return np.meshgrid(xs, zs)

    def sample(self, x: float, z: float) -> float:
        """The value of the cell that holds the point (clamped to the grid)."""
        col = int(np.clip(math.floor((x - self.origin_x) / self.cell_m), 0, self.shape[1] - 1))
        row = int(np.clip(math.floor((z - self.origin_z) / self.cell_m), 0, self.shape[0] - 1))
        return float(self.data[row, col])


@dataclass
class RiverLine:
    """A river as the pipeline carries it before it is cut into reaches: a polyline with a width and a depth at
    every vertex."""

    id: str
    name: str
    points: np.ndarray  # (n, 2): x, z
    width_m: np.ndarray  # (n,)
    depth_m: np.ndarray  # (n,)


@dataclass
class MapState:
    """What corrections change: the ground, which cells are land, and the rivers."""

    dem: Grid
    land: Grid  # bool
    rivers: list[RiverLine] = field(default_factory=list)

    def check(self) -> None:
        if self.dem.shape != self.land.shape:
            raise ValueError("the DEM and the land mask must have the same shape")


@dataclass
class Correction:
    """One feature of an overlay, read and checked."""

    file: str
    index: int
    op: str
    name: str
    source: str
    confidence: str
    properties: dict
    rings: list[np.ndarray]  # polygon rings in the world frame (all rings of all polygons; even-odd fill)
    line: np.ndarray | None  # (n, 2) for a line string

    @property
    def label(self) -> str:
        return f"{self.file} feature {self.index}" + (f" ({self.name})" if self.name else "")


@dataclass
class Effect:
    """What one applied correction did, for the report."""

    correction: Correction
    cells: int = 0
    area_km2: float = 0.0
    mean_change_m: float = 0.0
    land_cells_changed: int = 0
    rivers_removed: int = 0
    rivers_added: int = 0
    note: str = ""


# --- Reading ------------------------------------------------------------------------------------------------------


def _number(properties: dict, key: str, where: str, *, required: bool = True, default: float | None = None) -> float:
    if key not in properties:
        if required:
            raise CorrectionError(f"{where}: missing property '{key}'")
        return default  # type: ignore[return-value]
    value = properties[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise CorrectionError(f"{where}: property '{key}' must be a number")
    return float(value)


def _rings_of(geometry: dict, to_world: ToWorld, where: str) -> list[np.ndarray]:
    kind = geometry.get("type")
    if kind == "Polygon":
        polygons = [geometry.get("coordinates")]
    elif kind == "MultiPolygon":
        polygons = geometry.get("coordinates")
    else:
        raise CorrectionError(f"{where}: the geometry must be a Polygon or MultiPolygon, not {kind}")
    rings: list[np.ndarray] = []
    for polygon in polygons or []:
        for ring in polygon or []:
            if not isinstance(ring, list) or len(ring) < 4:
                raise CorrectionError(f"{where}: a polygon ring needs at least 4 points (closed)")
            points = np.array([to_world(float(p[0]), float(p[1])) for p in ring], dtype=np.float64)
            if not np.allclose(points[0], points[-1]):
                raise CorrectionError(f"{where}: a polygon ring is not closed (its last point must repeat its first)")
            rings.append(points)
    if not rings:
        raise CorrectionError(f"{where}: the polygon has no rings")
    return rings


def parse_features(data: dict, file: str, to_world: ToWorld) -> list[Correction]:
    """The corrections in one GeoJSON FeatureCollection, checked: operation known, source and confidence given,
    the operation's own properties and geometry right."""
    if not isinstance(data, dict) or data.get("type") != "FeatureCollection" or not isinstance(data.get("features"), list):
        raise CorrectionError(f"{file}: not a GeoJSON FeatureCollection")
    corrections: list[Correction] = []
    for index, feature in enumerate(data["features"]):
        where = f"{file} feature {index}"
        if not isinstance(feature, dict) or not isinstance(feature.get("properties"), dict):
            raise CorrectionError(f"{where}: a feature needs 'properties'")
        properties = feature["properties"]
        op = properties.get("op")
        if op not in OPERATIONS:
            raise CorrectionError(f"{where}: property 'op' must be one of {', '.join(OPERATIONS)} (got {op!r})")
        name = str(properties.get("name", ""))
        where = f"{file} feature {index}" + (f" ({name})" if name else "")
        source = properties.get("source")
        if not isinstance(source, str) or not source.strip():
            raise CorrectionError(f"{where}: missing property 'source' (where this correction comes from)")
        confidence = properties.get("confidence")
        if confidence not in CONFIDENCES:
            raise CorrectionError(f"{where}: property 'confidence' must be one of {', '.join(CONFIDENCES)}")
        geometry = feature.get("geometry")
        rings: list[np.ndarray] = []
        line: np.ndarray | None = None
        if op == "river_path":
            if not isinstance(geometry, dict) or geometry.get("type") != "LineString" or len(geometry["coordinates"]) < 2:
                raise CorrectionError(f"{where}: river_path needs a LineString of at least 2 points")
            line = np.array([to_world(float(p[0]), float(p[1])) for p in geometry["coordinates"]], dtype=np.float64)
        elif op == "remove_river":
            if geometry is not None:
                rings = _rings_of(geometry, to_world, where)
        else:
            if not isinstance(geometry, dict):
                raise CorrectionError(f"{where}: {op} needs a Polygon geometry")
            rings = _rings_of(geometry, to_world, where)
        correction = Correction(file, index, op, name, source.strip(), confidence, dict(properties), rings, line)
        _check_properties(correction, to_world)
        corrections.append(correction)
    return corrections


def _check_properties(correction: Correction, to_world: ToWorld) -> None:
    where = correction.label
    p = correction.properties
    op = correction.op
    if op == "set_water":
        _number(p, "level_m", where)
        if _number(p, "bed_depth_m", where, required=False, default=2.0) < 0:
            raise CorrectionError(f"{where}: 'bed_depth_m' must not be negative")
    elif op == "set_land":
        _number(p, "min_elevation_m", where)
    elif op == "elevation_offset":
        _number(p, "meters", where)
        if _number(p, "feather_m", where, required=False, default=0.0) < 0:
            raise CorrectionError(f"{where}: 'feather_m' must not be negative")
    elif op == "flatten_to":
        _number(p, "elevation_m", where)
        if _number(p, "feather_m", where, required=False, default=0.0) < 0:
            raise CorrectionError(f"{where}: 'feather_m' must not be negative")
    elif op == "remove_river":
        match = p.get("match")
        if not isinstance(match, dict) or set(match) not in ({"name"}, {"id"}) or not isinstance(
                next(iter(match.values())), str):
            raise CorrectionError(f"{where}: 'match' must be {{\"name\": ...}} or {{\"id\": ...}}")
    elif op == "river_path":
        if not isinstance(p.get("name"), str) or not p["name"]:
            raise CorrectionError(f"{where}: river_path needs a 'name'")
        count = len(correction.line)  # type: ignore[arg-type]
        for key in ("width_m", "depth_m"):
            value = p.get(key)
            values = value if isinstance(value, list) else [value]
            if value is None or not all(isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0 for v in values):
                raise CorrectionError(f"{where}: '{key}' must be a positive number or a list of them")
            if isinstance(value, list) and len(value) != count:
                raise CorrectionError(f"{where}: '{key}' needs one value per vertex ({count}), got {len(value)}")
    elif op == "fill_reservoir":
        if p.get("profile") != "valley_interpolate":
            raise CorrectionError(f"{where}: 'profile' must be \"valley_interpolate\"")
        for key in ("downstream", "upstream"):
            point = p.get(key)
            if not (isinstance(point, list) and len(point) == 2 and all(isinstance(v, (int, float)) for v in point)):
                raise CorrectionError(f"{where}: '{key}' must be a [lon, lat] point")
        _number(p, "downstream_elevation_m", where, required=False)
        _number(p, "upstream_elevation_m", where, required=False)
        if _number(p, "bank_rise_m", where, required=False, default=8.0) < 0:
            raise CorrectionError(f"{where}: 'bank_rise_m' must not be negative")
        if _number(p, "channel_depth_m", where, required=False, default=0.0) < 0:
            raise CorrectionError(f"{where}: 'channel_depth_m' must not be negative")
        if _number(p, "channel_width_m", where, required=False, default=0.0) < 0:
            raise CorrectionError(f"{where}: 'channel_width_m' must not be negative")


def load_correction_files(paths: Sequence[Path | str], to_world: ToWorld) -> list[Correction]:
    """The corrections of several overlay files, in file order, then feature order."""
    corrections: list[Correction] = []
    for path in paths:
        path = Path(path)
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except OSError as error:
            raise CorrectionError(f"{path}: cannot read the file: {error.strerror or error}") from None
        except ValueError as error:
            raise CorrectionError(f"{path}: not valid JSON: {error}") from None
        corrections.extend(parse_features(data, path.name, to_world))
    return corrections


# --- Geometry -----------------------------------------------------------------------------------------------------


def _window(grid: Grid, rings: list[np.ndarray], margin_m: float = 0.0) -> tuple[slice, slice]:
    """The rows and columns that can touch the rings (their box plus a margin), clipped to the grid."""
    points = np.vstack(rings)
    lo = (points.min(axis=0) - margin_m - np.array([grid.origin_x, grid.origin_z])) / grid.cell_m
    hi = (points.max(axis=0) + margin_m - np.array([grid.origin_x, grid.origin_z])) / grid.cell_m
    rows = slice(int(max(math.floor(lo[1]), 0)), int(min(math.ceil(hi[1]) + 1, grid.shape[0])))
    cols = slice(int(max(math.floor(lo[0]), 0)), int(min(math.ceil(hi[0]) + 1, grid.shape[1])))
    return rows, cols


def points_in_rings(x: np.ndarray, z: np.ndarray, rings: list[np.ndarray]) -> np.ndarray:
    """Even-odd point-in-polygon for arrays of points: a point is inside if it lies inside an odd number of rings,
    so a ring inside another one is a hole."""
    inside = np.zeros(x.shape, dtype=bool)
    for ring in rings:
        crossings = np.zeros(x.shape, dtype=bool)
        for (x1, z1), (x2, z2) in zip(ring[:-1], ring[1:]):
            if z1 == z2:
                continue
            straddles = (z1 > z) != (z2 > z)
            x_at = x1 + (z - z1) * (x2 - x1) / (z2 - z1)
            crossings ^= straddles & (x < x_at)
        inside ^= crossings
    return inside


def distance_to_rings(x: np.ndarray, z: np.ndarray, rings: list[np.ndarray]) -> np.ndarray:
    """The distance from each point to the nearest ring edge."""
    best = np.full(x.shape, np.inf)
    for ring in rings:
        for (x1, z1), (x2, z2) in zip(ring[:-1], ring[1:]):
            dx, dz = x2 - x1, z2 - z1
            length_sq = dx * dx + dz * dz
            t = np.zeros(x.shape) if length_sq == 0 else np.clip(((x - x1) * dx + (z - z1) * dz) / length_sq, 0.0, 1.0)
            best = np.minimum(best, np.hypot(x1 + t * dx - x, z1 + t * dz - z))
    return best


def _smooth(t: np.ndarray) -> np.ndarray:
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _feather_weight(x: np.ndarray, z: np.ndarray, rings: list[np.ndarray], inside: np.ndarray,
                    feather_m: float) -> np.ndarray:
    """1 inside the polygon, fading to 0 at its edge over ``feather_m`` (all 1 inside if there is no feather)."""
    weight = inside.astype(np.float64)
    if feather_m > 0.0 and inside.any():
        weight[inside] = _smooth(distance_to_rings(x[inside], z[inside], rings) / feather_m)
    return weight


def _polygon_cells(state: MapState, correction: Correction, margin_m: float = 0.0):
    grid = state.dem
    rows, cols = _window(grid, correction.rings, margin_m)
    x, z = grid.centers(rows, cols)
    inside = points_in_rings(x, z, correction.rings)
    return rows, cols, x, z, inside


# --- Applying -----------------------------------------------------------------------------------------------------


def _record(state: MapState, correction: Correction, rows: slice, cols: slice, mask: np.ndarray,
            before: np.ndarray, land_before: np.ndarray) -> Effect:
    after = state.dem.data[rows, cols]
    cells = int(mask.sum())
    effect = Effect(correction, cells=cells, area_km2=cells * (state.dem.cell_m / 1000.0) ** 2)
    if cells:
        effect.mean_change_m = float((after[mask] - before[mask]).mean())
        effect.land_cells_changed = int((state.land.data[rows, cols][mask] != land_before[mask]).sum())
    return effect


def _apply_polygon_op(state: MapState, correction: Correction) -> Effect:
    p = correction.properties
    feather = float(p.get("feather_m", 0.0)) if correction.op in ("elevation_offset", "flatten_to") else 0.0
    rows, cols, x, z, inside = _polygon_cells(state, correction)
    dem = state.dem.data[rows, cols]
    land = state.land.data[rows, cols]
    before, land_before = dem.copy(), land.copy()
    if correction.op == "set_water":
        floor = float(p["level_m"]) - float(p.get("bed_depth_m", 2.0))
        dem[inside] = np.minimum(dem[inside], floor)
        land[inside] = False
    elif correction.op == "set_land":
        dem[inside] = np.maximum(dem[inside], float(p["min_elevation_m"]))
        land[inside] = True
    elif correction.op == "elevation_offset":
        weight = _feather_weight(x, z, correction.rings, inside, feather)
        dem += float(p["meters"]) * weight
    elif correction.op == "flatten_to":
        weight = _feather_weight(x, z, correction.rings, inside, feather)
        dem += (float(p["elevation_m"]) - dem) * weight
    state.dem.data[rows, cols] = dem
    state.land.data[rows, cols] = land
    return _record(state, correction, rows, cols, inside, before, land_before)


def _clip_river_outside(river: RiverLine, rings: list[np.ndarray]) -> list[RiverLine]:
    """The parts of a river whose vertices are outside the rings (vertex-wise; a part has 2 or more vertices).
    Parts are numbered from 1 (``<id>_1``, ``<id>_2``, ...)."""
    outside = ~points_in_rings(river.points[:, 0], river.points[:, 1], rings)
    pieces: list[RiverLine] = []
    start = None
    for i, keep in enumerate([*outside, False]):
        if keep and start is None:
            start = i
        elif not keep and start is not None:
            if i - start >= 2:
                pieces.append(RiverLine(f"{river.id}_{len(pieces) + 1}", river.name, river.points[start:i].copy(),
                                        river.width_m[start:i].copy(), river.depth_m[start:i].copy()))
            start = None
    return pieces


def _apply_remove_river(state: MapState, correction: Correction) -> Effect:
    match = correction.properties["match"]
    key, wanted = next(iter(match.items()))
    kept: list[RiverLine] = []
    removed = 0
    for river in state.rivers:
        if getattr(river, key) != wanted:
            kept.append(river)
            continue
        if not correction.rings:
            removed += 1
            continue
        if not points_in_rings(river.points[:, 0], river.points[:, 1], correction.rings).any():
            kept.append(river)  # nothing of it lies inside the polygon
            continue
        removed += 1
        kept.extend(_clip_river_outside(river, correction.rings))
    state.rivers = kept
    effect = Effect(correction, rivers_removed=removed)
    if not removed:
        effect.note = f"no river with {key} {wanted!r} (or none inside the polygon)"
    return effect


def _apply_river_path(state: MapState, correction: Correction) -> Effect:
    p = correction.properties
    count = len(correction.line)  # type: ignore[arg-type]
    width = np.array(p["width_m"] if isinstance(p["width_m"], list) else [p["width_m"]] * count, dtype=np.float64)
    depth = np.array(p["depth_m"] if isinstance(p["depth_m"], list) else [p["depth_m"]] * count, dtype=np.float64)
    name = p["name"]
    replaced = [r for r in state.rivers if r.name == name]
    river_id = str(p.get("id") or (name.lower().replace(" ", "_") + f"_{correction.index}"))
    state.rivers = [r for r in state.rivers if r.name != name]
    state.rivers.append(RiverLine(river_id, name, correction.line.copy(), width, depth))  # type: ignore[union-attr]
    effect = Effect(correction, rivers_added=1, rivers_removed=len(replaced))
    if replaced:
        effect.note = f"replaced {len(replaced)} existing polyline(s) named {name!r}"
    return effect


def _apply_fill_reservoir(state: MapState, correction: Correction, to_world: ToWorld) -> Effect:
    p = correction.properties
    down = np.array(to_world(float(p["downstream"][0]), float(p["downstream"][1])))
    up = np.array(to_world(float(p["upstream"][0]), float(p["upstream"][1])))
    axis = up - down
    length_sq = float(axis @ axis)
    if length_sq == 0.0:
        raise CorrectionError(f"{correction.label}: 'downstream' and 'upstream' are the same point")
    down_elevation = float(p["downstream_elevation_m"]) if "downstream_elevation_m" in p else state.dem.sample(*down)
    up_elevation = float(p["upstream_elevation_m"]) if "upstream_elevation_m" in p else state.dem.sample(*up)

    rows, cols, x, z, inside = _polygon_cells(state, correction)
    dem = state.dem.data[rows, cols]
    land = state.land.data[rows, cols]
    before, land_before = dem.copy(), land.copy()
    if inside.any():
        t = np.clip(((x - down[0]) * axis[0] + (z - down[1]) * axis[1]) / length_sq, 0.0, 1.0)
        along_x, along_z = down[0] + t * axis[0], down[1] + t * axis[1]
        across = np.hypot(x - along_x, z - along_z)
        half_width = max(float(across[inside].max()), state.dem.cell_m)
        profile = down_elevation + (up_elevation - down_elevation) * t
        floor = profile + float(p.get("bank_rise_m", 8.0)) * (across / half_width) ** 2
        channel_depth = float(p.get("channel_depth_m", 0.0))
        channel_width = float(p.get("channel_width_m", 0.0))
        if channel_depth > 0.0 and channel_width > 0.0:
            floor -= channel_depth * (1.0 - _smooth(across / (0.5 * channel_width)))
        dem[inside] = floor[inside]
        land[inside] = True
    state.dem.data[rows, cols] = dem
    state.land.data[rows, cols] = land
    effect = _record(state, correction, rows, cols, inside, before, land_before)
    effect.note = f"valley falls from {up_elevation:.1f} m (upstream) to {down_elevation:.1f} m (downstream)"
    return effect


def apply_corrections(state: MapState, corrections: Sequence[Correction], to_world: ToWorld) -> list[Effect]:
    """Applies the corrections in order, changing ``state`` in place. Returns what each one did."""
    state.check()
    effects: list[Effect] = []
    for correction in corrections:
        if correction.op == "remove_river":
            effects.append(_apply_remove_river(state, correction))
        elif correction.op == "river_path":
            effects.append(_apply_river_path(state, correction))
        elif correction.op == "fill_reservoir":
            effects.append(_apply_fill_reservoir(state, correction, to_world))
        else:
            effects.append(_apply_polygon_op(state, correction))
    return effects


# --- The report ---------------------------------------------------------------------------------------------------


def format_report(effects: Sequence[Effect], title: str = "Historical corrections") -> str:
    """A markdown report of what each correction did, for the designer's review."""
    lines = [f"# {title}", "", f"{len(effects)} correction(s), applied in this order.", ""]
    counts = {c: sum(1 for e in effects if e.correction.confidence == c) for c in CONFIDENCES}
    lines.append("Confidence: " + ", ".join(f"{counts[c]} {c}" for c in CONFIDENCES) + ".")
    lines.append("")
    for number, effect in enumerate(effects, 1):
        c = effect.correction
        lines.append(f"## {number}. {c.op}: {c.name or '(unnamed)'}")
        lines.append("")
        lines.append(f"- File: `{c.file}`, feature {c.index}")
        lines.append(f"- Source: {c.source}")
        lines.append(f"- Confidence: **{c.confidence}**")
        if c.op in ("remove_river", "river_path"):
            lines.append(f"- Rivers removed: {effect.rivers_removed}, added: {effect.rivers_added}")
        else:
            lines.append(f"- Area: {effect.area_km2:.1f} km² ({effect.cells} cells)")
            lines.append(f"- Mean elevation change: {effect.mean_change_m:+.2f} m")
            lines.append(f"- Cells whose land/water changed: {effect.land_cells_changed}")
        if effect.note:
            lines.append(f"- Note: {effect.note}")
        lines.append("")
    return "\n".join(lines)


def write_report(path: Path | str, effects: Sequence[Effect], title: str = "Historical corrections") -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(format_report(effects, title), encoding="utf-8")

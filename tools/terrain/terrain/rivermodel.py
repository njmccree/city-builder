"""Rivers as the game wants them (docs/architecture.md section 15.6): reaches with a width, a depth and a water level at
every vertex, joined into a flow graph. This module is the part that needs no ground data: the parameters, reading
the Natural Earth lines, finding where rivers meet and split, and working out discharge, stream order, width and
depth. ``riverdem`` holds what needs the DEM and ``steps._rivers`` ties them together.

**Natural Earth has no discharge.** Its lines carry a ``scalerank`` (1 is the biggest rivers) and a name. Discharge is
therefore an estimate: ``rank_discharge`` maps a rank to a mean discharge in m3/s, and ``overrides`` in map.json set
the real value for a river by name (every override needs a ``source`` and a ``confidence``, like the correction
overlays). Where a river splits (a delta) its discharge is shared between the branches; where rivers meet, the river
below carries at least the sum of what flows in.

**Width and depth** follow the hydraulic-geometry rule ``width = a * Q**b`` and ``depth = c * Q**d``. The defaults
(width 7.2 Q^0.5, depth 0.27 Q^0.39) are the global fit of Andreadis et al. (2013, Water Resources Research); for the
Nile at 2,800 m3/s they give 380 m and 6 m, in line with the real river.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import shapefile
import shapely
from shapely.geometry import LineString, MultiLineString, Point, box
from shapely.ops import substring
from shapely.strtree import STRtree

from .cache import Source
from .corrections import RiverLine
from .riverdem import SnapParams

NATURAL_EARTH_DATASET = "natural-earth"
RIVERS_FILE = "ne_10m_rivers_lake_centerlines_scale_rank.zip"
LAKES_FILE = "ne_10m_lakes.zip"
_BASE_URL = "https://naciscdn.org/naturalearth/10m/physical/"
CONFIDENCES = ("low", "medium", "high")

# Mean discharge (m3/s) assumed for a Natural Earth scale rank: only a guess, overridden by map.json for known rivers.
DEFAULT_RANK_DISCHARGE = {1: 1000.0, 2: 400.0, 3: 200.0, 4: 100.0, 5: 50.0, 6: 25.0, 7: 12.0, 8: 6.0, 9: 3.0,
                          10: 1.5, 11: 0.7, 12: 0.3}


class RiverError(ValueError):
    """map.json's river settings, or the river data, are wrong; the message says which."""


def river_sources(pins: dict[str, dict] | None = None) -> list[Source]:
    """The Natural Earth downloads the rivers step needs (rivers and lakes)."""
    out = []
    for name in (RIVERS_FILE, LAKES_FILE):
        pin = (pins or {}).get(name, {})
        out.append(Source(NATURAL_EARTH_DATASET, name, _BASE_URL + name, pin.get("size"), pin.get("sha256")))
    return out


# --- Parameters -----------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class GeometryRule:
    """``a * Q ** b``: a river's width or depth from its discharge."""

    a: float
    b: float

    def __call__(self, discharge_m3s: float) -> float:
        return self.a * max(discharge_m3s, 0.0) ** self.b


@dataclass(frozen=True)
class FloodplainParams:
    """The floodplain of a reach is the ground within ``distance_m`` of the channel that lies less than ``height_m``
    above the water level there, connected to the channel."""

    distance_m: float = 3000.0
    height_m: float = 5.0
    cell_m: float = 64.0
    simplify_m: float = 64.0
    min_area_km2: float = 0.5


@dataclass(frozen=True)
class RiverOverride:
    """What is known about one river, by its Natural Earth name. Any of the three numbers may be left out."""

    name: str
    discharge_m3s: float | None
    width_m: float | None
    depth_m: float | None
    source: str
    confidence: str


@dataclass(frozen=True)
class RiverParams:
    min_discharge_m3s: float = 100.0
    rank_discharge: dict = field(default_factory=lambda: dict(DEFAULT_RANK_DISCHARGE))
    width: GeometryRule = GeometryRule(7.2, 0.5)
    depth: GeometryRule = GeometryRule(0.27, 0.39)
    min_width_m: float = 10.0
    min_depth_m: float = 1.0
    tolerance_detail_m: float = 30.0      # Douglas-Peucker tolerance inside the detail regions
    tolerance_elsewhere_m: float = 300.0  # and everywhere else
    junction_m: float = 600.0             # lines whose ends are this close (or an end this close to another line) meet
    # Water level = the lowest ground across the channel + this fraction of the depth. The Copernicus DEM is a
    # surface model that shows the flattened WATER SURFACE, so 0 is right for it; use 0.8 for a DEM of the bed.
    level_depth_fraction: float = 0.0
    snap: SnapParams = SnapParams()
    floodplain: FloodplainParams = FloodplainParams()
    overrides: dict = field(default_factory=dict)
    lakes_min_area_km2: float = 1.0
    lakes_simplify_m: float = 100.0

    @staticmethod
    def from_json(settings: dict | None) -> "RiverParams":
        """Reads the ``rivers`` object of map.json (all keys optional)."""
        if settings is None:
            return RiverParams()
        if not isinstance(settings, dict):
            raise RiverError("map.json: 'rivers' must be an object")
        known = {"min_discharge_m3s", "rank_discharge", "width", "depth", "min_width_m", "min_depth_m",
                 "tolerance_detail_m", "tolerance_elsewhere_m", "junction_m", "level_depth_fraction", "snap",
                 "floodplain", "overrides", "lakes"}
        unknown = sorted(set(settings) - known)
        if unknown:
            raise RiverError(f"map.json rivers: unknown setting(s) {', '.join(unknown)}")

        def number(table: dict, key: str, default: float, low: float | None = None, high: float | None = None,
                   where: str = "rivers") -> float:
            value = table.get(key, default)
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise RiverError(f"map.json {where}.{key} must be a number")
            if (low is not None and value < low) or (high is not None and value > high):
                raise RiverError(f"map.json {where}.{key} must be between {low} and {high}")
            return float(value)

        def rule(key: str, default: GeometryRule) -> GeometryRule:
            table = settings.get(key, {})
            if not isinstance(table, dict):
                raise RiverError(f"map.json rivers.{key} must be an object with a and b")
            return GeometryRule(number(table, "a", default.a, 0.0, None, f"rivers.{key}"),
                                number(table, "b", default.b, 0.0, 2.0, f"rivers.{key}"))

        rank_discharge = dict(DEFAULT_RANK_DISCHARGE)
        for rank, value in (settings.get("rank_discharge") or {}).items():
            if not str(rank).isdigit() or isinstance(value, bool) or not isinstance(value, (int, float)) or value <= 0:
                raise RiverError("map.json rivers.rank_discharge maps a rank number to a discharge above 0")
            rank_discharge[int(rank)] = float(value)

        snap_table = settings.get("snap", {})
        if not isinstance(snap_table, dict):
            raise RiverError("map.json rivers.snap must be an object")
        snap_defaults = SnapParams()
        snap = SnapParams(
            enabled=bool(snap_table.get("enabled", True)),
            radius_m=number(snap_table, "radius_m", snap_defaults.radius_m, 0.0, 20000.0, "rivers.snap"),
            step_m=number(snap_table, "step_m", snap_defaults.step_m, 5.0, 500.0, "rivers.snap"),
            half_width_m=number(snap_table, "half_width_m", snap_defaults.half_width_m, 0.0, 2000.0, "rivers.snap"),
            penalty_m_per_km=number(snap_table, "penalty_m_per_km", snap_defaults.penalty_m_per_km, 0.0, None,
                                    "rivers.snap"),
            smooth_vertices=int(number(snap_table, "smooth_vertices", snap_defaults.smooth_vertices, 1.0, 99.0,
                                       "rivers.snap")))

        plain_table = settings.get("floodplain", {})
        if not isinstance(plain_table, dict):
            raise RiverError("map.json rivers.floodplain must be an object")
        plain_defaults = FloodplainParams()
        floodplain = FloodplainParams(
            distance_m=number(plain_table, "distance_km", plain_defaults.distance_m / 1000.0, 0.0, 200.0,
                              "rivers.floodplain") * 1000.0,
            height_m=number(plain_table, "height_m", plain_defaults.height_m, 0.0, 200.0, "rivers.floodplain"),
            cell_m=number(plain_table, "cell_m", plain_defaults.cell_m, 8.0, 1000.0, "rivers.floodplain"),
            simplify_m=number(plain_table, "simplify_m", plain_defaults.simplify_m, 0.0, 1000.0,
                              "rivers.floodplain"),
            min_area_km2=number(plain_table, "min_area_km2", plain_defaults.min_area_km2, 0.0, None,
                                "rivers.floodplain"))

        lakes = settings.get("lakes", {})
        if not isinstance(lakes, dict):
            raise RiverError("map.json rivers.lakes must be an object")

        overrides = {}
        raw_overrides = settings.get("overrides", {})
        if not isinstance(raw_overrides, dict):
            raise RiverError("map.json rivers.overrides must be an object keyed by river name")
        for name, entry in raw_overrides.items():
            if not isinstance(entry, dict):
                raise RiverError(f"map.json rivers.overrides.{name} must be an object")
            for needed in ("source", "confidence"):
                if not isinstance(entry.get(needed), str) or not entry[needed]:
                    raise RiverError(f"map.json rivers.overrides.{name} needs a '{needed}' (where the numbers come from)")
            if entry["confidence"] not in CONFIDENCES:
                raise RiverError(f"map.json rivers.overrides.{name}.confidence must be one of {', '.join(CONFIDENCES)}")
            values = {}
            for key in ("discharge_m3s", "width_m", "depth_m"):
                values[key] = number(entry, key, 0.0, 0.0, None, f"rivers.overrides.{name}") if key in entry else None
                if values[key] is not None and values[key] <= 0:
                    raise RiverError(f"map.json rivers.overrides.{name}.{key} must be above 0")
            overrides[name] = RiverOverride(name, values["discharge_m3s"], values["width_m"], values["depth_m"],
                                            entry["source"], entry["confidence"])

        return RiverParams(
            min_discharge_m3s=number(settings, "min_discharge_m3s", 100.0, 0.0),
            rank_discharge=rank_discharge,
            width=rule("width", GeometryRule(7.2, 0.5)),
            depth=rule("depth", GeometryRule(0.27, 0.39)),
            min_width_m=number(settings, "min_width_m", 10.0, 1.0),
            min_depth_m=number(settings, "min_depth_m", 1.0, 0.1),
            tolerance_detail_m=number(settings, "tolerance_detail_m", 30.0, 0.0),
            tolerance_elsewhere_m=number(settings, "tolerance_elsewhere_m", 300.0, 0.0),
            junction_m=number(settings, "junction_m", 600.0, 1.0),
            level_depth_fraction=number(settings, "level_depth_fraction", 0.0, 0.0, 2.0),
            snap=snap, floodplain=floodplain, overrides=overrides,
            lakes_min_area_km2=number(lakes, "min_area_km2", 1.0, 0.0, None, "rivers.lakes"),
            lakes_simplify_m=number(lakes, "simplify_m", 100.0, 0.0, None, "rivers.lakes"))


# --- Natural Earth --------------------------------------------------------------------------------------------------


@dataclass
class NaturalEarthRiver:
    name: str
    rank: int
    ne_id: str
    parts: list[np.ndarray]  # each (n, 2) lon/lat, in the direction the river flows


def load_ne_rivers(zip_path: Path, bounds: tuple[float, float, float, float], margin_deg: float = 0.3
                   ) -> list[NaturalEarthRiver]:
    """The Natural Earth river lines that touch the bounds (plus a margin), whole, in file order. Lakes' center lines
    (``featurecla`` "Lake Centerline") are skipped: the lakes come from the lake polygons."""
    min_lon, min_lat, max_lon, max_lat = bounds
    window = (min_lon - margin_deg, min_lat - margin_deg, max_lon + margin_deg, max_lat + margin_deg)
    rivers: list[NaturalEarthRiver] = []
    try:
        with shapefile.Reader(str(zip_path)) as reader:
            for item in reader.iterShapeRecords(bbox=window):
                record = item.record.as_dict()
                if str(record.get("featurecla", "")).lower().startswith("lake"):
                    continue
                points = item.shape.points
                starts = list(item.shape.parts) + [len(points)]
                parts = [np.array(points[a:b], dtype=np.float64) for a, b in zip(starts[:-1], starts[1:]) if b - a >= 2]
                if not parts:
                    continue
                rank = record.get("scalerank")
                number = record.get("rivernum")
                name = str(record.get("name") or "").strip() or f"river {number}"
                rivers.append(NaturalEarthRiver(name, int(rank) if isinstance(rank, (int, float)) else 12,
                                                str(number), parts))
    except (OSError, shapefile.ShapefileException) as error:
        raise RiverError(f"{zip_path}: cannot read the river lines: {error}") from None
    return rivers


def slug(text: str) -> str:
    """A lower-case id fragment: letters, digits and underscores."""
    out = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return out or "river"


# --- Lines to reaches -----------------------------------------------------------------------------------------------


@dataclass
class Reach:
    """A stretch of river between junctions, running upstream to downstream."""

    key: int
    line_id: str
    name: str                # the Natural Earth name (or the correction's) of the line it came from
    points: np.ndarray       # (n, 2) world
    start_node: int = -1
    end_node: int = -1
    upstream: list = field(default_factory=list)    # keys of the reaches that flow into it
    downstream: list = field(default_factory=list)  # keys of the reaches it flows into
    own_discharge: float = 0.0
    explicit_discharge: bool = False
    fixed_width_m: float | None = None   # authored (a river_path correction or an override): used instead of the rule
    fixed_depth_m: float | None = None
    discharge_m3s: float = 0.0
    order: int = 1
    width_m: float = 0.0
    depth_m: float = 0.0
    levels: np.ndarray | None = None
    floodplain: list = field(default_factory=list)


def _cut_distances(line: LineString, others: list[tuple[int, LineString]], index: int,
                   junction_m: float) -> list[float]:
    """Where other rivers' ends touch the interior of this line: distances along it at which to cut it."""
    cuts: list[float] = []
    for other_index, other in others:
        if other_index == index:
            continue
        for end in (Point(other.coords[0]), Point(other.coords[-1])):
            if line.distance(end) > junction_m:
                continue
            along = line.project(end)
            if along <= junction_m or along >= line.length - junction_m:
                continue  # at (or near) an end of this line: an end-to-end junction, no cut needed
            cuts.append(along)
    cuts.sort()
    merged: list[float] = []
    for cut in cuts:
        if not merged or cut - merged[-1] > junction_m:
            merged.append(cut)
    return merged


def build_reaches(lines: list[RiverLine], junction_m: float) -> list[Reach]:
    """Splits the lines where other lines join or leave them, and links the pieces into a flow graph.

    Natural Earth draws a confluence or a delta as lines whose ends touch (or nearly touch) the middle of another line.
    Every such touch cuts the other line there; ends that are close together form one junction ("node"). A piece that
    ends at a node flows into every piece that starts there."""
    lines = [line for line in lines if LineString(line.points).length >= 1.5 * junction_m]  # shorter ones cannot hold a junction at each end
    if not lines:
        return []
    shapes = [LineString(line.points) for line in lines]
    indexed = list(enumerate(shapes))
    reaches: list[Reach] = []
    for index, (line, geometry) in enumerate(zip(lines, shapes)):
        cuts = _cut_distances(geometry, indexed, index, junction_m)
        bounds = [0.0, *cuts, geometry.length]
        for piece_number, (a, b) in enumerate(zip(bounds[:-1], bounds[1:])):
            part = substring(geometry, a, b) if (a > 0.0 or b < geometry.length) else geometry
            coordinates = np.array(part.coords)
            if len(coordinates) < 2 or part.length == 0.0:
                continue
            reaches.append(Reach(len(reaches), f"{line.id}.{piece_number}", line.name, coordinates))

    # Nodes: cluster the ends of all pieces that are within the junction distance of each other.
    ends = [(reach.key, side, Point(reach.points[0 if side == 0 else -1])) for reach in reaches for side in (0, 1)]
    end_tree = STRtree([e[2] for e in ends])
    parent = list(range(len(ends)))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i, (_, _, point) in enumerate(ends):
        for j in end_tree.query(point.buffer(junction_m)):
            if int(j) != i and point.distance(ends[int(j)][2]) <= junction_m:
                parent[find(i)] = find(int(j))
    node_of: dict[int, int] = {}
    members: dict[int, list[int]] = {}
    for i in range(len(ends)):
        members.setdefault(find(i), []).append(i)
    for node_number, root in enumerate(sorted(members)):
        for i in members[root]:
            node_of[i] = node_number
        centre = np.mean([ends[i][2].coords[0] for i in members[root]], axis=0)
        for i in members[root]:
            key, side, _ = ends[i]
            reaches[key].points[0 if side == 0 else -1] = centre
    for i, (key, side, _) in enumerate(ends):
        if side == 0:
            reaches[key].start_node = node_of[i]
        else:
            reaches[key].end_node = node_of[i]

    starting_at: dict[int, list[int]] = {}
    for reach in reaches:
        starting_at.setdefault(reach.start_node, []).append(reach.key)
    for reach in reaches:
        for other in starting_at.get(reach.end_node, []):
            if other != reach.key:
                reach.downstream.append(other)
                reaches[other].upstream.append(reach.key)
    return reaches


def topological_order(reaches: list[Reach]) -> list[int]:
    """Reach keys with every reach after the ones that flow into it. A loop (bad data) is cut: the reaches still
    waiting are appended in key order and the link that closed the loop is ignored by callers using this order."""
    waiting = {r.key: len(r.upstream) for r in reaches}
    ready = sorted(k for k, n in waiting.items() if n == 0)
    order: list[int] = []
    while ready:
        key = ready.pop(0)
        order.append(key)
        for nxt in reaches[key].downstream:
            waiting[nxt] -= 1
            if waiting[nxt] == 0:
                ready.append(nxt)
        ready.sort()
    if len(order) < len(reaches):
        done = set(order)
        order.extend(sorted(r.key for r in reaches if r.key not in done))
    return order


def assign_discharge(reaches: list[Reach]) -> None:
    """``discharge_m3s`` of every reach from its own estimate and what flows in (see the module docs)."""
    order = topological_order(reaches)
    position = {key: i for i, key in enumerate(order)}
    for key in order:
        reach = reaches[key]
        inflow = sum(reaches[u].discharge_m3s / max(len(reaches[u].downstream), 1)
                     for u in reach.upstream if position[u] < position[key])
        reach.discharge_m3s = reach.own_discharge if reach.explicit_discharge else max(reach.own_discharge, inflow)


def assign_orders(reaches: list[Reach]) -> None:
    """Strahler stream order: 1 at a source; where two or more reaches of the highest order meet, one more; a branch
    keeps the order of the reach it leaves."""
    order = topological_order(reaches)
    position = {key: i for i, key in enumerate(order)}
    for key in order:
        reach = reaches[key]
        upstream = [reaches[u].order for u in reach.upstream if position[u] < position[key]]
        if not upstream:
            reach.order = 1
        else:
            top = max(upstream)
            reach.order = top + 1 if upstream.count(top) >= 2 else top


def assign_width_depth(reaches: list[Reach], params: RiverParams) -> None:
    """Width and depth of every reach from its discharge, or from the override for its river."""
    for reach in reaches:
        override = params.overrides.get(reach.name)
        width = reach.fixed_width_m or (override.width_m if override and override.width_m else None)
        depth = reach.fixed_depth_m or (override.depth_m if override and override.depth_m else None)
        reach.width_m = width if width else max(params.width(reach.discharge_m3s), params.min_width_m)
        reach.depth_m = depth if depth else max(params.depth(reach.discharge_m3s), params.min_depth_m)


def simplify_line(points: np.ndarray, detail_area, tolerance_detail_m: float, tolerance_elsewhere_m: float
                  ) -> np.ndarray:
    """Douglas-Peucker with a fine tolerance inside ``detail_area`` (a shapely geometry or None) and a coarse one
    elsewhere. The two ends are kept, so reaches stay joined."""
    line = LineString(points)
    if detail_area is None or detail_area.is_empty:
        return np.array(line.simplify(tolerance_elsewhere_m, preserve_topology=False).coords)
    pieces = []
    for geometry, tolerance in ((line.intersection(detail_area), tolerance_detail_m),
                                (line.difference(detail_area), tolerance_elsewhere_m)):
        for part in shapely.get_parts(geometry):
            if isinstance(part, LineString) and not part.is_empty and part.length > 0:
                pieces.append(part.simplify(tolerance, preserve_topology=False))
    if not pieces:
        return np.array(line.simplify(tolerance_elsewhere_m, preserve_topology=False).coords)
    ordered = []
    for piece in pieces:
        start, end = line.project(Point(piece.coords[0])), line.project(Point(piece.coords[-1]))
        coords = list(piece.coords) if start <= end else list(piece.coords)[::-1]
        ordered.append((min(start, end), coords))
    ordered.sort(key=lambda item: item[0])
    merged: list[tuple[float, float]] = []
    for _, coords in ordered:
        for point in coords:
            if not merged or (point[0], point[1]) != merged[-1]:
                merged.append((point[0], point[1]))
    out = np.array(merged)
    out[0], out[-1] = points[0], points[-1]  # the ends stay exactly where the junctions are
    return out


def group_rivers(reaches: list[Reach]) -> list[list[int]]:
    """The reaches grouped into rivers: each group is everything connected by flow (a trunk with its tributaries and
    delta branches), in reach-key order."""
    parent = {r.key: r.key for r in reaches}

    def find(k: int) -> int:
        while parent[k] != k:
            parent[k] = parent[parent[k]]
            k = parent[k]
        return k

    for reach in reaches:
        for other in reach.downstream:
            parent[find(reach.key)] = find(other)
    groups: dict[int, list[int]] = {}
    for reach in reaches:
        groups.setdefault(find(reach.key), []).append(reach.key)
    return sorted(groups.values(), key=lambda g: g[0])


def river_documents(reaches: list[Reach], groups: list[list[int]]) -> list[dict]:
    """The ``rivers`` array of rivers.json (schema: game/content/README.md)."""
    rivers = []
    used_ids: set[str] = set()
    used_names: set[str] = set()
    for group in groups:
        members = [reaches[k] for k in group]
        main = max(members, key=lambda r: (r.discharge_m3s, len(r.points)))
        base = slug(main.name)
        river_id = base
        n = 2
        while river_id in used_ids:
            river_id, n = f"{base}_{n}", n + 1
        used_ids.add(river_id)
        name, copy = main.name, 2
        while name in used_names:
            name, copy = f"{main.name} ({copy})", copy + 1
        used_names.add(name)
        reach_ids: dict[int, str] = {}
        seen: dict[str, int] = {}
        for reach in members:
            stem = slug(reach.name)
            seen[stem] = seen.get(stem, 0) + 1
            reach_ids[reach.key] = f"{stem}_{seen[stem]}"
        documents = []
        for reach in members:
            vertices = []
            for (x, z), level in zip(reach.points, reach.levels):
                vertices.append({"x": round(float(x), 2), "z": round(float(z), 2), "width_m": round(reach.width_m, 2),
                                 "depth_m": round(reach.depth_m, 2), "water_level_m": round(float(level), 2)})
            documents.append({
                "id": reach_ids[reach.key], "name": reach.name, "order": reach.order,
                "discharge_m3s": round(reach.discharge_m3s, 2),
                "downstream": [reach_ids[d] for d in reach.downstream if d in reach_ids],
                "vertices": vertices,
                "floodplain": [[[round(float(x), 1), round(float(z), 1)] for x, z in ring] for ring in reach.floodplain],
            })
        rivers.append({"id": river_id, "name": name, "reaches": documents})
    return rivers


@dataclass
class LineInfo:
    """What the pipeline remembers about a river line besides its shape."""

    rank: int | None              # Natural Earth scale rank; None for an authored line
    own_discharge: float          # m3s: from an override, the rank table, or an authored width
    explicit_discharge: bool      # a known value (not a rank guess)
    fixed_width_m: float | None = None
    fixed_depth_m: float | None = None


def ne_to_river_lines(rivers: list[NaturalEarthRiver], to_world, world_size: tuple[float, float], params: RiverParams,
                      margin_m: float = 5000.0) -> tuple[list[RiverLine], dict[str, LineInfo]]:
    """The Natural Earth lines as ``RiverLine`` objects in the world frame, cut to the map's box (plus a margin), with
    an estimated discharge for each. The corrections engine works on ``RiverLine``s, so the river corrections can be
    applied to this list; ``LineInfo`` (keyed by the line's id) keeps what the engine does not know about."""
    world_box = box(-margin_m, -margin_m, world_size[0] + margin_m, world_size[1] + margin_m)
    lines: list[RiverLine] = []
    info: dict[str, LineInfo] = {}
    for river in rivers:
        override = params.overrides.get(river.name)
        if override and override.discharge_m3s:
            discharge, explicit = override.discharge_m3s, True
        else:
            discharge = params.rank_discharge.get(river.rank, params.rank_discharge[max(params.rank_discharge)])
            explicit = False
        for part_number, part in enumerate(river.parts):
            world = np.array([to_world(lon, lat) for lon, lat in part])
            clipped = LineString(world).intersection(world_box)
            for piece_number, piece in enumerate(p for p in shapely.get_parts(clipped)
                                                  if isinstance(p, LineString) and not p.is_empty):
                points = np.array(piece.coords)
                if len(points) < 2:
                    continue
                line_id = f"ne{river.ne_id}_{part_number}_{piece_number}"
                width = max(params.width(discharge), params.min_width_m)
                depth = max(params.depth(discharge), params.min_depth_m)
                lines.append(RiverLine(line_id, river.name, points, np.full(len(points), width),
                                       np.full(len(points), depth)))
                info[line_id] = LineInfo(river.rank, discharge, explicit)
    return lines, info


def authored_info(line: RiverLine, params: RiverParams) -> LineInfo:
    """Info for a line a ``river_path`` correction added: its discharge follows from its width (the rule inverted)."""
    width = float(np.mean(line.width_m))
    depth = float(np.mean(line.depth_m))
    discharge = (width / params.width.a) ** (1.0 / params.width.b) if params.width.a > 0 and params.width.b > 0 else 1.0
    return LineInfo(None, max(discharge, 0.1), True, width, depth)

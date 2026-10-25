# Ancient Near East: historical corrections (about 3000 BCE)

"Light corrections" (GDD §13): undo the most visible modern works and add the best-known ancient water features.
The overlay engine is `tools/terrain/terrain/corrections.py` (architecture §15.7). Files are applied in name order;
`map.json` (written in T1.2.6.1) must list them as `"corrections": ["corrections/01_modern_works.geojson", ...]`.

## Read this first: how far to trust these files

- **Everything here was authored without the real elevation data.** The cloud environment cannot download the
  Copernicus DEM or the Natural Earth data (the network proxy blocks them), so outlines and coordinates are written
  **from memory** and are approximate. No outline has been laid over a hillshade yet.
- **The references were also cited from memory.** The author, year and journal of each are believed correct but were
  **not looked up**. Check them before quoting them anywhere.
- What *was* tested: every file parses, every feature has a `source` and a `confidence`, all coordinates lie inside
  lon 24-50 E, lat 21-38.5 N, and each feature changes something when applied to a flat raster
  (`tools/terrain/tests/test_ancient_near_east_corrections.py`). That shows the syntax is right, not that the
  geography is.
- Rule used: when unsure, leave the modern terrain. Several items from the Task list were deliberately **not** done
  (see the end).

## What is corrected

### 01_modern_works.geojson

| # | Feature | Op | Why | Source | Confidence |
|---|---|---|---|---|---|
| 0-2 | Suez Canal in three strips (Port Said to Lake Timsah, Timsah to Great Bitter Lake, Little Bitter Lake to Suez) | `set_land`, min 2 m, ~900 m wide | The canal (1869) joins the Mediterranean and Red Sea; in 3000 BCE the isthmus was land (the Gulf of Suez reached further north than today, but no through-channel existed). | Course read off modern maps. | medium |
| 3 | Lake Nasser | `fill_reservoir`, valley from ~123 m (Wadi Halfa) down to ~88 m (Aswan), 25 m bank rise, 500 m x 8 m river channel | The reservoir (1970) drowns the Nile valley in Lower Nubia; the DEM shows a flat lake at ~182 m. | Outline: shape on modern maps, main body only. Elevations: approximate pre-dam low-water levels, from memory. | medium |

**Designer should check**
- Do the Suez strips line up with the canal on the hillshade? Each strip is only ±0.004° wide.
- Is the Lake Nasser outline covering the whole flat lake surface, including the wide Toshka and Kalabsha parts, and
  is nothing outside it flattened by mistake? The side arms (khors) are not in the outline, so shoreline fingers may
  remain as water pockets.
- Is the valley floor believable: a 500 m wide, 8 m deep channel falling ~35 m over ~300 km? Do the 25 m banks look
  like the cliffs of the Nubian valley, not a bowl?

### 02_ancient_nile_delta.geojson

Six `river_path` features, all from the Cairo / Cercasorus apex (~31.25 E, 30.15 N) (Bolbitine and Canopic start a
little west of it):

| Branch | Ends near | Confidence |
|---|---|---|
| Pelusiac | Pelusium (32.55 E, 31.05 N) | low |
| Tanitic (past Tanis) | 31.95 E, 31.50 N | low |
| Mendesian (past Mendes) | 31.77 E, 31.50 N | low |
| Sebennytic (past Sebennytos) | 31.00 E, 31.52 N | low |
| Bolbitine (today's Rosetta branch) | 30.40 E, 31.40 N | medium |
| Canopic | Abu Qir / Canopus (30.07 E, 31.32 N) | low |

Width 250-300 m narrowing ~30 % toward the sea; depth 4-5 m. Source: the classical seven-branch description
(Herodotus, Strabo, Ptolemy) and reconstructions such as Stanley & Warne (1993, *Science* 260), Said (1993, *The
River Nile*) and Bunbury & Lutley (2008, *Antiquity*): cited from memory, not re-read. The lines are hand-drawn.

The modern Damietta branch is **not** removed: it follows roughly the old Phatnitic course, and the sources are too
loose to justify cutting the modern Nile. The modern Rosetta branch is kept as the Bolbitine.

**Designer should check**
- Do the lines run along the flattest, lowest ground of the delta, or do they cut across modern embankments?
  (The rivers step snaps rivers to the DEM, so small errors may self-correct.)
- Is the Nile delta busy enough with six extra rivers plus the modern branches and canals the DEM carries? If it is
  too busy, drop the least certain: Tanitic or Mendesian.
- Where a branch ends in the modern Manzala / Burullus lagoon, is that acceptable?

### 03_lakes_and_coasts.geojson

| # | Feature | Op | Why | Source | Confidence |
|---|---|---|---|---|---|
| 0 | Lake Moeris, Faiyum | `set_water`, level +10 m, bed 3 m | The Faiyum lake stood far higher than today's Birket Qarun (-43 m). Shorelines near +20 m are dated to the Middle Kingdom; the level in 3000 BCE is uncertain, so +10 m is a hedge. | Caton-Thompson & Gardner (1934) and Hassan (1986), cited from memory. | low |
| 1 | Head of the Persian Gulf | `set_water`, level 0 m | At the mid-Holocene highstand the Gulf reached well inland of today's Shatt al-Arab mouth. Estimates differ by tens of kilometers; this is a conservative strip around the lower Shatt al-Arab only (≈ 30.0-30.4 N, 47.8-48.8 E). | Kennett & Kennett (2006); Sanlaville & Dalongeville (2005); Pournelle (2003), from memory. | low |

**Designer should check**
- Moeris: does the polygon sit inside the Faiyum depression? Anything of the oasis above +10 m inside the polygon is
  cut down to 7 m and flooded, so a loose outline eats farmland. Is a +10 m lake the right look: a big lake, not a
  puddle? You may prefer +20 m.
- Gulf head: set_water ignores elevation and floods the whole polygon. Is anything high (e.g. Warba / Bubiyan
  islands, the Kuwait side) flooded by mistake? Are Ur, Eridu and Basra (far north-west, outside the polygon)
  still dry land? Whether to push the line further inland is a historical call for you.

## Not done on purpose (so you can decide)

| Task item | Why it was left out |
|---|---|
| Sweet Water / Ismailia canals | 30-50 m wide: below the 1 km region cells and mostly below the 32 m detail cells. Nothing the engine can remove without damaging the fields beside them. |
| Toshka lakes and spillway | A 1998 flood plain that has since mostly dried up. Without the real DEM I cannot tell what the surface is, and a wrong guess puts a hole in the Western Desert. Check on the PC hillshade. |
| Euphrates reservoirs (Atatürk, Keban, Karakaya, Birecik, Tabqa / Lake Assad, Haditha), Tigris reservoirs (Mosul, Dukan, Darbandikhan, Hamrin), Lake Tharthar | I could not place their outlines accurately from memory, and a bad `fill_reservoir` polygon carves a valley into whatever land it covers. Best done on the PC: take the polygons from Natural Earth `ne_10m_lakes` (the file the rivers step already caches) by name, then add `fill_reservoir` features. Keban and Karakaya are also close to the map's northern edge. |
| Marsh drainage in southern Iraq | The engine has no wetness override, and a `set_water` over the marshes would remove their land. Needs a decision (a wetness override in the layers step, or leave the modern state) before authoring. |
| Nile delta coast south of today's | No source I can cite with confidence for 3000 BCE. |
| Removing the modern Rosetta / Damietta dominance | See above: kept. |

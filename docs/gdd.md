# City Builder — Game Design Document (Living)

> Status: **Discovery in progress.** This document records the designer's answers verbatim (or near-verbatim)
> plus derived design implications. Sections marked _OPEN_ have not been answered yet.

## Discovery Log

| Round | Topic | Status |
|-------|-------|--------|
| 1 | Vision & Identity | In progress (Q1–Q2 answered, plus follow-up on other influences; Q3–Q16 open) |

---

## 1. Vision & Identity

### 1.1 Core Idea (Q1)

**Designer's answer:**
The playstyle should feel *familiar* to players of Cities: Skylines and SimCity, the same type of game
but with more depth. The city should feel **living**, with **real people** and **real consequences** for the player's
choices that affect the city and region. It should not feel like an isolated city: the city interacts with the
region, and preferably the **player can build multiple cities that interact with each other**.

**Derived pillars (draft):**
1. **Familiar foundation.** Zoning, roads, utilities, and services work much like CS/SimCity, so the learning curve is low.
2. **A living city.** Citizens feel like real people, and their lives visibly respond to the world.
3. **Consequences.** Player decisions ripple outward over time and are hard to "undo."
4. **Regional play.** There are multiple player-built cities that interact (trade, migration, commuting, politics) within a shared region.

### 1.2 Influences (Q2)

| Game | What the designer likes | What it lacks / what to improve |
|------|-------------------------|---------------------------------|
| Cities: Skylines 1 & 2 | Wide creative range in world building; balancing city needs and resources | Shallow **governmental** aspects; little **regional / multi-city** play; no **natural emergence of cultural, economic, and demographic districts**; CS2's companies, routes, transport changes, and individual citizen info "only go so far" |
| Civilization | Experiencing different **eras**; good **player and NPC interactions**; **tech and culture trees**; **progression through time**; the feeling of continuously "upgrading" cities and civilizations; **trade routes** and interactions between cities of all types | It's **turn-based**, which is not desired |
| Crusader Kings III | Its **time system** (real-time with pause and speed controls); **power dynamics** and **RPG elements**; **progression trees**; **era / time progression**; **skills and traits** | — |
| Songs of Syx | A **larger map view** (world/region layer above the city); **granular creativity** in structure building and landscaping; feels like a **living city**, where you can tell what each citizen is doing and where they're going | — |
| RimWorld | A **larger map view**; **granular creativity** in structure building and landscaping; feels like a **living city**, where each pawn's activity and destination is legible | — |
| Minecraft | **Granular creativity** in structure building and landscaping | — |

**Derived design implications:**
- **Time model:** Real-time with pause and multiple speeds (CS / CK3 style). Not turn-based.
- **Government depth** is a headline system: policies, laws, administration, possibly politics and factions.
- **Emergent neighborhoods:** Cultural, economic, and demographic identity should *emerge* from the simulation, not be painted on by the player.
- **Deeper economy than CS2:** Companies, supply chains, routes, and logistics with more fidelity.
- **Deeper citizens than CS2:** Individual life histories, needs, opinions, and relationships.
- **Era progression (Civ-inspired):** The game progresses through eras, with tech and culture trees, and cities continuously "upgrade" over time.
- **Inter-city interaction:** Trade routes, diplomacy or relations, and possibly NPC-controlled cities as well as player cities.
- **Power dynamics and RPG layer (CK3-inspired):** Characters with **skills and traits** (officials, notable citizens, rival leaders?) and political power struggles.
- **Two-layer map:** a **region / world map view** above the detailed city view (Songs of Syx / RimWorld style).
- **Custom building construction:** Players can **customize the structure of buildings**, **build from scratch**, or use an **assisted build** mode. This needs a modular or voxel-like building system, not only fixed prefab assets. It's a major technical driver.
- **Granular landscaping:** Fine-grained terrain shaping beyond CS-style terraforming.
- **Legible citizens:** At any zoom, the player can see what an individual is doing and where they're going (activity states, destinations, visible agents). This implies **agent-based** citizen simulation, at least for visible or notable citizens.

### 1.3 Remaining Round 1 Questions — _OPEN_
3. Player fantasy / role (mayor, governor, ruler, god-hand, etc.)
4. Setting and time period. Does era progression span history (ancient → modern → future)?
5. Tone
6. Realism vs. stylized
7. Fail states vs. sandbox
8. Scenarios, campaign, narrative
9. Session length and the lifespan of a save
10. Target audience and difficulty
11. Peak city size
12. Individual vs. aggregate citizen simulation
13. Camera and visual style
14. Target platforms
15. Single-player vs. multiplayer
16. Commercial vs. hobby vs. open source; mod support

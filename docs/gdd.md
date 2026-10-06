# City Builder — Game Design Document (Living)

> Status: **Discovery in progress.** This document records the designer's answers verbatim (or near-verbatim)
> plus derived design implications. Sections marked _OPEN_ have not been answered yet.

## Discovery Log

| Round | Topic | Status |
|-------|-------|--------|
| 1 | Vision & Identity | **Complete** |
| 2 | Core Gameplay Loop & City Building | **Complete** (rank follow-ups open) |
| 3 | Citizens & Society | In progress |

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

### 1.3 Player Role (Q3)

**Designer's answers:**
- **3a.** The player holds **an office or position**, not a family dynasty. At any moment the player is also **a specific person** in that office, with traits and skills. **Succession** follows the mechanism that fits the era and government (politics, tribal selection, appointment, election, and so on). It is dynastic only when that makes sense for the era. Succession matters less than in CK3.
- **3b.** The **government form is chosen freely** from the options the era allows. **Citizens can force change** through revolutions, reform movements, social activism, civil rights movements, militia mobilization, rebellions, and so on.
- **3c.** Undecided. The designer asked for a recommendation. **Recommendation accepted** (below).

**Model: "The Office persists; the officeholder does not."**
- The player controls **the Office** (the seat of power of their realm). Its title, powers, and constraints come from the current **Government Form**.
- One person holds the Office at a time. They have **traits, skills, ideology, and a faction backing**, and they age, can be removed, and can die.
- The game has one government system that does not depend on era: **Government Form → Offices → Factions → Legitimacy → Policies and Laws**. Each era *unlocks or removes entries* (data, not new code).
- Government change can be **player-initiated** (reform, within era limits) or **citizen-forced** (reform movements, activism, civil rights movements, militia uprisings, revolutions, coups). These come from faction and citizen discontent in the simulation.

**3c recommendation: losing power is a hard transition, not game over.**
- When the officeholder is ousted by an election, a coup, or a revolution, the **player keeps playing as the new officeholder**. That person's traits, ideology, and backing faction are **not chosen by the player**.
- Consequences carry over: the new holder arrives with a **mandate or agenda** from the faction that put them in power. That can mean **forced policy reversals**, a legitimacy reset, purges of officials, and possibly **cities seceding or changing allegiance**.
- The player's "score" is the long-term state of the realm, not one leader's survival. This fits the *consequences* pillar.
- **Game over** happens only if the **realm ceases to exist**, meaning all cities are lost, conquered, or abandoned.

### 1.4 Setting & Eras (Q4)

**Designer's answers:**
- **4a.** Start with **three main eras**: **Ancient**, **Colonial**, and **20th-Century Modern (≈1900–1960)**.
- **4b.** **Real Earth**: real technologies, cultures, and territories. **Alternate history emerges** through play.
- **4c.** All eras eventually lead to the **Utopian Era**, the final era. It spans **present day → 2100** and **continues indefinitely** after 2100 as the endgame.

**Era roadmap (draft):**

| # | Era | Approx. span | Status |
|---|-----|--------------|--------|
| 1 | Ancient | _TBD_ | Launch era |
| 2 | Colonial | _TBD_ (≈1500s–1800s?) | Launch era |
| 3 | 20th-Century Modern | 1900–1960 | Launch era |
| 4 | Utopian | Present day → 2100 → indefinite | Final era, **after the first three are complete** |
| — | Classical, Medieval, Industrial, Late Modern, … | fill the gaps | **Post-launch** expansion eras |

**Design implications:**
- The engine **must not depend on era**. Eras, government forms, technologies, buildings, and cultures are all **data**.
- **Modular building parts** per era (they tie into custom building construction) keep the per-era asset cost manageable.
- **Era transitions inside a living city** (old districts that persist, renovation, demolition, historic preservation) are a core feature, not a reset.
- **4d.** Gaps between eras (e.g., Classical, Medieval, Late Modern) are **filled with more eras after launch**. The era system must allow inserting eras between existing ones.
- **4e. Eras progress per realm**, not per city and not globally. Realms in different eras **coexist** in the same region (e.g., colonial settlements beside tribes still in the Ancient era). The player's era advances through **tech and culture progress**. **NPC realms progress as the game calendar passes**. The player can **compare their progress** with other realms (falling behind or far ahead).
- **4f.** The Utopian Era ships **after** the first three eras are complete.
- **4g.** The Utopian Era **can go dystopian** depending on choices. _Details deferred._
- **4h. Real places with real terrain data** for the region map. Example starts: **Nile Delta** (Ancient), **Chesapeake Bay** (Colonial), **Chicago** (Modern).
- **4i. Real cultures and civilizations**, picked from a set at game start. Culture **shapes buildings, government options, and citizens**. Cultures **mix and evolve** inside cities over time and **localize into neighborhoods** (they feed the emergent-district system).

**Further implications:**
- A **real-world terrain data pipeline** is needed (elevation, water, possibly climate and soil) to produce playable region maps.
- **NPC realms** are first-class: they run their own era and tech progression, and they need AI, diplomacy, trade, and conflict with the player's realm.
- A **realm comparison / progress view** is needed (an era and tech standing relative to other realms).
- **Culture is a simulation value carried by citizens**, not just a building skin. It drives building styles, neighborhood identity, and cultural blending.

**Game starts & map scope (4j–4m):**
- **4j. Start modes, CK3-style:** both a **full playthrough from the Ancient era** and **era-specific bookmark starts** (e.g., Chesapeake Bay 1607, Chicago 1900).
- **4k.** Only **historically appropriate cultures** for the chosen location and era (for now; it may be relaxed later).
- **4l. Map size: continental / macro-region scale** to start. The globe may come later.
  - Ancient → **Middle East**
  - Colonial → **North America**
  - Modern → **U.S. Midwest**
- **4m.** **Native peoples** start as full **NPC realms** with cities, government, and diplomacy. **Sensitive histories are included** (displacement, conflict, treaties, colonization) and portrayed in a serious, grounded way.

- **4n.** **Full playthroughs stay on the same map through every era.** A Nile Delta realm reaches the Colonial and Modern eras **in the Middle East**. Every launch map therefore needs content for **all launch eras**, not just its starting era.
- **4o.** Realms on bookmark starts *can* in principle meet other regions and civilizations. **Every realm starts at the bookmark date** (e.g., all of them in 1900), possibly with **simulated pre-history** to give them a plausible starting state.
- **4p.** **Colonial start:** play as **native nations or colonial powers**. **Ancient start:** play as one of **several Middle Eastern civilizations**.
- **4q.** The Ancient map covers **Egypt → Mesopotamia** (the Fertile Crescent) to start. The **full game scales to the entire Earth**.

**Technical implication of 4l:** real terrain at continental scale needs **multiple resolutions**. A coarse realm or region map is built from real elevation and hydrology data. Detailed **city-scale terrain is generated on demand** for each city site from real data plus procedural detail.

## 2. Tone & Realism

### 2.1 Tone (Q5)
**Serious and grounded** (CK3-like). Not cozy, not satirical.

### 2.2 Realism (Q6)
- **Economics is active** (money, prices, and budgets matter), **traffic is a system the player must maintain**, and **zoning rules apply**.
- **The micro layer is the most realistic:** individual citizens, buildings, roads, zones, neighborhoods.
- **The macro layer is simplified:** the wider economy, trade, and inter-realm systems use abstracted models.
- **Design principle:** *simulate in detail what the player can see and touch; abstract what they can only read about.*

## 3. Structure, Audience & Platform

### 3.1 Fail State (Q7)
**The realm ceasing to exist is the only loss.** Bankruptcy, annexation of cities, losing office, and so on are setbacks, not losses.

### 3.2 Narrative & Goals (Q8)
- **Scripted historical events** are woven together with **emergent simulation events** (CK3-style event chains with triggers and conditions).
- **Open-ended play.** There are no victory conditions.

### 3.3 Session Length & Time Cadence (Q9)
- Replay value like **Cities: Skylines or Civilization**. The game should work for a **30-minute session** as well as **sessions many hours long**.
- **Era-specific time cadence:** game time runs at a different rate in each era. _Proposed defaults below, pending tuning._

| Era | Proposed time at speed 1 | Rationale |
|-----|--------------------------|-----------|
| Ancient | 1 game year ≈ 2 real minutes | Thousands of years of history; slow technological change |
| Colonial | 1 game year ≈ 5 real minutes | Roughly 300 years of history |
| Modern (1900–1960) | 1 game year ≈ 12 real minutes | 60 years of dense change; city-scale detail dominates |
| Utopian | 1 game year ≈ 15 real minutes | Endgame; time is open-ended |

- **Design note:** the **calendar** (years, seasons) is separate from the **citizen daily cycle** (day/night, commutes, work shifts). The daily cycle runs at a speed that reads well visually. The calendar advances at the era cadence. Citizens' lives "skip" days at a statistical level when the calendar outpaces the visual day.

### 3.4 Audience & Onboarding (Q10)
- Aimed at **medium to hardcore sim players**.
- **Difficulty settings** make the game easier or harder.
- Onboarding priorities: **1) in-game encyclopedia (must have)** · **2) tooltips (second)** · **3) advisors (fringe goal)**.

## 4. Scale & Simulation

### 4.1 City Size (Q11)
- **No hard population caps.** Each era has **soft "living thresholds"**: growth gets harder past them, and **bonuses, stats, and city or realm progress** raise them. A city outgrowing its era's carrying capacity is a challenge the player has to manage.

### 4.2 Simulation Level of Detail (Q12) — confirmed hybrid
- **Viewed or active city:** every citizen is fully simulated as a visible individual agent.
- **Other cities:** statistical or aggregate simulation until the player zooms in. Detail is then re-hydrated from the aggregates.
- **Notable people** (officeholders, officials, faction leaders, rivals, notable citizens): **always simulated as individuals**, wherever they are.

## 5. Presentation & Platform

### 5.1 Camera & Art (Q13)
- **Full 3D with a free camera.**
- The art style is still flexible. **Prototype realistic first**, then stylized and low-poly for comparison.

### 5.2 Platform (Q14)
**PC only** to start.

### 5.3 Multiplayer (Q15)
**Single-player only.** Multiplayer is a possible future addition. _Architecture note: keep the simulation deterministic and command-driven where practical, so multiplayer isn't ruled out later._

### 5.4 Business & Modding (Q16)
- Goal: **sell on Steam** eventually.
- **Mod support is a strong plus.** Data-driven content (eras, cultures, buildings, events, governments) and an eventual Steam Workshop integration.

## 7. Core Gameplay Loop & City Building

### 7.1 Zoning & Placement (Q17)
- **CS-style zoning** painted onto the terrain. Zoned buildings grow on their own.
- **Ploppable buildings** for civic, religious, government, and service buildings.
- **Decorations and visual props** can be placed freely.

### 7.2 Custom Buildings (Q18)
- The game **ships with every building required for gameplay**. Custom building is **optional visual customization** and never a requirement.
- All of these are supported:
  - (a) Players design **templates** that the city then builds.
  - (b) Players **hand-build** individual landmark buildings.
  - (c) Citizens build **culturally styled** homes, which the player can edit.

### 7.3 Construction & Logistics (Q19–Q20)
- **Construction is physical** (Songs of Syx / Manor Lords style). Workers and materials are hauled to the site, and the building rises over time.
- **Construction and goods share one logistics system.**
- **Inside a city:** goods **move physically** (haulers, carts, trucks).
- **Between cities:** goods move as **generalized flows** along trade routes. This follows the micro-realistic / macro-simplified rule.

### 7.4 Roads (Q21)
- **Free-form and grid** road tools.
- **Early eras:** **desire paths form where people actually walk**, wear down over time, and can then be **upgraded** into formal roads.

### 7.5 Terrain & Landscaping (Q22)
- Terraforming, decorations, and aesthetic items are **placed instantly and paid for upfront** (unlike buildings).

### 7.6 Land Ownership (Q23)
- **Citizens and companies own land and buildings.**
- **Private property can block the player.** Use **eminent domain** to seize it, with political and cost consequences.
- **Protected land** also exists: **natural preserves**, **Indigenous lands**, and other legal protections.

### 7.7 Founding Cities (Q24)
- Cities are founded by **sending settlers**, or **appear on their own** through migration.
- **Limit: 10 player-controlled cities per realm.** _(How this works with ranks is OPEN; see 8.2.)_

### 7.8 Region Map (Q25)
- Activities: building **roads, rail, canals, and transport lines between cities**; **trade routes**; **diplomacy**; **moving armies**; **claiming land**.
- **Decision (delegated to Claude): one real-time clock for the whole game.** The region map is a *view*, not a separate mode. Pausing pauses everything, and time keeps flowing in every city and realm while the player is on any view.

## 8. Ranks of Office (Q26) — core system

The player's Office has a **rank**. The **systems available to the player depend on that rank**, and they exist in **every era**.

| Rank | Domain | Example titles (era-dependent) |
|------|--------|-------------------------------|
| 1 | **Neighborhood** (part of a city) | Ward elder, quarter headman, alderman, ward boss |
| 2 | **City** | Chieftain or governor of a town, burgess, mayor |
| 3 | **Region / State** (multiple cities) | Nomarch, provincial governor, state governor |
| 4 | **Nation** | King, president, prime minister |
| 5 | **Empire** (many countries, or a nation with colonies) | Pharaoh of the Two Lands, emperor, imperial power |

- Each rank has its own **systems**: budget scope, laws vs. ordinances, diplomacy, military, and so on.

### 8.1 Rank-Scaled Conflict (Q27)
- **War and military are in scope.**
- **Conflict scales by era:**
  - **Ancient:** conflict is localized and can happen at **any rank**, from **neighborhood clashes inside a city** to (more rarely) **wars between kingdoms or empires**.
  - **Later eras:** conflict moves up the ranks. By the **Modern era**, formal warfare is **between nations and empires only**. Neighborhoods and cities no longer wage formal war.
- **Combat model:** **abstracted CK3-style armies** on the region map, **with battles that can be watched in the city** when they happen in or near a city being viewed.

### 8.2 Rank Questions — _OPEN_
See Round 3 follow-ups (starting rank, promotion and demotion, superiors, delegation, the 10-city limit).

## 9. Crises & Constraints

### 9.1 Disasters & Crises (Q28)
- **Specific to era, region, and situation.** Examples: fire, flood (the Nile's annual inundation), plague and epidemics, famine, earthquakes, economic depressions (e.g., 1929).

### 9.2 Limits on Player Power (Q29)
- **The player's power is limited by game mechanics:** eminent domain costs, laws, factions, government form, resource limits, protected land, and so on. The player is **not an all-powerful hand**.

## 6. Scope Notes & Risks (living)
- **Content multiplication:** launch maps × launch eras × playable cultures. Because full playthroughs stay on one map (4n), the Middle East map needs Ancient, Colonial, and Modern content. _Launch scope decision OPEN._
- **Biggest technical risks:** custom building construction, agent simulation at scale, continental real-terrain pipeline, aggregate ↔ agent LOD transitions, NPC realm AI.
- **Mitigation:** ship in **milestones**, each one a playable vertical slice; keep content in data; build systems that don't depend on any era.

# game

Godot project (GDScript, scenes, assets). Built GDExtension binaries go in `bin/` (gitignored).

## Trying the terrain before a real map exists

The city view draws real terrain when a game has a map and an active city. Until the real Ancient Near East package
is built (T1.2.6.1), use the synthetic map as a developer content root:

1. Build the tools (any preset), then write the map once into `game/content_dev/` (gitignored):

       build/<preset>/sim/tools/mapgen/citysim_mapgen --synthetic --mod game/content_dev

   This makes a mod `test` holding the map `test:map.synthetic` (96 x 96 km, a valley, a river with a delta and a
   coast), with its definition, its text and its package.
2. Run the game, open the developer console (the backquote key) and type `new_world`. It starts a game on that map
   with `res://content_dev` as an extra content root and founds a city in the middle of the detail terrain
   (`Sim.new_world()`, `Sim.find_city_site()`). The test ground disappears and the terrain appears; the camera stays
   inside the city's 8 x 8 km and rides on the ground. `new_test_map` goes back to the flat test map.


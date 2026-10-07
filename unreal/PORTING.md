# Ringshadow in Unreal Engine: the port

The game is being remade in Unreal Engine 5 here, in `unreal/`. The Swift and
SceneKit game it was ported from left this repo on 2026-10-05; it is archived
with its history in `~/git/dev/autocraft-swift` (`docs/swift-move.md`). The
`Foo.swift:123` notes in this file and in the code point into that archive. Why the move: SceneKit draws every unit part by part,
about 40 draws a Ranger, and late games of two players already drop under
30 fps; eight players with big armies need instanced drawing, and Unreal also
opens Windows, Steam and VR.

## Layout

| Path | What |
|---|---|
| `Autocraft.uproject` | The Unreal project (UE 5.8). |
| `Source/AutocraftCore/` | The simulation and the AI: GameCore ported line by line to C++20, **with no engine code** (`Public/*.h`, `Private/*.cpp`). Unreal builds it as a module; `core-tests/` builds it with plain clang++. |
| `Source/Autocraft/` | The Unreal game: terrain, unit rendering (instanced), camera, input, HUD, audio, effects. |
| `core-tests/` | `make -C unreal/core-tests` builds the core and its tests without Unreal and runs them. `golden/` holds what the Swift game computes, for the C++ to match. |
| `Tools/` | Exporters from the Swift game (models, textures, sounds) and Unreal editor scripts that import them. |

## How the core is written

- **A faithful port.** Every Swift file in `Sources/GameCore` becomes a `.h`
  and `.cpp` of the same name (`NavGrid.swift` → `NavGrid.h`, `NavGrid.cpp`;
  extensions such as `Commander+Intel.swift` → `Commander+Intel.cpp`). Same
  type names, same member names, same order of operations, and the comments
  come along. The AI and the balance were tuned over many games; a port that
  "improves" them as it goes loses that.
- **Namespace `ac`.** Types keep their Swift names (`ac::Unit`,
  `ac::GameState`). Nested types stay nested (`Unit::Kind`, `Unit::Task`).
  Swift's member style (lowerCamelCase) stays, not Unreal's: the Unreal module
  wraps the core and never mixes the two.
- **Data types** live in `Types.h` (from `State.swift`, `MapDefinition.swift`,
  `Directives.swift`, the record and effect types of `Leveling.swift`,
  `Intel` of `Vision.swift`), since C++ needs a type complete before it is
  held by value and the Swift types hold each other. Logic goes in the file
  named after its Swift file.
- **Swift to C++:**
  - `Vec2` (`SIMD2<Double>`) is `ac::Vec2` in `SimdMath.h` (not `Math.h`:
    on a case-insensitive disk that name hides `<math.h>`) with the simd
    functions it uses (`distance`, `length`, `normalize`, `dot`, `min`/`max`
    elementwise...), computed as Apple's precise simd does. `SimdMath.h` also
    has Swift's scalar `min`/`max` (`max(x, y)` is `y >= x ? y : x`; `std::max`
    differs on -0 and +0), `pi` and `rounded`: write `max(a, b)` as in Swift.
  - `T?` is `std::optional<T>`, `[T]` is `std::vector<T>`. `Set` and
    `Dictionary` become `std::set` and `std::map` wherever they are iterated:
    their order is then fixed, so the simulation repeats exactly (Swift's
    hash order changes every launch, see README, Performance).
    `std::unordered_*` only for lookups that are never iterated.
  - An enum without payloads is an `enum class` with the same case names,
    plus `rawValue(e)` and `parse<E>(string)` (nil when unknown) for the raw
    strings and `allCases<E>()`, all from an `EnumInfo<E>` specialisation in
    `Types.h`. Its computed properties are free functions of the same name
    taking it first (`ore(Upgrade)`, `name(Upgrade)`, `title(Stance)`,
    `ordinal(Unit::Kind)`, `covers(Filter, k)`, `info(Perk)`); its nested types
    and statics take its name as a prefix (`PerkInfo`, `PerkSlot`,
    `perkOffer`). `Unit::Kind` and `Structure::Kind` are aliases of
    `UnitKind` and `StructureKind`, declared in `Rules.h` so `Rules` can take
    them.
  - An enum with payloads (`Mission`, `Command`, `GameEvent`, `Effect`,
    `Stat`, `Scope`, `Change`, `Request::What`) is a struct holding
    `std::variant value` of small structs named after the cases
    (`Mission::Raid{Vec2 at}`), in the Swift order (`AC_CASES` in `Types.h`).
    A case converts to the enum (`Mission m = Mission::Raid{p};`);
    `m.is<Mission::Raid>()` tests, `m.as<Mission::Raid>()` gives a pointer or
    null, `m.value` goes to `std::visit`. A labelled payload keeps its label;
    an unlabelled one is named for what it holds (`at` for a point, `kind`,
    `upgrade`, `mission`, `x` for `Change`'s number) and is `_0` in JSON.
    Inside such a struct a case may share a type's name (`Command::Mission`),
    so payloads name the type `ac::Mission`. `indirect case` holds an
    `Indirect<T>` (a copying heap box): `Command::Serve{p, r, Command(...)}`.
  - A Swift name that is a C++ keyword gets a trailing underscore
    (`GameState::new_`, `Stance::auto_`, `Rules::requires_`). A method named
    like a stored property gets a new name (`Intel::isExplored`).
  - Structs are aggregates with Swift's defaults as member initialisers, so
    Swift's labelled inits are designated initialisers
    (`Player{.aggression = r}`, `Aura{.filter = Filter::bio}`). `Unit` and
    `Structure` have constructors (they set `hp`), with `owner` not
    defaulted since C++ defaults must trail. Computed properties are const
    member functions of the same name (`complete()`, `walking()`). Every
    type has `==` (the payload enums of `Leveling` also `<=>`, for map keys).
  - Swift's `&+`/`&*` wrapping arithmetic is unsigned arithmetic in C++.
    `Int` is `int64_t`, `UInt32` is `uint32_t`, `Double` is `double`.
- **Swift extensions of one type, ported by different agents at once:**
  the class lives in `Simulation.h` (owned by whoever ports
  `Simulation.swift`), and its body includes one members fragment per
  extension file, `Public/Simulation+Leveling.members.h`,
  `Simulation+Pilot.members.h` (for `Pilot.swift`), `Simulation+Kinds.members.h`
  (`Pilot+Kinds.swift`), `Simulation+Vision.members.h`,
  `Simulation+Playground.members.h`: the member declarations of that file,
  nothing else (types the extension declares go in a normal header of its
  own). The bodies go in the `.cpp` of the Swift file's name. `Commander.h`
  works the same (`Commander+Early.members.h`, `+Intel`, `+Objectives`,
  `+Requests`). Each agent edits only the files it owns; a missing fragment
  is created empty by the class owner so the build never breaks on it.
- **Building while others edit:** check your own files with
  `clang++ -std=c++20 -fsyntax-only -Wall -Wextra -Wshadow -Werror -ffp-contract=off -fno-exceptions -fno-rtti -I unreal/Source/AutocraftCore/Public -I unreal/Source/AutocraftCore/ThirdParty FILE.cpp`;
  the full `make -C unreal/core-tests` builds everyone's files and may fail
  on another agent's half-written one. Wait and retry, never "fix" a file
  you don't own: tell the owner (the report) instead.
- **Rules Unreal sets:** no exceptions, no RTTI, no `dynamic_cast`. No
  identifier named `check`, `verify`, `ensure`, `TEXT`, `PI` or `UE_LOG`
  (Unreal macros). No shadowed variables (`-Wshadow` is an error in Unreal).
  No `static` objects that need constructors at load: use a function-local
  `static` instead.
- **Saved games:** `Session` and `GameState` read and write the same JSON as
  the Swift game (Swift's `Codable` shape: optionals left out when nil, a
  `Vec2` as `[x, y]`, an enum with payloads as `{"case":{"_0":...}}`), using
  `ThirdParty/json.hpp` (nlohmann/json 3.12, MIT) built with
  `JSON_NOEXCEPTION`, only in `Session.cpp`: each type lists its fields once
  in a `fields(io, value)` there that both reads and writes, so a new stored
  property is one line. `SessionStore::decode<T>(text)` and `encode(value)`
  (explicitly instantiated for `Session`, `GameState`, `MapDefinition` and
  the other saved types), `SessionStore::load(path)`, `save(session, path)`.
  Dates are ISO 8601 in UTC to the second; infinities and NaN are the
  strings `"inf"`, `"-inf"`, `"nan"`; a whole double is written as an
  integer, as Swift does. Every Swift fixture (`bench/*.json`,
  `Tests/Fixtures/*.json`) loads in C++ and saves back to the same JSON
  (`SessionTests.cpp`).
- **Same numbers as Swift:** `core-tests` builds with `-ffp-contract=off`
  (no fused multiply-add, as in Swift), so terrain, paths and sight match the
  Swift game bit for bit where the code is the same. Goldens come from
  `Tests/GameCoreTests/GoldenDumpTests.swift`, which writes them only when
  `AUTOCRAFT_GOLDEN_OUT` is set:
  `AUTOCRAFT_GOLDEN_OUT=$PWD/unreal/core-tests/golden swift test --filter GoldenDump`.
- **Tests:** `core-tests/tests/test.h` is the framework: `TEST(name)`,
  `EXPECT_TRUE`, `ASSERT_TRUE` (leaves the test), `EXPECT_EQ` (doubles
  exact, NaN equal to NaN), `EXPECT_NEAR`; `golden.h` reads goldens and
  compares JSON trees. `make -C unreal/core-tests FILTER=noise` runs the
  tests whose name contains `noise`. Each Swift test in `Tests/GameCoreTests` is ported to
  `core-tests/tests/` with the same scenario, and the goldens are checked
  there too. The rules in `AGENTS.md` hold: isolated scenarios only, never a
  whole game played by the AIs.

## Teams and eight players (C++ only)

From here the C++ core diverges from Swift by design: one human (player 0)
and up to seven AIs in teams (4v4, 2v2v2v2, 1v1...), allies sharing their
sight. With no team set anywhere, every rule below reduces to the Swift one,
so every Swift golden still matches bit for bit.

- **Membership.** `Player::team` (optional, saved as `"team"` only when set).
  `GameState::team(p)` is it, else `p` itself (also for an index that is no
  player); `allied(a, b)`, `hostile(a, b)` compare teams, and `teams()` lists
  them (nil when none is set). Teams are set for all players or none. Old
  saves load with none: every player for itself.
- **"Enemy" everywhere.** Every test that meant "the other side" now asks
  `hostile` (targets, splash and flak, turrets, melee, threats near a base,
  hits that earn leveling XP, the fog's "others", every `u.owner != player`
  of the Commander and its extensions, enemy starts for scouting, bastion
  spots and expansion). Tests that mean "mine" stay `==`/`!=` on the owner:
  supply, production, entering one's own Bastion, Derrick or building,
  Dropship heals, rally, an expansion site held by anyone else. Allies'
  units never shoot each other; non-flak splash still hurts everyone near.
- **Shared sight.** `Vision::sight(p)` sees from every allied unit and
  building, and its `ids` are the hostile ones it sees. `look()` works a
  team's sight out once (by its first player) and gives each ally a copy,
  OR-ing the explored bits. `sees`, `foe`, `known`, `shown`, `unseen` and
  `Intel` treat allies as one's own (never in `Intel`).
- **Victory.** `bury` ends the game when the buildings left belong to one
  team. `winner` is that team's lowest-indexed player still standing (the
  `.victory` event carries it); every player of the team gets +1 in `score`,
  which stays per player (a team's score is any member's). An AI whose team
  won hunts what is left (`s.winner` allied).
- **Starts.** `GameState::new_(map, score, round, teams)`: one player per
  `teams` entry (at most the map's starts). A map's `starts` go round the map
  in order; each team takes a run of neighbouring starts and the teams are
  spread evenly round the ring (4v4 on 8: halves; 2v2v2v2: quarters; 1v1:
  opposite), the ring's offset, the teams' order and the order inside a team
  drawn from the seed (`deal` in `Types.cpp`). Two-start maps keep Swift's
  draw. `Simulation::newGame` and `Session::restart` keep the teams.
- **Maps.** `MapChoice::players` (2, 4, 8). 2 is the Swift maps, unchanged.
  4 and 8 are `WindowMaps::buildSquare`: a square laid out in its eighth
  0 <= z <= x and copied by the square's eight symmetries
  (`MapDefinition::squareSymmetric`, which also folds the terrain noise and
  keys the walking grid's cache), so every start has the same ground. Mains
  on a ring (8: at 22.5° + 45°k; 4: on the diagonals), a natural each, a
  contested base on each axis, 0-3 more by size; highlands and badlands put
  each main on a pocket with one ramp toward the middle, the ridge raises the
  middle with four ramps, the open field is flat. Side 160-256 cells (4) and
  224-320 (8).
- **Allied AIs.** Minimal: `Commander::focus` picks the enemy team whose
  starts lie nearest its own team's starts (of the teams it knows a building
  of), the same pick for every ally, and `attackTarget` aims only at that
  team's bases. An army of at least `allyJoin` (6) supply joins an ally's
  attack at its point when it has no wave of its own. The same army (no
  attack of its own under way, not on hold or all in) defends an ally's base
  under a real attack (enemy soldiers worth more than `raidSize` within 12
  of an allied building; the human's too) as it defends its own, and goes
  home with it once that base is clear (`teams_aiDefendsAnAllysBase`).
- **Leveling and piloting.** The pilot is player 0's; a pick's `Scope::Team`
  and `Every` mean the driver's own player, not its allies (allies' AIs are
  not boosted), and kills of allies earn nothing.
- **Tests.** `core-tests/tests/TeamTests.cpp`: hostility with no teams,
  allies holding fire, shared sight, victory with three teams, the square
  maps (symmetry, flat far-apart bases, every base reachable from every
  start; all sizes with `AUTOCRAFT_MAP_TESTS=1`), team starts, the AI's
  enemy team and joining, saves, and the timing of one minute of a 4v4 from
  the start (about 0.05-0.06 ms a step at 60 Hz on the M4 Max; AI seconds
  peak near 2 ms).
- **Left for later.** Shared Intel memory (each
  ally keeps its own, alike while their sight is), a team's AIs splitting
  expansions, the human's HUD for teams (GAME-LAYER §3.6), timing of a late
  8-player game (P1).

## Progress

Update this as files land: `todo`, `ported` (compiles), `tested` (its tests
pass, goldens match).

| Swift file | Status | Notes |
|---|---|---|
| `Noise.swift` | tested | `SeededRandom` also has Swift's `next(upperBound:)`, `Int.random(in:)`, `shuffle`, `randomElement`; golden `noise.json` |
| `MapDefinition.swift` | tested | in `Types.h`; `BaseSite::standard`, `front` against golden `states.json` |
| `State.swift` | tested | in `Types.h` (methods in `Types.cpp`); `Rules`, `UnitStats` in `Rules.h`; `GameState::new_` against golden `states.json` |
| `Directives.swift` | ported | in `Types.h`; `Request::title()` in `Directives.cpp` (reads `Simulation::title`) |
| `Leveling.swift` | tested | types and `Leveling`'s declarations in `Types.h`, catalogue, balance and signature in `Leveling.cpp` (golden `leveling.json`: signature, balance, every perk match); `Simulation+Leveling.swift` → `Simulation+Leveling.cpp` (tested: LevelingTests, LevelingKindsTests, TrackingTests' facts) |
| `Vision.swift` | tested | `Intel` in `Types.h`, `Rules.vision` in `Rules.cpp`; `Vision`, `Sight` in `Vision.h` (cells are bytes; `tier(at:)` is `tierAt`), the fog's Simulation members in `Simulation+Vision.members.h`, bodies in `Vision.cpp`. `VisionTests` (8) pass; `GoldenVisionTests` matches the Swift fog exactly on `bench/badlands-large.json` (levels, sights, Intel) |
| `TerrainField.swift` | tested | heights, border, levels, materials, picks bit-equal to `golden/terrain.json` (GoldenTerrainTests) |
| `NavGrid.swift` | tested | cells, regions, 626 paths, queries, `setDynamic` bit-equal to `golden/nav.json`; grid built on threads |
| `Router.swift` | tested | routes and distances match `golden/nav.json` |
| `MapLibrary.swift`, `WindowMaps.swift` | tested | all 16 window maps, playground, handcrafted, 2 generated: field by field; `MapStyle`/`MapSize`/`MapChoice` in `WindowMaps.h` |
| `MapView.swift`, `Projection.swift`, `FreeView.swift`, `ScreenConfig.swift` | tested | ProjectionTests, WindowModeTests; FreeView steps bit-equal to the golden |
| `Hearing.swift`, `StallWatch.swift`, `MusicQueue.swift` | tested | `HearingTests` (3), `StallWatchTests` (1), `MusicQueueTests` (4, a seeded order matching Swift's shuffle); `MusicQueue.h` is a header-only template on any generator with `uint64_t next()` |
| `Simulation.swift` and its extensions | tested | `Simulation.cpp`, `Simulation+Playground.cpp` (the other extensions with their owners). Tests: `SimulationTests`, `FightTests`, `AirTests`, `PlaygroundTests`, `StallTests` (movement); `SimulationGoldenTests` matches `golden/simulation.json` (`GoldenSimulationTests.swift`: duel, mining, skirmish under fog, playground) bit for bit every second. The id index is `unitIndexByID` (C++ cannot share a name with `unitIndex(_:)`); `hit`'s shooter is a `const Unit*` |
| `Pilot.swift`, `Pilot+Kinds.swift` | tested | `Pilot.h`, `Pilot.cpp`, `Pilot+Kinds.cpp`, both members fragments; `core-tests/tests/PilotTests.cpp`: all 46 scenarios of PilotTests.swift pass (426 expectations); one generic path for every kind, as in Swift |
| `Commander.swift` and its extensions | `Commander.swift`: tested | `Commander.h`/`.cpp`; `CommanderTests` (19 scenarios) pass, golden `commander.json` (`GoldenCommanderTests.swift`: orders and spots on the bench fixture, a fresh and a teched game) matches exactly. Extensions: see their porter |
| `Commander+Early.swift`, `+Intel`, `+Objectives`, `+Requests` | tested | members fragments (nested types `EnemyMix`, `Counters`, `Plan`, `Manning`, `Step`, `Requests` defined there; `Manning.short` is `short_`); `DirectivesTests` (6) and `ObjectivesTests` (13) pass; golden `squads.json` (`GoldenSquadsTests.swift`: every call of the four files on the bench fixture, a fresh and a teched game, with objectives and a queue) matches exactly. Intel/Early scenarios are in `CommanderTests` |
| Teams, 4- and 8-player maps (C++ only) | tested | see "Teams and eight players"; `TeamTests` |
| `Session.swift` | tested | JSON load and save, `SessionStore` with plain files; fixtures and golden `session-full.json` round-trip |
| `Database.swift`, `Tracking*.swift` | tested | `Tracking.h/.cpp` (engine-free: `TrackedGame::make` builds a game's rows from a session and its state, `worth::` the pick-rate, win-rate and margin maths) and `TrackingStore.h/.cpp` (tables, versioned migration, the four views, the whole-game upsert) speak SQLite's C API: the game links the engine's `SQLiteCore` plugin (3.47.1, enabled in `Autocraft.uproject`), core-tests the system's `libsqlite3`. The game layer is `AcTracker.*` (one writer thread, queued in order) and `UAcSimSubsystem::Track`; the file is `~/Library/Application Support/Autocraft/Unreal/tracking.sqlite` (`AcSaves::TrackingFile`). `make balance` at the repo root reads it. Tests: `TrackingStoreTests` (18, core-tests) and `Autocraft.Tracker.*` (automation, on the engine's SQLite). The performance tables (`PerfStore`, `version.perf`) are not ported |
| `MusicStore.swift` | later | SQLite; in Unreal later |

## The Unreal side

Not started: it needs Unreal Engine 5.8 installed (Epic Games Launcher, the
default `/Users/Shared/Epic Games/UE_5.8`). Plan, in order:

1. Project and modules; the core ticking a saved game with no graphics.
2. Terrain from `TerrainField` as a mesh, the RTS camera (`FreeView`).
3. Models exported from the Swift game (`Tools/`) and imported as static
   meshes, one per rigid part; units drawn as instanced static meshes, one
   instance set per kind, part and material, the walk cycles and turrets
   driven in C++ as `GameScene` drives them.
   Exporter done: `Autocraft export-models unreal/Content-src/models` writes every model as USDA with a part manifest (`Tools/README.md`); the Unreal import is next.
4. Selection, commands, the HUD (UMG from C++), the minimap, the fog.
5. Effects, sounds and music, day and night, lights.
6. Driving a unit (`Pilot`), first and third person.
7. Eight players.

## Simulation speed

`make -C unreal/core-tests bench` builds `core-tests/build/simbench` (the
core at -O3, `bench/SimBench.cpp`): `simbench FIXTURE SECONDS [--army N]`
is the C++ twin of `Autocraft bench --sim SECONDS [--army N] --fixture
FIXTURE`. Same map, commanders and fog, the same `stageFight` armies, steps
of 1/60 s and the same output line. Both sides end each run on the same unit
counts. Measured 2026-10-05 on the M4 Max (load about 4), three interleaved
runs each, medians, step times in ms (Swift with
`SWIFT_DETERMINISTIC_HASHING=1`; since 2026-10-05 `Commander.transfers` walks its
per-base dictionary in site order like the core's `std::map`, so Swift no longer
needs it for these runs: `AUTOCRAFT_SIM_HASH=1 .build/release/Autocraft bench
--sim 60 --fixture bench/badlands-large.json` prints the same `hash` line as
`simbench ... 60 --hash` (`dc23a93bbd495396`, three launches, no env var; with
`--army 100` over 20 s `1eda476e99bad99c`, both sides). Other Dictionary/Set
iteration in the commander path: none that affects commands, the rest are lookups.
The table's runs (Swift with the env var):

| Run | | wall s | mean | p50 | p95 | p99 | max | units |
|---|---|---|---|---|---|---|---|---|
| `badlands-large` 120 s | Swift | 2.15 | 0.30 | 0.10 | 0.44 | 4.00 | 23.2 | 110 → 143 |
| | C++ | 1.14 | 0.16 | 0.05 | 0.31 | 2.71 | 10.2 | 110 → 143 |
| `badlands-large` 30 s `--army 200` | Swift | 3.04 | 1.69 | 0.69 | 5.48 | 25.6 | 192.8 | 499 → 201 |
| | C++ | 2.00 | 1.11 | 0.47 | 3.17 | 14.8 | 145.5 | 499 → 201 |
| late game (19.6 min) 60 s | Swift | 1.00 | 0.28 | 0.19 | 0.59 | 0.74 | 8.2 | 150 → 154 |
| | C++ | 0.77 | 0.21 | 0.16 | 0.42 | 0.50 | 6.4 | 150 → 154 |

C++ is 1.3–1.9 times as fast with the same algorithms; the worst steps (the
first step after staging, a fight's re-pathing) shrink less. The slowest
step in each badlands run is the AIs' turn at the start of each game second.

Where the C++ goes at `--army 200` (Time Profiler, self time, share of the
time inside `Simulation::step`):

1. `NavGrid::path` (A*, called from `travel`): 38 %
2. `NavGrid::inRock`: 22 % (from `separate` about half, the rest from `travel` and `move`)
3. `Vision::stamp`: 7 %
4. `Heap::push` (the path's open list): 4.5 %
5. `TerrainField::level`: 4.4 %
6. `Simulation::separate`: 2.1 %
7. `Noise::fbm` (terrain height lookups): 1.9 %
8. `Commander::clear`: 1.8 %
9. `__sincos_stret`: 1.6 %
10. `NavGrid::clear`: 0.9 %

Path finding and its rock tests are about two thirds of a big fight's step:
the place to optimise, in Swift and C++ together, with new goldens.

### C++ speed-ups that keep every result (2026-10-05)

These change no result: every golden and test passes unchanged, and a hash of
every unit's position and hp after every step of the three runs below is the
same bit for bit before and after.

- **`NavGrid::inRock`** looks only at the rocks whose box reaches the point's
  2×2 bucket (`rockStart`/`rockList`, rebuilt with `rocks`), with the same
  test per rock. It used to scan every rock on the map.
- **`NavGrid::path`** reads one `passable` byte per cell (1 free, 2 a
  jumpable cliff face, rebuilt with `dynamic`) instead of three layers. A
  search's per-cell `mark`, `g` and `came` sit side by side. Each scan order's
  8 steps are unrolled. Cells off the map's edge are tested only for an edge
  start cell: free cells and cliff faces keep three cells off the edge.
- **`Heap`** stores 8-byte items (`float`, `int32_t`) and sifts a hole instead
  of swapping, with the same comparisons, so items with the same priority come
  out in Swift's order. Which child is smaller is worked out without a branch
  (`lessBit`, `fcmp` + `cset` on arm64). That branch was a coin toss for the
  CPU and took half of a search's time.
- **`TerrainField::level`** skips a plateau when the point is off its polygon's
  bounds. Its signed distance cannot be negative there.

The spike at `--army 200` is the first step after `stageFight`: 324 path
searches, about 5,400 cells expanded each (1.76 M), 142 of its 146 ms. No two
of them are the same query, and only 3 % of searches over a whole run repeat an
earlier one, so a cache would not help. Spreading the searches over several
steps, or running them in parallel, would change when units get their paths and
so the results. That leaves making each search cheaper.

Measured 2026-10-05 with the load at about 15 (other projects building), three
interleaved runs each, medians, C++ only, step times in ms:

| Run | | wall s | mean | p50 | p95 | p99 | max | units |
|---|---|---|---|---|---|---|---|---|
| `badlands-large` 30 s `--army 200` | before | 2.21 | 1.23 | 0.52 | 3.51 | 16.2 | 159.5 | 499 → 201 |
| | after | 1.34 | 0.74 | 0.22 | 2.09 | 11.9 | 111.4 | 499 → 201 |
| `badlands-large` 120 s | before | 1.28 | 0.18 | 0.05 | 0.36 | 2.98 | 11.0 | 110 → 143 |
| | after | 0.92 | 0.13 | 0.02 | 0.30 | 2.28 | 9.3 | 110 → 143 |
| late game (19.6 min) 60 s | before | 0.84 | 0.23 | 0.17 | 0.48 | 0.60 | 6.8 | 150 → 154 |
| | after | 0.57 | 0.16 | 0.10 | 0.39 | 0.57 | 4.5 | 150 → 154 |

The three runs are 1.4–1.65 times as fast, and the first step after staging
takes 30 % less time. What is left at `--army 200`: the binary
heap's pop and push, about half of the A* time. They are latency-bound, and a
heap of another shape would break ties in another order. Then
`Vision::stamp`: rewriting it to mark each row's run of cells without a test,
and to gather the `explored` bits per word, gave nothing measurable, so it
stays as it was. The fixed costs follow (`TerrainField::height`,
`Commander::clear`, `NavGrid::solid`).

### Paths ahead, and more that keeps every result (P1/P2, 2026-10-05)

`unreal/bench/late8.json` is the bench's `late8` moment saved as a fixture
(Badlands · Large · 8 players, 4v4, time 0, 1,448 units: 180 a player and a
late game's buildings, staged by the renderer's `-AcStage=180
-AcStageBuildings`); `simbench` now takes 4- and 8-player maps. Its slow
steps were all path searches: the first step (the AIs' first orders) made
2,664 calls to `NavGrid::path`, 1,408 of them full searches expanding 34.6 M
cells (3.7 s of 3.8 s); at 2.15 s a building's footprint changed, so
`refreshNav` dropped every walking unit's goal and 949 units searched again
(1.3 s). Swift does the same work.

- **`PathAhead`** (`PathAhead.h`, C++ only): `PathAhead::step(sim, dt)` copies
  the simulation and steps the copy (the *probe*) on another thread with
  every search left out (it walks straight), queueing each search it would
  make; worker threads run them on the probe's grid. The real step takes an
  answer only for the very same question (start, target, stop distance,
  jumps, compared bit for bit) on a grid checked equal to its own, waits for
  one a worker is on, and searches itself otherwise. `NavGrid::path` is split
  into `pathBegin` (the cheap checks) and `pathSearch`; `Simulation::findPath`
  routes `travel`'s two calls. The probe skips copying the commanders on
  steps where they do not think, assigns into a spare probe (its arrays are
  reused), and is freed off the stepping thread. Test
  `pathAheadStepsTheSameGame`; `simbench --ahead N`.
- **`Simulation::separate`** tests only the pairs in a box about each unit,
  from a grid of buckets kept up to date as units are pushed, in the same
  order (Swift tests all pairs, 1 M at 1,448 soldiers).
- **`NavGrid::insidePatch`** answers no without the rotation when the point
  is farther from the patch than its half diagonal (it took a `sincos` per
  patch per push: a third of `separate`).
- **`NavGrid::closestReachable`** marks the cells it has seen with a search
  stamp in pooled scratch instead of a hash set.

Every run below ends on the same hash of every unit's id, position and hp
after every step (`simbench --hash`), with and without `--ahead` at 1, 4 and
12 threads; all 257 core tests pass. Three interleaved rounds, load 4-7,
medians, step times in ms ("before": the core before these changes; "ahead":
`--ahead 12`):

| Run | | wall s | mean | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|---|
| `late8` 10 s (1,448 units) | before | 7.96 | 13.21 | 3.56 | 29.4 | 66.9 | 2722 |
| | after | 6.55 | 10.88 | 1.17 | 26.7 | 66.4 | 2684 |
| | ahead | 2.10 | 3.45 | 1.50 | 9.5 | 17.7 | 301 |
| `badlands-large` 30 s `--army 200` | before | 1.34 | 0.73 | 0.22 | 2.08 | 12.1 | 106 |
| | after | 1.23 | 0.67 | 0.15 | 1.94 | 12.0 | 108 |
| | ahead | 0.98 | 0.53 | 0.27 | 1.51 | 5.1 | 16 |
| late game (19.6 min) 60 s | before | 0.55 | 0.15 | 0.10 | 0.36 | 0.43 | 4.5 |
| | after | 0.49 | 0.13 | 0.08 | 0.35 | 0.41 | 4.6 |
| | ahead | 0.80 | 0.22 | 0.16 | 0.46 | 0.61 | 2.2 |
| `badlands-large` 120 s | before | 0.90 | 0.12 | 0.02 | 0.27 | 2.18 | 8.8 |
| | after | 0.88 | 0.12 | 0.02 | 0.27 | 2.14 | 8.6 |
| | ahead | 1.36 | 0.18 | 0.09 | 0.37 | 1.30 | 8.0 |

The probe costs ~0.1 ms a step (its copy and the hand-over), so with few
units it loses a little: the game layer turns it on from 300 units
(`ac.SimPathAhead`, 2 = auto). What is left of the first `late8` step is the
1,408 searches over 10 threads (each ~2 ms, some ~20 ms). A search itself is
no faster: the binary heap's order decides ties, see above.

### Fast paths (the three that change results, approved 2026-10-05)

`Simulation::setFastPaths(on, budget = 32)` (C++ only, off by default, so
every golden and parity test is unchanged; the game turns it on,
`ac.SimFastPaths` / `ac.SimPathBudget`). On:

- **Re-path only what a change crosses.** `NavGrid::setDynamic` keeps the
  cells it newly blocked (`newlyBlocked`, a mask grown by one cell,
  `changedBox`); `refreshNav` clears a walker's goal and waypoints only when
  it was pushed out of something or a leg of its route (position, waypoints,
  goal) crosses the mask (`NavGrid::crossesChange`, a grid walk). The rest
  walk on; freed cells change nobody's route.
- **A budget of searches a step** (`pathBudget`, counted in `pathsStarted`,
  reset at the top of `step`): past it a unit whose goal differs keeps
  walking its old waypoints, or waits where it stands, and asks again next
  step (units in order of index; no queue). PathAhead's probe counts the
  same way, so it still matches the real step.
- **Faster A\*.** Octile heuristic (minus the goal disk's reach, scaled by
  octile/euclid <= 1.0825, so it is admissible), 4-ary heap (`Heap4`), and a
  heuristic weight of `NavGrid::fastWeight` = 1.15: weight 1 still closed
  38 M cells over late8's first 3 s (cliff basins; octile plus tie-breaking
  barely helped, the heap order none), 1.15 closes 4 M. Routes measured
  <= 1.07 times the shortest on 150 random pairs (test).

Tests (`core-tests/tests/FastPathsTests.cpp`): routes valid and not much
longer, units arrive through a budget of 2, the same game run to run and
under PathAhead (and unlike the exact game), the re-path set (crossed legs,
last straight leg, alongside, away, idle, pushed). `simbench --fast [N]`.
late8 10 s, simbench (ms a step, three runs):

| | wall s | mean | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| exact | 6.8 | 11.3 | 1.2 | 27.6 | 68 | 2,820 |
| exact, `--ahead 8` | 2.35 | 3.9 | 1.7 | 10.3 | 19 | 395 |
| fast | 1.19 | 2.0 | 0.74 | 8.9 | 16.6 | 19 |
| fast, `--ahead 8` | 1.28 | 2.1 | 1.18 | 6.5 | 13 | 21 |

(Searches 6.2 k to 1.6 k, cells closed 69 M to 1.8 M: fewer walkers move
while they wait, so the games differ.) PathAhead now helps the fast mode
little (the probe's copy costs ~0.1 ms a step); the game keeps both on.

## Kestrel combat differs from Swift on purpose (2026-10-05)

The Kestrel fires rockets at the ground and a rail gun at the air
(`UnitStats::railgun`: 34, +14 armoured, 3.2 s, range 8, hitscan, own clock
`Unit::railCooldown`; `Simulation::weapon(u, air)`). Swift's Kestrel is rockets
at the ground only, and the Swift game is not changed. The C++/Swift hash
parity above holds for games without Kestrels only (the golden and parity
tests have none that meet a flier); with a Kestrel the two diverge as soon as
it sees an enemy flier, and the AI counts it as anti-air.

## All in does not gather mid-attack (2026-10-05, differs from Swift)

`Commander::army`: with the stance All in, an attack under way never issues the "strung out, gather first" attack-move (`regroup`). That gather point is the soldier at the army's middle, behind the front runners, so each gathering sent them back and the army dithered two steps on, two steps back. Swift (`Commander.swift:164`) still gathers under All in. Test: `core-tests/tests/AllInDitherTests.cpp`; goldens unchanged.

## Prospectors never idle with ore on the map (2026-10-05)

`Simulation::choosePatch` (Simulation.cpp) only looked at patches of the
player's own bases (a base with a Citadel). With every patch of those mined
out and too little ore for a new Citadel, an idle Prospector found no work and
stood still for good, with ore fields all around and nobody repairing or
mining (the Swift game has the same flaw). C++ now falls back to the nearest
live patch with no enemy Citadel on its site, and the worker hauls the ore to
its nearest Citadel; the ore pays for the next base. Swift is not changed. The
goldens and the parity tests are not affected (their Prospectors always have an
owned patch); tests: `ProspectorIdleTests.cpp`.

## Fliers fly every step under an army order (2026-10-05, differs from Swift)

`Simulation::travel` never set `Unit::goal` for a flier. The attack-move branch of `stepSoldier` (and the rally branch) walk on between two thinks (4 a second) only while a goal is set, so a Kestrel (or Dropship) under an army attack-move moved one step in eight: stop-and-go. Swift has the same flaw. C++ now keeps the goal while a flier flies (`Simulation::flierKeepsGoal`, true; false only in the skirmish golden, whose red Kestrel attack-moves). Not the rail gun, fast paths (fliers never search) or the budget. Tests: `core-tests/tests/FlyerStopGoTests.cpp`.

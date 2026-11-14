# MoonBase — Relay Wars

A native-first Defold RTS skirmish using DataStore. Two armies fight across a 1,536 × 1,536 map, capture five relays, and attack each other's headquarters. Relays drain enemy HQ integrity; the first HQ to reach zero loses. Destroyed units return after eight seconds to keep the battle active.

The design takes the readable team colors, selection and formation orders of classic RTS games, the large armies of Total Annihilation, and the simple capture-point objectives of small skirmish games. The faction palette draws on [Command & Conquer](https://store.steampowered.com/app/1213210/): blue Selene markings and graphite-and-red Helios armor. Green grass, sandy patches and grey rock outcrops replace the lunar dust and grid. The command console uses shaded steel panels, silver bevels, recessed displays and light text. The terrain adds fine procedural surface detail at close range. Models and interface icons are generated locally; there are no remote Defold project dependencies.

The command console uses raised metal controls, inset radar and HQ gauges, vehicle portraits, and [Press Start 2P](https://github.com/google/fonts/tree/main/ofl/pressstart2p) type. The bundled TrueType font is rendered at 8, 16 and 24 pixels with nearest filtering. Its [SIL Open Font License](main/fonts/OFL.txt) is also included in the built content.

## Play

After building the project, start the native game with:

```sh
./tools/run.sh
```

The game starts CPU vs CPU. **Tab** switches the Selene army between CPU and player control. Selecting Selene units also takes player control. The Helios army remains CPU controlled. Selected units have bright green hull highlights and rings drawn above the models. The objective stays at the top of the screen; destroying an HQ shows a central victory panel with restart instructions.

The console provides buttons for move, attack-move, hold, pause, control mode, restart, army size, and camera views. Orders issued through the console take player control. Unit portraits select a type, and the radar moves the camera.

| Input | Action |
| --- | --- |
| Left drag | Select Selene units inside the screen rectangle, in either drag direction |
| Left click | Select the nearest Selene unit |
| Shift + select | Add units to the selection |
| Right click | Move selected units in a formation |
| F, then right click | Attack-move to a destination |
| H | Hold position |
| Q | Select all living Selene units |
| 1 / 2 / 3 / 4 | Select scouts / assault / siege / support; Shift adds the type |
| Unit portraits | Select that unit type |
| WASD / arrow keys | Pan the camera |
| Mouse wheel | Smoothly zoom toward the new camera scale |
| Minimap click or drag | Move the camera |
| Home / End | Home base / battlefield overview |
| Tab | CPU vs CPU / player vs CPU |
| Space | Pause or resume |
| + / - | Double/halve below 32K; add/remove 32,768 above it; restart the battle |
| R | Restart the current army size |

The default is **32,768 total units**; the stress limit is **262,144 across both teams**. Unit counts remain explicit; the game does not silently reduce its army to meet a frame budget. The large HUD number is the **live delta time between consecutive engine frames**, updated every frame. It includes pacing and waiting; it is not CPU execution time. The smaller **Sim** value is live elapsed time for the complete DataStore simulation chain, including worker synchronization and excluding rendering, GUI and physics. FPS is secondary and smoothed. Unit-count changes above 32,768 use steps of 32,768.

| Type | Health | Speed | Range | Role |
| --- | ---: | ---: | ---: | --- |
| Scout | 50 | 49 | 35 | Fast, light raider |
| Assault | 140 | 29 | 48 | Durable main force |
| Siege | 90 | 19 | 88 | Slow, long-range damage |
| Support | 75 | 34 | 42 | Repairs damaged allies |

## Build from source

This project needs the **experimental DataStore and extension-broadcast SDK branch**, including `dmsdk/datastore/datastore.h`, the native input APIs, and physics post-step events. A stock released editor/SDK cannot supply those APIs yet.

Use a prepared engine SDK with its host tools and a matching `bob-light.jar` or `bob.jar`. The helper prefers the local light compiler, supplies the checkout's builtins, isolates generated SDK headers, builds content locally, and links the extension into the matching engine. It does not upload sources to an extension server or modify the engine checkout.

Requirements: CMake 4+, Ninja, the engine's Java toolchain, Python 3, and the prepared Defold SDK. The engine's normal platform toolchain is used.

```sh
export DEFOLD_SOURCE=/path/to/defold-datastore-checkout
export DYNAMO_HOME=/path/to/prepared/dynamo_home
python3 tools/build.py --test
./tools/run.sh
```

Optional overrides: `--bob /path/to/bob-light.jar`, `--platform arm64-macos`, and `--jobs 8`. `--content-only` rebuilds editor-authored assets without relinking the engine.

Native extension source is also provided in the usual `moonbase/src` layout with `moonbase/ext.manifest`. The local CMake build is the supported path while these APIs are under development. The authored collection, glTF models, shaders, GUI and collision shapes can be inspected in the editor.

## Hidden, offscreen screenshots

The capture build never shows its window and renders into a color/depth/stencil target. The current engine bootstrap has no public hide-window setting, so CMake makes an isolated build copy of `engine.cpp` with window showing disabled. The engine checkout stays unchanged. The regular build remains available separately.

```sh
python3 tools/build.py --offscreen --test
python3 tools/capture.py --units 32768 --frames 1200 --capture-frame 600 \
    --output screenshots/overview.png
python3 tools/capture.py --units 8192 --frames 220 --capture-frame 100 \
    --smoke-test --output screenshots/selection.png
python3 tools/capture.py --units 8192 --camera-x -200 --span 200 \
    --frames 600 --capture-frame 400 --output screenshots/closeup.png
```

PNG conversion uses Pillow; `--output frame.ppm` needs no image library. Each capture writes a sibling `.log` and fails on engine/script errors. `--width` and `--height` control offscreen resolution. The capture path is implemented with the C++ graphics SDK, rather than desktop screenshots. Metal on macOS is the tested graphics path.

## DataStore and C++ structure

`engine.datastore = 1` creates the engine-owned store. The extension borrows it, creates a typed `moon.units` table, and inserts owned unit rows in batches. Every native member is registered with its name, type and offset. Stable per-unit indices refer to snapshots; no row pointer escapes a query reservation.

The schedule depends on `DATASTORE_EVENT_GAMEOBJECT_PRE_UPDATE`:

```text
input broadcast → grid/snapshot → decisions → movement → combat/objectives
                                    512/job    512/job
                      engine waits → Lua presentation → render
```

- `simulation.cpp` owns unit data, table/query lifecycle, selection, formation commands, spatial grid, targeting, crowd steering, damage, repairs, respawning, relays and victory.
- `moonbase.cpp` registers the extension, handles native input and physics broadcasts, connects the small Lua presentation API, and provides optional screenshot capture and input smoke tests.
- `geometry.cpp` owns native GPU buffers, presents shared compiled glTF meshes with instancing, and generates terrain, bases, rocks, relays, tracers and console artwork. Each visible unit uploads a 32-byte transform/team record; selection rings and minimap markers are also instanced. Terrain is static. Camera culling and low-detail glTF silhouettes reduce distant work. CPU arrays grow geometrically and retain high-water capacity for reuse. Every simulation and geometry function has a purpose comment.
- `main/main.script` binds resources, presents the native batches and releases render registrations before the renderer shuts down. The GUI and render scripts handle text and render passes. The render script calls `moonbase.submit()` while recording, before render-list consumption. There are no per-unit Lua updates or game objects.
- `main/terrain.collisionobject` supplies the Defold-owned Bullet world with a ground plane and static blockers. Native post-step events perform terrain picking for pending orders. The extension never steps or owns that physics world.

Crowd movement uses circle-based steering and the same blocker layout as the authored scene. It deliberately does not create a Bullet body per unit. Neighbor visits are bounded, and target acquisition is staggered over eight frames. These are RTS crowd approximations, rather than exact rigid-body contact or optimal pathfinding. The snapshot separates parallel reads from writes; combat resolves damage in a deterministic pass after workers finish. Target lists are separated by team so dense groups of allies cannot hide enemies behind the bounded search limit. [Queries, update dependencies and culling](docs/simulation.md) are described separately.

Queries and callbacks are removed before the borrowed store is destroyed. Geometry holds model/material factory references and borrows their shared vertex/index buffers. Collection finalization releases render items, registrations, owned buffers and references while the render context is still alive. The extension releases the simulation later.

The four editable `.gltf` files each contain close and distant meshes. Eight `.model` resources select those meshes. An unspawned factory pins their normal authored dependencies in the build graph; it creates no unit objects. Regenerate the bundled vehicles with `python3 tools/generate_models.py`, or replace them in the editor while preserving the selected mesh names and the single static primitive/material contract. The shader uses glTF vertex alpha as the team-color mask.

There are **no authored empty `.buffer` or `.mesh` files**. Dynamic streams are created with `dmGraphics::NewVertexBuffer()` and updated from C++; no duplicate CPU buffer resource is needed.

## Tests

Build and run the native regression tests with:

```sh
python3 tools/build.py --test
```

The tests cover angled projection/picking, reverse and additive selection, enemy
filtering, formations, pause, population reset, combat, relay capture, bounds,
dense-army targeting, and identical results with zero and two worker threads.
The optional offscreen smoke test drives SDK device packets through real input
broadcasts to exercise selection, terrain picking, console controls, camera
navigation, population steps, smooth zoom and victory feedback.

## Wasm-web

Build the WebGPU bundle as described below, then serve it locally. The server
opens no window:

```sh
python3 tools/run_web.py
# Open http://127.0.0.1:8000/ when you want to play.
```

The browser uses the same C++ DataStore simulation and native instanced renderer, with no DataStore worker threads in `wasm-web`. Browser requestAnimationFrame owns pacing. Query parameters support `?units=65536`, and optional `frames=600&smoke=1` for diagnostics.

Rebuild with a configured Emscripten toolchain (tested with Homebrew Emscripten 6.0.3-git):

```sh
export EMSCRIPTEN=/path/to/emscripten
python3 tools/build.py --platform wasm-web --graphics webgpu
```

WebGPU is the default browser build; `--graphics webgl` produces a WebGL comparison build. The helper isolates web outputs, Emscripten caches and SDK writes from the native build. Dawn’s `emdawnwebgpu` port is downloaded on the first WebGPU build. The sample enables the checkout’s current WebGPU C API path and WGSL shaders; a generated backend copy supplies a finite depth-clear default required by the port. The engine checkout stays unchanged. Bob resolves HTML locally and then uses the adapter manifest to include its WebGPU startup probe. It builds missing Bullet libraries from the checkout. The sample CMake configuration uses the browser virtual filesystem, current browser baselines and `-fno-strict-aliasing` for the SDK's scalar vectormath. The HTML creates raw archive files synchronously for compatibility with newer Emscripten preload behavior.

# ByteBuddy — Desktop Companion

A tiny animated sprite that lives on your desktop, wanders around, jumps toward your cursor, and generally makes your screen a little less lonely.

Built with **C++20**, **SDL3**, and **Dear ImGui**. Windows-only for now (uses Win32 tray icon + transparency APIs).

---

## Features

- **Transparent overlay** — renders on top of all your windows; clicks pass straight through to whatever's underneath
- **System tray integration** — right-click the tray icon to show/quit, left-click to bring the window back
- **Global hotkey** — `Ctrl + Alt + P` toggles the settings panel from anywhere
- **Autonomous behaviour** — the buddy idles, grooms itself, walks, runs, sleeps, and jumps around on its own
- **Mouse-aware** — walks/runs/jumps toward your cursor and says *MEOW* when you get too close
- **Fully adjustable** — speed, scale, ground level, animation timing, and more via the live settings panel

---

## Controls

| Action                | How                                                 |
|-----------------------|-----------------------------------------------------|
| Open / close settings | `Ctrl + Alt + P`                                    |
| Minimise to tray      | `Escape`                                            |
| Restore from tray     | Left-click the tray icon                            |
| Quit                  | Right-click tray → **Quit**, or Settings → **Quit** |

---

## 📁 Project Structure

```
ByteBuddy/
├── include/
│   └── buddy.hpp          ← Buddy class declaration + full Doxygen docs
├── src/
│   └── buddy.cpp          ← Buddy class implementation
├── main.cpp               ← SDL3 + ImGui setup, main loop, Win32 tray
└── resources/
    ├── font/
    │   └── font.ttf
    ├── textures/
    │   └── cat/           ← Sprite sheets (one PNG per frame, named below)
    └── sfx/               ← Optional sound files
```

---

## Texture Naming Convention

Textures are loaded by the `JFLX::SDL3::TextureHandler` and looked up by name at runtime.  
The expected naming scheme is:

```
<buddy_name>_<state>_<frame_index>
```

| State     | Example filenames                         |
|-----------|-------------------------------------------|
| `idle_0`  | `cat_idle_0_0.png`, `cat_idle_0_1.png`, … |
| `idle_1`  | `cat_idle_1_0.png`, `cat_idle_1_1.png`, … |
| `clean_0` | `cat_clean_0_0.png`, …                    |
| `clean_1` | `cat_clean_1_0.png`, …                    |
| `walk`    | `cat_walk_0.png`, `cat_walk_1.png`, …     |
| `run`     | `cat_run_0.png`, `cat_run_1.png`, …       |
| `sleep`   | `cat_sleep_0.png`, `cat_sleep_1.png`, …   |

The frame counter wraps automatically when the next-index texture is missing, so animations can be **any length** — just add more frames.

### Jump Animation (fixed 7 frames, always required)

The jump arc is driven by a pre-computed trajectory and **must have exactly 7 frames**, one per phase:

| File             | Phase     | Arc position               |
|------------------|-----------|----------------------------|
| `cat_jump_0.png` | PreJump   | crouching, preparing       |
| `cat_jump_1.png` | MidJump   | leaving ground, rising     |
| `cat_jump_2.png` | MidAir    | at the apex                |
| `cat_jump_3.png` | MidFall_0 | descending (upper)         |
| `cat_jump_4.png` | MidFall_1 | descending (lower)         |
| `cat_jump_5.png` | Landing_0 | feet touching down         |
| `cat_jump_6.png` | Landing_1 | fully extended, last frame |

> **All other animations can have as many frames as you like.**  
> The jump strip is the only one with a fixed length requirement.

---

## Creating a Custom Buddy (🐱➡🐶)

1. Copy `resources/textures/cat/` to a new folder, e.g. `resources/textures/dog/`
2. Rename every file from `cat_*` to `dog_*`
    - e.g. `cat_idle_0_0.png` → `dog_idle_0_0.png`
3. Replace the images with your own sprites (keeping the same filenames)
4. In the **Settings panel** (`Ctrl + Alt + P`), change **Buddy Name** to `dog` and press `Enter`

That's it. All animation states, the jump arc, and the behaviour system will work automatically with the new sprites.

> The jump animation frames **must** stay named `_jump_0` through `_jump_6` and cover all seven phases. Everything else is completely flexible.

---

## ⚙️ Settings Panel (`Ctrl + Alt + P`)

| Setting                        | What it does                                               |
|--------------------------------|------------------------------------------------------------|
| **Buddy Name**                 | Which sprite folder to use (press Enter to apply)          |
| **Show Buddy**                 | Toggle sprite visibility without closing the app           |
| **Window**                     | Show/hide the overlay window                               |
| **Position X / Y**             | Drag the buddy to an exact pixel coordinate                |
| **Ground Level**               | The Y coordinate the buddy snaps back to after jumping     |
| **Speed**                      | Walk = `2 × speed` px/tick, Run = `4 × speed` px/tick      |
| **Scale**                      | Sprite scale multiplier (default `4×`)                     |
| **State time**                 | How many ticks a state is held before a new one is chosen  |
| **Min / Max Animation Length** | Random range for state duration                            |
| **Animation speed**            | Ticks between frame advances (lower = faster)              |
| **Facing**                     | Manually flip the sprite direction                         |
| **Change State**               | Force an immediate random state transition                 |
| **State / Frame**              | Live read-out of the current state and frame index         |
| **Jump phase**                 | Shown only while jumping — which of the 7 phases is active |
| **Quit**                       | Exit the application                                       |

---

## 🔨 Building

### Prerequisites

- CMake ≥ 3.20
- C++20-capable compiler (MSVC or MinGW on Windows)
- SDL3, SDL3_ttf, Dear ImGui (loaded via the `osSpecificLibs` CMake module)
- The `JFLX` helper library (TextureHandler, TextRenderer, AudioHandler)

### CMake

```cmake
cmake_minimum_required(VERSION 3.20)
project(ByteBuddy)

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "F:/Dropbox/Dropbox/CompilerUndScripts/CMake/modules")
include(osSpecificLibs)

add_executable(${PROJECT_NAME}
    main.cpp
    src/buddy.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE osSpecificLibs)
```

```bash
cmake -B build -S .
cmake --build build --config Release
```

### Run

Copy the `resources/` folder next to the compiled executable. ByteBuddy resolves all asset paths from `std::filesystem::current_path()` at startup.

---

## 🏗️ Architecture

```
main.cpp
│
├── SDL3 / Win32 setup
│   ├── Transparent borderless window (WS_EX_LAYERED + WS_EX_TRANSPARENT + WS_EX_TOOLWINDOW)
│   ├── System tray icon (message-only HWND, Shell_NotifyIcon)
│   └── Global hotkey (RegisterHotKey: Ctrl+Alt+P)
│
├── Dear ImGui overlay
│   └── renderImGuiSettings()   ← live settings panel, only active when gShowSettings == true
│
└── ByteBuddy::Buddy
    ├── updateBuddy(mouseX, mouseY)   ← called every frame
    │   ├── Frame animation (auto-wrapping)
    │   ├── State machine (random transitions with configurable timing)
    │   ├── Movement (Walk / Run toward target, Jump along trajectory)
    │   └── Gravity + ground clamping
    └── renderBuddy()                 ← SDL_RenderTextureRotated with flip
```

### State Machine

States are chosen randomly each tick when the state timer expires. Each state has a randomly-chosen duration within `[stateTimerMin, stateTimerMax]` ticks.

```
Idle_0 ──┐
Idle_1   │
Clean_0  ├──► random next state after timer expires
Clean_1  │
Walk     │
Run      │
Jump  ───┘
Sleep
```

Walk and Run pick a destination (50 % mouse position, 50 % random X within window). Jump computes a full parabolic arc via `updateTrajectory()` — a deque of per-tick keyframes that the buddy consumes one entry at a time.

---

## 🧩 Dependencies

| Library             | Purpose                                   |
|---------------------|-------------------------------------------|
| SDL3                | Window, renderer, input, events           |
| SDL3_ttf            | Font rendering for the MEOW speech bubble |
| Dear ImGui          | Settings overlay panel                    |
| JFLX TextureHandler | Sprite loading and rendering              |
| JFLX TextRenderer   | Text drawing                              |
| JFLX AudioHandler   | Sound effect loading (optional)           |
| Win32 API           | Tray icon, transparency, global hotkey    |

---

## 📝 Notes

- The overlay is capped to ~16 fps (60 ms frame budget) to keep CPU usage low.
- Click-through (`WS_EX_TRANSPARENT`) is disabled only while the cursor is over the settings panel, so you can always interact with windows underneath.
- The buddy is hidden from `Alt+Tab` and the taskbar via `WS_EX_TOOLWINDOW`.
- Closing the window with `Escape` minimises to the system tray rather than quitting.

---

*Made with Paulaner Spezi and not much Sleep <3.*

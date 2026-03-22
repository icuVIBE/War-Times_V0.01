# War Times — Custom 3D FPS Engine

**War Times** is a first-person multiplayer shooter built on a custom DirectX 11 engine written in C++17. The engine features a full rendering pipeline with shadow mapping, SSAO, post-processing, AMD FSR upscaling, GPU-driven particle effects, and a built-in level editor — all running natively on Windows.

> **Version:** 0.1.0 (Early Development)

---

## Table of Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Getting the Source](#getting-the-source)
- [Building](#building)
- [Running](#running)
- [Project Structure](#project-structure)
- [Controls](#controls)
- [Level Editor](#level-editor)
- [Weapons](#weapons)
- [Tools](#tools)
- [Roadmap](#roadmap)
- [License](#license)

---

## Features

- **DirectX 11 Renderer** — Forward rendering with shadow mapping, SSAO, and post-processing
- **AMD FSR Upscaling** — FidelityFX Super Resolution for performance scaling
- **GPU Particle System** — Compute-shader-driven particle simulation and rendering (billboards + trails)
- **Built-in Level Editor** — Place entities, edit properties, save/load `.wtlevel` files
- **Physics** — Integrated physics world with fracture/destruction and debris systems
- **Weapon System** — Data-driven weapons loaded from `.wpn` files with inventory and HUD
- **AI** — AI agents with navigation grid pathfinding
- **Networking** — Client/server architecture with packet serialization and entity replication
- **Procedural Generation** — Level and warfield generators
- **Sky & Environment** — Dynamic sky rendering, time/weather system
- **Assimp Integration** — Import OBJ, FBX, glTF, and Blender files
- **Dear ImGui** — Editor UI and debug overlays

---

## Prerequisites

Before building, make sure you have the following installed:

| Requirement | Version |
|---|---|
| **Windows** | 10 or 11 (x64) |
| **Visual Studio** | 2022 (v17.x) with "Desktop development with C++" workload |
| **CMake** | 3.20 or newer |
| **Git** | Any recent version |
| **Windows SDK** | 10.0.19041.0 or newer (included with VS 2022) |
| **DirectX 11 Runtime** | Ships with Windows 10/11 |

> CMake and Git should be available on your system PATH. Visual Studio's installer can install CMake for you — or grab it from [cmake.org](https://cmake.org/download/).

---

## Getting the Source

```bash
git clone https://github.com/<your-username>/WarTimes.git
cd WarTimes
```

> **Note:** The first build will automatically download [Assimp v5.4.3](https://github.com/assimp/assimp) via CMake FetchContent. An internet connection is required for the initial configure step.

---

## Building

### Option A — Command Line (Recommended)

**1. Configure the project:**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

**2. Build (Debug):**

```bash
cmake --build build --config Debug
```

**3. Build (Release):**

```bash
cmake --build build --config Release
```

### Option B — CMake Presets

```bash
cmake --preset windows-debug
cmake --build --preset debug-build
```

Or for release:

```bash
cmake --preset windows-release
cmake --build --preset release-build
```

### Option C — Visual Studio

1. Open the generated `build/WarTimes.sln` in Visual Studio 2022
2. Set **WarTimes** as the startup project
3. Select **Debug** or **Release** configuration
4. Press **F5** to build and run

### Build Output

The compiled executable and all runtime assets are placed in:

```
build/bin/WarTimes.exe
build/bin/shaders/       ← copied automatically
build/bin/models/        ← copied automatically
build/bin/textures/      ← copied automatically
build/bin/levels/        ← copied automatically
```

---

## Running

After building, run the game from the project root:

```bash
# Run directly
build\bin\WarTimes.exe

# Or load a specific level
build\bin\WarTimes.exe -level levels/test_arena.wtlevel
```

> The working directory must contain (or be) the `build/bin/` folder so the engine can find shaders, textures, models, and levels at their expected relative paths.

---

## Controls

*(Default bindings — may change during development)*

| Action | Key |
|---|---|
| Move | W / A / S / D |
| Look | Mouse |
| Shoot | Left Mouse Button |
| Aim / Zoom | Right Mouse Button |
| Reload | R |
| Interact | E |
| Inventory | Tab / I |
| Jump | Space |
| Sprint | Left Shift |
| Toggle Editor | F1 (or editor shortcut) |

---

## Level Editor

War Times includes a built-in level editor powered by Dear ImGui. From the editor you can:

- Place and transform entities in the 3D viewport
- Edit entity properties via inspector panels
- Save and load levels as `.wtlevel` files
- Access section management tools

Level files are stored in the `levels/` directory and automatically copied to the build output.

---

## Weapons

Weapons are defined as data files in the `weapons/` directory using the `.wpn` format:

```
weapons/
├── m4a1.wpn
├── pistol.wpn
├── rifle.wpn
├── sg553.wpn
└── shotgun.wpn
```

---

## Tools

### Blender Mesh Exporter

A Python script for Blender that exports models to the engine's custom `.mesh` format:

```
tools/blender_mesh_exporter.py
```

Use this to author new 3D assets in Blender and export them for use in the engine.

---

## Roadmap

The engine is being developed in phases:

- **Phase 1** — Core runtime: rendering, ECS, physics, networking, asset management *(current)*
- **Phase 1.5** — Production systems: animation, audio, UI/HUD, VFX, anti-cheat, level editor
- **Phase 2** — Next-gen: ray tracing, virtual geometry, global illumination, console ports, live service

---

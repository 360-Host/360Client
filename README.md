# 360Client

One file. Everything included.

360Client is a Minecraft Bedrock client built on Flarial.
Download one `.exe` — the launcher and client DLL are merged inside it.

---

## Features

- **Blue theme** — left-rail navigation, 360 branding
- **Entity Culler** — skips rendering mobs and items outside your FOV
- **Chunk FOV Culling** — chunks behind you turn invisible but stay loaded. Look back — instant
- **LOD System** — lower detail at long render distances for FPS gains
- **FPS Presets** — Low / Medium / High / Custom
- **Smooth Lighting** — gamma fades instead of hard snapping
- **Self-updating** — checks GitHub and swaps itself automatically

---

## Repo files

| File | What it does |
|---|---|
| `Launcher360.cs` | Entire launcher — UI, injection, self-update |
| `360Client.hpp` | All C++ headers — frustum math, chunk cache, FPS system |
| `360Client.cpp` | All C++ modules — entity culling, chunk LOD, FPS presets, lighting |
| `Flarial.Launcher.csproj` | C# project config |
| `.github/workflows/build.yml` | Builds everything automatically on GitHub |

---

## Credits

Base client: [Flarial / dll-oss](https://github.com/flarialmc/dll-oss)  
Base launcher: [Flarial Launcher](https://github.com/flarialmc/launcher)  
360Client: Zaki / 360-Host

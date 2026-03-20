# 360Client

One file. Everything included.

360Client is a Minecraft Bedrock client built on Flarial.
Download one `.exe` — the launcher and client DLL are merged inside it.

---

## What's in the repo

| File | What it does |
|---|---|
| `Launcher360.cs` | Entire launcher — UI, injection, self-update |
| `360Client.hpp` | All C++ headers — frustum math, chunk cache, FPS system |
| `360Client.cpp` | All C++ modules — entity culling, chunk LOD, FPS presets, lighting |
| `Flarial.Launcher.csproj` | C# project config |
| `build.yml` | GitHub Actions — builds everything automatically |
| `README.md` | This file |

---

## Features

- **Blue theme** — left-rail navigation, 360 branding
- **Entity Culler** — skips rendering mobs and items outside your FOV
- **Chunk FOV Culling** — chunks behind you turn invisible but stay loaded. Look back — instant
- **LOD System** — lower detail at long render distances for FPS gains
- **FPS Presets** — Low / Medium / High / Custom. One tap on low-end PCs
- **Smooth Lighting** — gamma fades in/out instead of hard snapping
- **Self-updating** — launcher checks GitHub and swaps itself automatically

---

## How to get the .exe (takes ~10 minutes)

1. Go to your repo → **Releases** → **Draft a new release**
2. Click **Choose a tag** → type `v1.0.0` → click **Create new tag**
3. Click **Publish release**
4. Go to the **Actions** tab and watch it build
5. When done, go back to **Releases** — `360Launcher.exe` is there to download

---

## Setup (one time, on mobile)

Upload these 6 files to the root of your repo:

| File | Rename before uploading? |
|---|---|
| `README.md` | No |
| `Launcher360.cs` | No |
| `360Client.hpp` | No |
| `360Client.cpp` | No |
| `Flarial.Launcher.csproj` | No |
| `build.yml` | **Yes — rename to** `.github/workflows/build.yml` |

After uploading, open `Launcher360.cs` in GitHub's editor,
find `YOUR_USERNAME` and replace it with your actual GitHub username. Commit.

That's it.

---

## Credits

Base client: [Flarial / dll-oss](https://github.com/flarialmc/dll-oss)
Base launcher: [Flarial Launcher](https://github.com/flarialmc/launcher)
360Client: Zaki / 360

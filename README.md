<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->
<!-- lang: en-GB -->

<h1 align="center">
  <br>
  <a href="https://git.eden-emu.dev/eden-emu/eden"><img src="https://github.com/user-attachments/assets/35031845-4bfa-4916-8bf5-bc3bce67d5e2" alt="Eglantine" width="200"></a>
  <br>
  <b>Eglantine</b>
  <br>
</h1>

<h4 align="center"><b>Eglantine</b> is a free and opensource (FOSS) Switch 2 emulator, derived from Eden, Yuzu and Sudachi - started by developer Camille LaVey.
It's written in C++ with portability in mind, with builds for Windows, Linux and macOS
</h4>

<p align="center">
    </a>
    <a href="https://discord.gg/jCCZ58VExk">
        <img src="https://img.shields.io/discord/1367654015269339267?color=5865F2&label=Eglantine&logo=discord&logoColor=white"
            alt="Discord">
    </a>
  
</p>

<p align="center">
  <a href="#about-this-fork">About this fork</a> |
  <a href="#compatibility">Compatibility</a> |
  <a href="#development">Development</a> |
  <a href="#building">Building</a> |
  <a href="#download">Download</a> |
  <a href="#support">Support</a> |
  <a href="#license">License</a>
</p>

## About this fork

This is an unofficial fork of [Eglantine](https://github.com/Djihads80/Eglantine), aimed at raising the frame rate in crowded areas of Fire Emblem: Fortune's Weave. The work is on the `perf/crowded-areas` branch. Please report problems caused by these changes here, not upstream.

### Results

Measured in crowded areas on a Ryzen 7 5800X3D and RTX 4070 Ti at 2x resolution. The game targets 30 FPS.

| Area | Before | With both settings below |
|---|---|---|
| Crowded town | 17 FPS | at least 26 FPS |
| Heaviest crowd tested | about 9 FPS | 28-30 FPS |

These come from play testing in a limited part of the game on one PC, so other areas may behave differently. They were measured on the PGO build.

### Which build to download

| Build | Videos | Notes |
|---|---|---|
| **MSVC** | Play normally | The one to choose if you want the game's movies. It hasn't had the same fps comparison |
| **PGO** (clang, MinGW) | **Don't play.** The screen stays blank and the game skips them | Its FFmpeg package has no AV1 decoder, and this game's movies are AV1. The results above come from this build |

### What changed

- **Texture array limit setting:** shaders that pick a texture at runtime made the emulator bind up to 1024 textures per array on every draw. In these crowds, that was nearly half of the GPU back thread's busy time. A lower limit skips most of that work.
- **Run ahead of the GPU setting:** adds a **Relaxed** mode, which lets the game prepare its next frame while the GPU threads finish the current one, without the extra wait in the existing run-ahead mode.
- **Storage buffer sizing:** upstream sized every native compute storage buffer from its mapping, up to 8 MiB, which caused flickering lines on the world map and a slower title screen in this game. This fork uses upstream's narrower version from [#561](https://github.com/Djihads80/Eglantine/pull/561), limited to DLSS passes. Upstream later reverted that change in [#569](https://github.com/Djihads80/Eglantine/pull/569) without giving a reason, so it may affect other games.
- **Fire Emblem: Fortune's Weave 60 FPS setting:** keeps the game's own frame rate target at 60, which reached 43-46 FPS in the heaviest crowd tested and looked like normal speed. This hasn't been timed, and the game may run slower wherever it can't hold 60 FPS. It only works on the game version with build ID `DB93B0125F485060`.
- **Thread names on MinGW builds:** profilers and debuggers can now tell the emulator's threads apart.

### Using the settings

Both settings are on the advanced graphics tab and can be set per game. They are off by default, so the emulator behaves normally until you change them. For Fortune's Weave, set:

| Setting | Value | If something goes wrong |
|---|---|---|
| Texture array limit | 64 | Wrong textures: raise it to 128 or 256 |
| Run ahead of the GPU | Relaxed | Freezes, crashes or rubberbanding: use **Previous frame** or **Auto** |
| Fire Emblem: Fortune's Weave: 60 FPS (optional) | On | Slow motion: turn it off or lower the resolution. Don't use `EDEN_NX2_SWAP_INTERVAL` with it, or the game runs too fast |

Keep a backup save when using **Relaxed**. Running ahead can make a game overwrite data the GPU still needs, and upstream has seen freezes and crashes from this in other games. Neither setting has been tested with other games, so set them per game rather than globally.

The environment variables `EDEN_NX2_TEXTURE_ARRAY_CAP` and `EDEN_NX2_FRAMES_AHEAD` override these settings when set.

### Known issues

- Short hitches can still happen, with the worst single frames taking up to about 0.8 seconds.
- A torch's light in one cave disappears at a certain distance. This may be an older bug, but that hasn't been confirmed.
- Settings of 64 and Relaxed have only been tested in the parts of the game played so far.
- Videos don't play on the PGO build. Use the MSVC build if you want to see them.
- Real NVIDIA DLSS is optional and off by default. It needs NVIDIA's Streamline, which the Graphics tab can download. Without it, the game uses its own DLSS network at 1x and FSR 3.1 at higher resolutions.

### Credits

The 60 FPS setting relies on the frame rate target's location and behavior as documented by [seedds/nxnative](https://github.com/seedds/nxnative) and [weile10000/FEFW-60fps](https://github.com/weile10000/FEFW-60fps). The setting is this fork's own implementation and doesn't use their files or cheat code. Please don't report problems with it to them.

## Compatibility

The emulator is capable of running Mario Kart World at decent/full speed depending on specs and configurations, and Splatoon Raiders at variable performances

A list of supported games will be available in future. Please be patient.

## Development

For development discussions, please join us on [Discord](https://discord.gg/jCCZ58VExk)

If you would like to contribute, we are open to new developers and pull requests. Please ensure that your work is of a high standard and properly documented. You can also contact any of the developers on Discord or Stoat to learn more about the current state of the emulator.

## Documentation

We have a user manual! See our [User Handbook](./docs/user/README.md).

## Building

See the [General Build Guide](docs/Build.md)

For information on provided development tooling, see the [Tools directory](./tools)

## Download

You can download the latest releases from [here](https://github.com/Djihads80/Eglantine/releases).

## Support

We currently do not take in any donations for the time being

If you would prefer to support us in a different way, please join our [Discord](https://discord.gg/jCCZ58VExk)

## Star History

<a href="https://www.star-history.com/?repos=djihads80%2Feglantine&type=date&legend=bottom-right">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=djihads80/eglantine&type=date&theme=dark&legend=bottom-right" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=djihads80/eglantine&type=date&legend=bottom-right" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=djihads80/eglantine&type=date&legend=bottom-right" />
 </picture>
</a>

## License

Eglantine is licensed under the GPLv3 (or any later version). Refer to the [LICENSE.txt](https://github.com/Djihads80/Eglantine/blob/nx2/LICENSE.txt) file.

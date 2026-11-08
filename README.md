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

With the 60 FPS setting on, the heaviest crowd tested ran at 52-53 FPS, and 55-56 FPS with the two uniform buffer and texture handle settings also on.

These come from play testing in a limited part of the game on one PC, so other areas may behave differently. They were measured on the PGO build.

### Which build to download

The Windows release is the **PGO** build (clang, MinGW). It now plays the game's movies, so the separate MSVC build from earlier releases isn't needed. It uses MSYS2's FFmpeg, which includes an AV1 decoder, and this game's movies are AV1.

### What changed

- **Texture array limit setting:** shaders that pick a texture at runtime made the emulator bind up to 1024 textures per array on every draw. In these crowds, that was nearly half of the GPU back thread's busy time. A lower limit skips most of that work.
- **Run ahead of the GPU setting:** adds a **Relaxed** mode, which lets the game prepare its next frame while the GPU threads finish the current one, without the extra wait in the existing run-ahead mode.
- **Storage buffer sizing:** upstream sized every native compute storage buffer from its mapping, up to 8 MiB, which caused flickering lines on the world map and a slower title screen in this game. This fork uses upstream's narrower version from [#561](https://github.com/Djihads80/Eglantine/pull/561), limited to DLSS passes. Upstream later reverted that change in [#569](https://github.com/Djihads80/Eglantine/pull/569) without giving a reason, so it may affect other games.
- **Fire Emblem: Fortune's Weave 60 FPS setting:** keeps the game's own frame rate target at 60, which reached 43-46 FPS in the heaviest crowd tested and looked like normal speed. This hasn't been timed, and the game may run slower wherever it can't hold 60 FPS. It only works on the game version with build ID `DB93B0125F485060`.
- **Uniform buffer and texture handle settings:** two optional speedups for the GPU back thread from [Frizti/Lichii](https://github.com/Frizti/Lichii). One checks small uniform buffers for changes directly in game memory and skips copying the unchanged ones. The other remembers where the texture handles read on every draw live in memory. Together they raised the heaviest crowd tested from about 53 to 55-56 FPS with the 60 FPS setting on.
- **PGO profile trained on Fortune's Weave:** the PGO build's profile now includes about an hour of this game, weighted 3 to 1 over upstream's profile for the graphics and shader code. In testing this was within measurement noise of upstream's profile, so expect no real change.
- **Movies on the PGO build:** it now links MSYS2's FFmpeg, which can decode AV1.
- **Recommended settings applied automatically:** when Fortune's Weave starts, the settings in the table below are applied, along with GPU accuracy **Fast** and asynchronous shader compilation **on**. Each one is applied only if you haven't changed it yourself, globally or for the game. The 60 FPS setting stays off.
- **Fewer shader-compile stutters:** a series of shader compiler commits from Wael3rd's [PokeDinner](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork) fork. Pipelines that run the same shaders now share them instead of translating them again, and with asynchronous shader compilation on, new Switch 2 pipelines are translated on worker threads instead of the GPU thread. In a 40 minute session this halved the shader work on the GPU thread. The longest hitches mostly have other causes, so they didn't become less frequent.
- **Built-in frame generation fixes:** the **Optical Flow Interpolation** frame generation in the graphics settings showed no visible effect, for two reasons. Each generated frame was a copy of the next real frame, and generated frames were sent straight after each other, so on a monitor faster than twice the game's frame rate they flashed for a single refresh. Both are fixed. The pacing fix needs **Asynchronous presentation** on. The interpolation itself is simple, so expect smearing on fast motion and UI.
- **Thread names on MinGW builds:** profilers and debuggers can now tell the emulator's threads apart.

### Using the settings

All of these settings are on the advanced graphics tab and can be set per game. When Fortune's Weave starts, the values below are used for any of them you haven't changed (see **Use recommended settings for Fire Emblem: Fortune's Weave**, which is on by default). For other games they are off by default. The recommended values for Fortune's Weave are:

| Setting | Value | If something goes wrong |
|---|---|---|
| Texture array limit | 64 | Wrong textures: raise it to 128 or 256 |
| Run ahead of the GPU | Relaxed | Freezes, crashes or rubberbanding: use **Previous frame** or **Auto** |
| Fire Emblem: Fortune's Weave: 60 FPS (optional) | On | Slow motion: turn it off or lower the resolution. Don't use `EDEN_NX2_SWAP_INTERVAL` with it, or the game runs too fast |
| Compare uniform buffers in place | On | Wrong colours or flickering: turn it off |
| Cache texture handle lookups | On | Wrong textures: turn it off |

Keep a backup save when using **Relaxed**. Running ahead can make a game overwrite data the GPU still needs, and upstream has seen freezes and crashes from this in other games. None of these settings have been tested with other games, so set them per game rather than globally.

The environment variables `EDEN_NX2_TEXTURE_ARRAY_CAP`, `EDEN_NX2_FRAMES_AHEAD`, `EDEN_NX2_UBO_INPLACE` and `EDEN_NX2_HANDLE_PTR_CACHE` override these settings when set.

### Known issues

- Short hitches can still happen, with the worst single frames taking up to about 0.8 seconds.
- In the heaviest crowd tested, the 60 FPS setting holds about 46-56 FPS at 2x with NVIDIA DLSS, depending on the view. None of the emulator's threads is fully busy there, so the remaining limit isn't one obvious piece of work.
- A torch's light in one cave disappears at a certain distance. This may be an older bug, but that hasn't been confirmed.
- Settings of 64 and Relaxed have only been tested in the parts of the game played so far. The uniform buffer and texture handle settings have had one short test in the heaviest crowd.
- One play session ended with the graphics driver resetting the GPU ("device loss"), which closes the emulator. It hasn't happened again, and the cause isn't known.
- Real NVIDIA DLSS is optional and off by default. It needs NVIDIA's Streamline, which the Graphics tab can download. Without it, the game uses its own DLSS network at 1x and FSR 3.1 at higher resolutions.

### Credits

The 60 FPS setting relies on the frame rate target's location and behavior as documented by [seedds/nxnative](https://github.com/seedds/nxnative) and [weile10000/FEFW-60fps](https://github.com/weile10000/FEFW-60fps). The setting is this fork's own implementation and doesn't use their files or cheat code. Please don't report problems with it to them.

The shader reuse and shader compiler changes are Wael3rd's, from PokeDinner. The commits keep their authorship.

The uniform buffer and texture handle settings are ported from [Frizti/Lichii](https://github.com/Frizti/Lichii), where Dr. Sergio wrote them. The commits keep their authorship.

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

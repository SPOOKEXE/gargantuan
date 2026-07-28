<div align="center">

<img src="./assets/github/banner.png" alt="Gargantuan" width="656px" />
<br/>
<img src="./assets/github/demo-sphere.gif" alt="Gargantuan" width="324px" />
<img src="./assets/github/demo-waveform.gif" alt="Gargantuan" width="324px" />

<h3>An Independent Game Engine for Roblox Developers</h3>

<a href="https://discord.gg/wTudGB7cJA">
<img src="https://img.shields.io/discord/1470469501790457858?logo=discord&logoColor=white&label=Discord&color=5865F2&style=flat-square" alt="Discord" />
</a>
<a href="./LICENSE.md">
<img src="https://img.shields.io/github/license/teamfireworks/gargantuan?style=flat-square&label=License" alt="MPL-2.0 License" />
</a>
<a href="https://discord.gg/9Fuv68NcSt">
<img src="https://img.shields.io/badge/-Made_by_Team_Fireworks-F8F1E9?style=flat-square&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB3aWR0aD0iMTciIGhlaWdodD0iMTciIHZpZXdCb3g9IjAgMCAxNyAxNyIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPHBhdGggZD0iTTEwLjMxMDUgMC4wNDM5NDUzTDkuODY4MTYgNi4xOTkyMkwxNi4wMjQ0IDUuNzU3ODFMMTYuMDY4NCA2LjAxOTUzTDguNTQ4ODMgOC4wMzMyTDEyLjY4NzUgOS4xNDI1OEwxMi43NjE3IDkuMTYzMDlMMTIuNzgyMiA5LjIzNzNMMTQuMDE5NSAxMy44NTY0TDEzLjg1NjQgMTQuMDE5NUw5LjIzNzMgMTIuNzgyMkw5LjE2MzA5IDEyLjc2MTdMOS4xNDI1OCAxMi42ODc1TDguMDMzMiA4LjU0ODgzTDYuMDE5NTMgMTYuMDY4NEw1Ljc1NzgxIDE2LjAyNDRMNi4xOTkyMiA5Ljg2ODE2TDAuMDQzOTQ1MyAxMC4zMTA1TDAgMTAuMDQ4OEw3LjUxODU1IDguMDM0MThMMy4zODA4NiA2LjkyNTc4TDMuMzA2NjQgNi45MDUyN0wzLjI4NjEzIDYuODMxMDVMMi4wNDg4MyAyLjIxMTkxTDIuMjExOTEgMi4wNDg4M0w2LjgzMTA1IDMuMjg2MTNMNi45MDUyNyAzLjMwNjY0TDYuOTI1NzggMy4zODA4Nkw4LjAzNDE4IDcuNTE4NTVMMTAuMDQ4OCAwTDEwLjMxMDUgMC4wNDM5NDUzWiIgZmlsbD0iI0ZGMDA0RCIvPgo8L3N2Zz4K&logoColor=%23FF004D&logoSize=auto&labelColor=%23F8F1E9&color=%23FF004D" alt="Made by Team Fireworks" />
</a>

</div>

## About Gargantuan

Gargantuan is an 3D game engine, scriptable using Luau, independently developed
and maintained by Team Fireworks.

- **Gargantuan is powerful,** boasting a feature rich 2D and 3D featureset.
- **Gargantuan is productive,** with a familiar Luau API surface that enables rapid prototyping.
- **Gargantuan is multiplatform,** bringing one game for MacOS, Windows, Linux, mobile, and VR.

And finally,

- **Gargantuan is 100% yours,** From the platform, assets, multiplayer, and even core scripts.

Sparked your interest? [Read the documentation.](./docs/index.md)

## Status

Gargantuan is in its infancy! Development has just began near the start of July 2026.

As of July 26th 2026, the foundation has been laid out. Datatypes such as
Instances, Vectors, and Signals all implemented, alongside basic instance
classes like parts and the data model rendered using SDL3's GPU API. Gargantuan
is just missing more instance classes.

A major milestone is to run
[Welcome To Hell](https://github.com/welcomestohell/roblox-archive), our
flagship tower-obby game. We hope to have a usable Gargantuan Studio before
2027, and a feature-rich 1.0 release by March 2028.

Please understand Gargantuan is mostly developed by
[godmothersfire](https://godmothersfire.github.io/), the lead developer of
Team Fireworks. Gargantuan welcomes your contribution and support, even if it's
just messing around with the engine :)

For more development updates, [join our Discord server!](https://discord.gg/wTudGB7cJA)

## Prior Art

Gargantuan's design were informed by several other game engines:

| Resource                                                            | Info                                                            |
| ------------------------------------------------------------------- | --------------------------------------------------------------- |
| [Kinemium Engine](https://github.com/Qquaded/Kinemium-Engine)       | Initial reference implementation for some datatypes             |
| [Phoenix Engine](https://github.com/PhoenixWhitefire/PhoenixEngine) | Initial reference implementation for Instances and the renderer |
| [Kitbash'd](https://github.com/kitbashd)                            | Previously inspired the renderer, now irrelevant                |
| [Flux](https://github.com/thegalaxydev/flux)                        | Inspired the architecture of instances and userdatas            |
| [Librebox](https://github.com/StayBlue/librebox-demo/)              | Examples to bugtest the Gargantuan engine                       |
| [Roblox Creator Documentation](https://create.roblox.com)           | API design inspirations                                         |

## License

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Legal Notice

Gargantuan is an independent open-source game engine created and maintained by
godmothersfire, who represents Team Fireworks.

Gargantuan is an independent project and is NOT affiliated with, authorized by,
endorsed by, or in any way officially connected with Roblox Corporation.
"Roblox" is a registered trademark of Roblox Corporation.

No reverse engineering, decompilation, or extraction of proprietary binaries,
source code, or assets belonging to Roblox Corporation was performed or utilized
in developing Gargantuan. Gargantuan is built from scratch.

API features such as Instances and data types are implemented solely for
developer familiarity, platform portability, and software interoperability under
applicable fair use law including but not limited to the Copyright Act of 1976,
17 U.S.C. § 107.

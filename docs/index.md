# Introduction

_Note: Gargantuan will have it's own docsite eventually._

FYI: Gargantuan is in it's infancy and should not be used yet. This page is for
people who want to work on Gargantuan's core.

Before you contribute, read the [TODO.md](../TODO.md)!! Also, please ask
godmothersfire in the Discord server before you try any big changes. I don't
want you wasting time to get a PR closed :(

## Prerequisites

- cmake
- ninja
- glslc for compiling shaders
- spirv-cross and the XCode toolchain IF compiling Metal shaders

On Windows, install the
[Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) and Visual Studio 2022
with the "Desktop development with C++" workload. The SDK provides `glslc`, and
its `VULKAN_SDK` environment variable is how the build finds `shaderc` so
runtime shaders compile in-process instead of shelling out.

## Configure

```sh
rm -rf build
mkdir build
cmake -B build
```

### Windows

MSVC is not on `PATH` by default, so Ninja cannot find the compiler unless the
shell has been through `vcvars64.bat` first. Run both commands from a
**x64 Native Tools Command Prompt for VS 2022**, or set the environment up in
the shell you already have:

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`SDL3.dll` and `shaderc_shared.dll` are copied next to `build\gargantuan.exe`
after every link, so the executable runs from where it was built:

```bat
build\gargantuan.exe
```

Configuring without `-G Ninja` picks the Visual Studio generator instead. That
works, but it is a multi-config generator: pass `--config Release` to
`cmake --build` and the executable lands in `build\Release\` rather than
`build\`.

## Testing

```sh
cmake -B build
```

Create a `Testbed.luau` script in the repository root (it is not tracked by
Git), write some code (there's a few `examples` to use!), and then run the
program:

```sh
./build/gargantuan
```

For now, set Luau LSP's `platform` to Roblox. Eventually, Gargantuan will have
procedurally generated type definitions for consumption, alongside proper
project management and a test framework.

## Profiling

The engine is instrumented for [Tracy](https://github.com/wolfpld/tracy). The
client is compiled in by default and collects nothing until Tracy's profiler
connects, so an ordinary build pays an atomic read per zone.

Build the tools once. They are separate programs, so they are off by default:

```sh
cmake -B build -DGARGANTUAN_TRACY_TOOLS=ON
cmake --build build --target tracy-profiler tracy-capture tracy-csvexport
```

Run the engine, then connect to it:

```sh
./build/gargantuan
./build/bin/tracy-profiler
```

For an unattended run, `--profile <seconds>` stops the engine after that long,
which bounds what a capture alongside it collects:

```sh
./build/bin/tracy-capture -o run.tracy -s 10 &
./build/gargantuan --profile 12
```

`tracy-csvexport run.tracy` prints per-zone totals, which is how two runs get
compared without opening a window.

`-DGARGANTUAN_TRACY=OFF` compiles every zone out entirely.

## Adding Data Types

It's easy!

- Make a datatype class that extends Userdata
- Implement a StackValue (theres the G_UD_STACKVALUE macro to simplify this)
- Add a matching UserdataTag
- If you're writing a Lib for the data type, edit ScriptEngine.hpp to include a OpenLib function for it, then implement it under scripting
- Call your datatypes CreateUserdataMetatable inside ScriptEngine.hpp
- It's done

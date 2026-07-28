# TODO

- [ ] Credit these people in readme when u write a better one:
      Kinemium engine -> cframe impl ref
      Phoenix engine -> instance luau api ref, stackvalue inspo
      Flux engine -> uhh more luau api design inspo

## Classes

### By API Completeness

- [ ] MeshPart

## Data Types

### By Implementation

- [ ] InstanceHandle

### By API Completeness

- [ ] Instance
- [ ] InstanceHandle

## Platforms

- [x] Linux, x11 and Vulkan
- [ ] Windows: nothing POSIX-only is left in the engine and CMake copies the
      DLLs, but it has never been built or run. glslc comes from the Vulkan SDK
      there rather than a package.
- [ ] macOS: CMake already cross-compiles SPIR-V to metallib, untested, and it
      wants spirv-cross installed
- [ ] Android and iOS: SDL3 supports both, no build is set up
- [ ] Wayland: goes through SDL, untested

## VR

The camera half already exists. Two cameras with their own targets render a
stereo pair today; examples/StereoCameras.luau measures the parallax between the
eyes. What is missing is the runtime that drives them.

- [ ] Vendor OpenXR, create a session and its swapchains
- [ ] Drive the eye cameras from the runtime's head pose rather than a script
- [ ] Take each eye's projection from the runtime instead of FieldOfView,
      because headset frusta are asymmetric and differ per eye
- [ ] Submit the eye textures to the compositor instead of reading them back
- [ ] Lens distortion and chromatic aberration as a per-eye PostProcessShader
- [ ] Motion controllers and head tracking through UserInputService
- [ ] Draw both eyes in one pass, via multiview or instancing

## Cameras

- [ ] Split-screen, several cameras compositing into one window
- [ ] Sample a camera's target as a texture in-world, ie. ViewportFrame

## Images

- [ ] Decode images from disk, PNG and JPEG
- [ ] DrawImage, DrawCircle, DrawLine
- [ ] AssetService:CreateEditableImage

## Shaders

- [ ] Reflect more than the parameter block, so samplers and storage bindings
      get checked against what the shader declares
- [ ] Report a shader's expected parameters to scripts, so a misspelled name is
      an error rather than silently ignored
- [ ] Let surface shaders take images the way post-process shaders can
- [ ] Cache compiled runtime shaders on disk so they survive a restart

## Render

- [ ] Textures
- [ ] Lighting service
- [ ] PBR

## Scripting

- [ ] Access levels/Script securities/Script capabilities/proper sandboxing

## Services

- [_] ...

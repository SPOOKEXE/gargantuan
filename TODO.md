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

- [ ] Let a camera's target be put on a part's surface, not only sampled by a
      shader
- [ ] Break camera sampling cycles more usefully than dropping one edge, eg. by
      giving the loop an explicit previous-frame copy to read

## Images

- [ ] AssetService:CreateEditableImage
- [ ] Save an image back out to a file
- [ ] Antialias the drawing calls; they are hard-edged today
- [ ] The blend modes Roblox's Draw calls take, ie. Overwrite against BlendSource

## Shaders

- [ ] Vertex stage overrides, so a shader can move geometry and not only shade it
- [ ] Reflect vertex inputs, to reject a surface shader whose layout does not
      match what opaque.vert emits
- [ ] Bound the shader cache, it grows without limit today

## Render

- [ ] Textures
- [ ] Lighting service
- [ ] PBR

## Scripting

- [ ] Access levels/Script securities/Script capabilities/proper sandboxing

## Services

- [_] ...

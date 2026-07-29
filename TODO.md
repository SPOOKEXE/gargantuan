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

- [ ] Give the freecam a configurable invert-Y and separate sensitivity per axis

## Images

- [ ] DrawText, which needs a font first; examples/SecurityCamera.luau draws its
      clock as seven-segment rectangles to work around this

## Shaders

- [ ] Vertex stage overrides, so a shader can move geometry and not only shade it
- [ ] Reflect vertex inputs, to reject a surface shader whose layout does not
      match what opaque.vert emits

## Render

- [ ] Occlusion, so a part hidden behind a wall stops costing a redraw. The
      frustum test cannot see it; something depth-aware has to
- [x] Depth as a bindable render texture. Enum.RenderTexture.Depth and
      .DepthHistory, and taa.frag now rejects a history that was nearer than
      what stands there now, which is the disocclusion colour cannot see.
      Not the camera's own depth buffer in the end: the motion vector pass
      already produces the distance as the w a perspective projection divides
      by, so it writes it to a second attachment. That gives it linear and in
      studs, needing no clip planes to interpret, and as an ordinary picture,
      so the copy kept for the next frame is the same blit as everything else
- [ ] Textures
- [ ] Lighting service
- [ ] PBR

## Scripting

- [ ] Access levels/Script securities/Script capabilities/proper sandboxing

## Services

- [_] ...

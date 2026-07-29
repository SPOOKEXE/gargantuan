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
- [x] A real TAA to ship as the swapped-in antialias pass. assets/shaders/
      taa.frag, as Enum.PresetShaders.TemporalAntialias; examples/
      TemporalAntialiasing.luau does the swap and measures it. A pass reaches
      the camera it is running on through SetRenderTexture, which a shared
      pass could not do by naming one: Enum.RenderTexture.History is that
      camera's last frame, Enum.RenderTexture.Velocity its motion vectors,
      and reading builtin.Jitter puts its projection on a sub-pixel wander.
      Each is produced only for a camera whose passes ask, so an ordinary one
      pays for none of it
- [ ] Depth as a bindable render texture. TAA rejects stale history on colour
      alone, which is enough at contrast but thin where a surface slides out
      from behind another in much the same shade. The camera's depth buffer is
      D16_UNORM and not created sampleable, so both would have to change
- [ ] Textures
- [ ] Lighting service
- [ ] PBR

## Scripting

- [ ] Access levels/Script securities/Script capabilities/proper sandboxing

## Services

- [_] ...

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

- [ ] Give the freecam a configurable invert-Y and separate sensitivity per axis

- [ ] Let a part choose which face a SurfaceCamera lands on, and how it is
      tiled; it uses the mesh's own UVs on every face today
- [ ] Show an EditableImage on a part's surface, not only a camera

## Images

- [ ] AssetService:CreateEditableImage
- [ ] Save formats besides PNG, ie. JPEG and BMP
- [ ] DrawText, which needs a font first; examples/SecurityCamera.luau draws its
      clock as seven-segment rectangles to work around this

## Shaders

- [ ] Let the built-in antialias pass be swapped for a better one, eg. TAA using
      the previous-frame copies cameras already keep
- [ ] Vertex stage overrides, so a shader can move geometry and not only shade it
- [ ] Reflect vertex inputs, to reject a surface shader whose layout does not
      match what opaque.vert emits

## Render

- [ ] Occlusion, so a part hidden behind a wall stops costing a redraw. The
      frustum test cannot see it; something depth-aware has to
- [ ] Keep a fixed-size list of the parts worth looking at rather than walking
      all of them. Anything written to goes to the front and pushes the
      longest-still one off the back, so the sweep is over what moves rather
      than over the world. Order it by a LastUpdate stamp with a threshold
      before a swap, or a part sitting on the boundary thrashes in and out
      every frame and costs more than it saves
- [ ] Textures
- [ ] Lighting service
- [ ] PBR

## Scripting

- [ ] Access levels/Script securities/Script capabilities/proper sandboxing

## Services

- [_] ...

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
- [ ] Add mesh edge and mesh face inputs as well for shader scripts.
- [ ] Build a internal "shader capabilities" which holds what the shader accesses and uses. This way if no shaders use vertex information, we skip processing it and passing it in, and we can skip entirely for entire pipeline if no shader scripts use it.

## Render

- [ ] Occlusion, so a part hidden behind a wall stops costing a redraw. The
      frustum test cannot see it; something depth-aware has to
- [ ] Textures
- [ ] Emissive, so a surface can be a source of colour rather than only a
      receiver of it. The opaque shader multiplies a part's colour by the light
      falling on it, so nothing can be brighter than what reaches it and an
      unlit face is always its colour times the ambient fifth. Wants a term the
      lighting is added to rather than multiplied into, and a texture channel to
      drive it per pixel once there are textures at all.
      examples/ConcertStage.luau switches its lamps between their colour and
      black to fake a rig, which is as close as a multiply gets
- [ ] Lighting service
- [ ] Lights as instances -- point, spot and surface -- rather than the single
      directional sun the shader has now. The stage in ConcertStage.luau is lit
      by an afternoon sun swung round behind the audience because that is the
      only light there is; a concert wants a dozen of them, coloured, aimed and
      switchable. Needs somewhere to put them in the opaque pass's uniforms and
      a decision about how many one draw can carry
- [ ] PBR
- [ ] Ensure batched, parallel, vectorised and SIMD compute across the board (use stress test to find bottlenecks).
- [ ] For shapes that are "flat" on axis, add per-cardinal 3d direction (up/down/left/right/forward/backward) face grouping so we can skip rendering faces that are pointing away from camera and are out of view. Refer to video https://www.youtube.com/watch?v=40JzyaOYJeY . Mainly for "static" objects, so Anchored = true and cardinal axis aligned (use epsilon for threshold). Also watch Anchored property as a quick dirty "i am updated" state so we can scan and check whether to put it in the flat mesh system or keep it in the dynamic one.
- [ ] Consider using multiple larger contiguous blocks of memory for storing known primitives (cubes, cylinders, spheres) and either expanding or linked list of contiguous blocks. Helps memory location, simd over batched contiguous memory, etc.
- [ ] Shadow map is one fixed 60-stud box at the world origin, so nothing
      outside it casts at all. The walk rejects casters against that box now
      rather than handing the pass a whole world of them -- SkyGrid with
      CastShadow on went 311 -> 27.6 ms a frame -- but a large scene still only
      has shadows near the origin. Wants cascades, or a box fitted to the camera

## Scripting

- [ ] Access levels/Script securities/Script capabilities/proper sandboxing

## Services

- [_] ...

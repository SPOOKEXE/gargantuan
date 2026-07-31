# Gargantuan — ECS Layout Plan

A design note on how to lay out a component system behind a Roblox-style
instance API, written against Gargantuan as it stands on `spook-dev`
(commit `a16e551`).

The short version: the engine already contains about 60% of an ECS under
different names. The plan is to finish and generalise what is there, not to
rewrite the object model.

---

## Contents

1. [What already exists](#1--what-already-exists)
2. [Why this is easier than a general ECS](#2--why-this-is-easier-than-a-general-ecs)
3. [The four layers](#3--the-four-layers)
4. [Measured layout, today](#4--measured-layout-today)
5. [The component system](#5--the-component-system)
6. [Before / after, by class](#6--before--after-by-class)
7. [Migration order](#7--migration-order)
8. [What not to do](#8--what-not-to-do)

---

## 1 · What already exists

| ECS concept | What Gargantuan calls it |
|---|---|
| Entity | `Instance` (`include/gargantuan/datatypes/Instance.hpp:19`) |
| Type registry | `ClassRegistry` + `ClassDefinition`, with flattened property/method tables |
| Dense component array | `WorldRoot::Parts` / `RawParts` (`classes/WorldRoot.hpp:13`) |
| Entity to row handle | `BasePart::WorldIndex` |
| Change detection | `Instance::QuickHash` + `ChangeList` / `DirtyParts` |
| Hot system view | `RenderProvider::PartRow` (`render/RenderProvider.hpp:166`) |
| Spatial index over rows | `PartGrid` (`render/RenderProvider.hpp:200`) |

`WorldRoot.cpp:27-50` is a textbook sparse-set removal: swap with last, fix the
back-index. That is EnTT's storage, hand-rolled.

The render path is the fast path precisely because it already has all three
layers — `RawParts` (registry) into `PartRows` (system view) into `PartGrid`
(spatial index). Physics and input do not, which is why they are the ones to
build this way rather than retrofit.

---

## 2 · Why this is easier than a general ECS

In a general ECS, archetypes are dynamic: any entity can gain or lose any
component at runtime, so you need archetype graphs, chunk migration, and
deferred structural change. That is most of the complexity in flecs and Unity
DOTS.

A Roblox-style API does not have that problem. An instance's property set is
fixed by its `ClassName` at `Instance.new` and never changes for its lifetime.
So:

> **One concrete class = one archetype, computed statically at registry init.**

`Instance.new("Frame")` does not add `Instance` + `GuiObject` + `Frame`
components. `Frame` *is* the archetype, and `ClassRegistry::Flatten()`
(`src/ClassRegistry.cpp:75`) already walks the superclass chain once and caches
the union — the work happens at startup, not per instance.

You never need `add_component` / `remove_component` as a general facility.
Delete that axis from the design. The only genuinely dynamic components are a
handful of optional ones (see [§5](#5--the-component-system)), and each is a
single sparse set — an insert or erase, not an archetype migration.

### The two axes people conflate

The Discord thread that prompted this note mixed up two independent questions:

- **What the API exposes** — a class tree with `IsA`. This is surface. Roblox
  itself keeps it separate; `Part` deriving from `BasePart` is API, and the
  renderer does not care.
- **How bytes are laid out** — which fields get iterated together.

If components mirror the inheritance tree you get a `Part` component that no
system ever iterates: pure bookkeeping, zero cache benefit. Components are
chosen by *which system reads them together*.

---

## 3 · The four layers

### L0 · `Instance` — identity, tree, reflection

Unchanged. Keep `shared_ptr`, keep `Parent`, keep signals.

Do **not** reduce this to a bare `uint32` entity id. Luau holds a userdata that
must survive reparenting, `:Destroy()`, and `part.CFrame = x` writes. The entity
handle model buys nothing when the scripting layer already owns strong
references. `WorldIndex` is the entity handle already, scoped to the one system
that needs it, which is the right amount.

### L1 · Class = archetype

`ClassDefinition` gains a dense id and a materialised ancestor chain:

```cpp
struct ClassDefinition {
    // ...existing Name, Superclass, Properties, Methods, AllProperties...
    uint16_t ClassId = 0;
    uint8_t  Depth = 0;                    // Instance == 0
    uint16_t Ancestors[MAX_CLASS_DEPTH];   // Ancestors[Depth] == ClassId

    // Which component sets an instance of this class may appear in. Computed
    // once at registry init; Destroy and the serialiser walk exactly this list
    // and nothing else, instead of probing every set in the engine.
    std::vector<ComponentSetBase *> Components;
};
```

`Components` unions up the superclass chain in `Flatten()`, exactly as
properties and methods already do.

### L2 · Registries — the component storage

Generalise the `WorldRoot::Parts` pattern into one reusable thing:

```cpp
template <typename T> class InstanceRegistry {
public:
    void Attach(Instance *root);              // hooks DescendantAdded/Removed
    std::span<T *const> Raw() const;
    std::span<Instance *const> Dirty();
    void ClearDirty();
private:
    std::vector<std::shared_ptr<T>> Owned;    // keeps alive
    std::vector<T *> Rows;                    // what systems walk
    std::vector<Instance *> Changed;          // the change list
};
```

Membership is decided once, on parent, by the O(1) `IsA` from L1.

### L3 · System views — the hot rows

`PartRow` is the model. The discipline to write down:

- Script-readable → lives on the Instance.
- Read every frame by a system → lives in that system's row array.
- Both → lives in both, with the row a **cache** refreshed from the change
  list, never authoritative.

Rows are **per system, not shared**. When physics lands it gets its own
`BroadphaseRow`, not a field bolted onto `PartRow` — the cull loop should not
drag velocity through cache.

---

## 4 · Measured layout, today

Measured with a probe translation unit compiled against the real headers using
the project's own flags from `build/compile_commands.json`
(x86-64, libstdc++, `-std=gnu++20`):

```
sizeof(Instance)           216      sizeof(Signal<T>)   48
sizeof(BasePart)           496      sizeof(CFrame)      48
sizeof(Part)               496      sizeof(Color3)      12
sizeof(Camera)             528      sizeof(std::string) 32
sizeof(InputObject)        248      sizeof(glm::mat4)   64
sizeof(UserInputService)   816

offsetof(Instance, Children)             80
offsetof(Instance, ChildAdded)          112
offsetof(BasePart, CollisionGroup)      304
offsetof(BasePart, PreviousModelMatrix) 424
```

Two things fall out immediately:

- **96 of `Instance`'s 216 bytes are signal pointers** (offsets 112 to 208), all
  eagerly `make_shared`'d. Constructing any instance performs **7 heap
  allocations**: the object plus six signals.
- **`BasePart`'s cold fields occupy a contiguous 192-byte tail** from offset 304
  to the end — `CollisionGroup`, `CustomPhysicalProperties`, the surface block,
  and `PreviousModelMatrix`. Roughly 174 bytes of real content, almost never set
  on an ordinary part.

Per part, today: **496 bytes of object plus about 384 bytes of signal
allocations, at 7 allocations each.** For 10 000 parts that is ~8.8 MB and
70 000 allocations.

---

## 5 · The component system

### The cutting rule

> **A component is a group of fields that one loop reads together on the same
> pass. Not a group of fields that conceptually belong to the same thing.**

`BasePart` today is 27 fields that belong to a *concept*; no single loop reads
more than about a third of them. Cut along loop boundaries and the class tree
becomes irrelevant to storage.

Two axes decide where each cut lands:

- **Density** — do ~all entities in the family have it? Dense means an array
  indexed by `WorldIndex`. Sparse means a `SparseSet`.
- **Sharing** — per-entity, or shared by thousands? Per-entity means a
  component. Shared means a *resource*, and the component holds a handle.

### `BasePart` — 27 fields into 8 components

| Component | Fields | Bytes | Storage | Read by |
|---|---|---|---|---|
| `Transform` | `CFrame`, `Size` | 60 | dense | render fill, shadow, physics, bounds |
| `Bounds` | `Centre`, `Radius` | 16 | dense, **derived** | cull, grid |
| `Visual` | `Color`, `Reflectance`, `Transparency`, `Material`, `MeshId`, `CastShadow` | 28 | dense | render fill, shadow |
| `Collider` | `CanCollide`, `CanQuery`, `CanTouch`, `CollisionGroupId` | 6 | dense | broadphase, raycast |
| `RigidBody` | `Velocity`, `AngularVelocity`, `InvMass`, `InvInertia`, `BodyId` | ~64 | **sparse** | physics step |
| `Surface` | `SurfaceCamera`, `SurfaceImage`, `SurfaceFace`, `SurfaceTiling`, `SurfaceOffset`, `SurfaceTextureSlot` | 60 | **sparse** | surface pass |
| `MassOverride` | `CustomPhysicalProperties` | 24 | **sparse** | mass calc |
| `Motion` | `PreviousModelMatrix` | 64 | **sparse** | velocity pass |

Two entries deserve comment.

**`Bounds` is derived, not stored on the part.** Computed from `Transform` when
the change list says the part moved, and it is the only thing the cull loop
touches. This already exists as `PartRow::Centre` / `Radius`; the component
framing just names it and makes it the pattern rather than a one-off.

**`Locked` and `Massless` are absent.** `Locked` is studio-only metadata and
belongs in a sparse `EditorFlags` set alongside `Archivable`. `Massless` folds
into `RigidBody` as `InvMass = 0`.

### `Anchored` becomes a component's absence

This is the single highest-value cut in the engine, and the one that is *only*
available if you go component-based.

Before — `Anchored` is a bool, and the physics step walks every part to discard
almost all of them:

```cpp
bool Anchored = false;   // on every part

void PhysicsStep(float dt) {
    for (BasePart *p : world.RawParts) {
        if (p->Anchored) continue;                  // true for ~95% of parts
        p->CFrame.Position += p->Velocity * dt;     // 496 B object touched to find out
    }
}
```

After — `Anchored` is not stored at all. It *is* the absence of `RigidBody`:

```cpp
// The Luau property is a view over set membership
{"Anchored", {
    [](lua_State *L, Instance *i) -> int {
        StackValue<bool>::Push(L, !Bodies.Has(i->Cast<BasePart>()));
        return 1;
    },
    [](lua_State *L, Instance *i) -> int {
        auto *part = i->Cast<BasePart>();
        if (StackValue<bool>::From(L, -1)) Bodies.Remove(part);
        else                               Bodies.Add(part, RigidBody::For(*part));
        part->MarkChanged();
        return 0;
    },
    G_UD_REFLECT_TYPE(bool),
}},

void PhysicsStep(float dt) {
    for (auto &[index, body] : Bodies) {            // exactly the movers, contiguous
        Transforms[index].Position += body.Velocity * dt;
    }
}
```

`part.Anchored = false` now constructs the component. The script API is
unchanged. On a 10 000-part place with 200 movers, the physics step's working
set drops from roughly 5 MB to roughly 13 KB.

This is also the answer to "does the interface just build components as
needed?" — yes, and this is what it looks like. It is the one legitimate
structural change in the design, and because only one sparse set is involved it
costs an insert or erase rather than an archetype migration.

### Meshes are a handle, not a component

Mesh data is **shared**. Ten thousand blocks reference one 24-vertex cube.
`MeshProvider` (`render/MeshProvider.hpp`) already interns them and hands out
stable `GpuMesh *` slots; `Part::SetShape` already reduces a shape to a one-byte
`MeshId`.

```cpp
// WRONG — 10 000 parts, 10 000 GpuMeshes
struct MeshComponent { GpuMesh Mesh; };

// RIGHT — the resource is interned; the component is a handle
uint8_t  MeshId;       // primitives: Block, Ball, Cylinder, Wedge, CornerWedge
uint32_t MeshHandle;   // MeshPart later: index into MeshProvider
```

The general rule:

| Kind | Example | Where it lives |
|---|---|---|
| Per-entity data | `Transform`, `Visual` | component array |
| Shared asset | `GpuMesh`, textures, shaders | resource table, interned |
| Reference to shared | `MeshId`, `SurfaceTextureSlot` | component, as a small integer |

`MeshPart` therefore adds no heavyweight component — just `MeshHandle` plus a
sparse `MeshOverride` for `CollisionFidelity` / `RenderFidelity`. Sorting by
`MeshId` also groups parts into draw calls, which is what `PartInstances` and
`InstanceData` already feed.

### Services are resources, not entities

`sizeof(UserInputService)` is **816 bytes**, with 23 `G_SIGNAL`s inline
(`services/UserInputService.hpp:90-120`) — 368 bytes of pointers plus 23 heap
allocations at startup. That sounds bad until you notice there is exactly one of
them, forever, and nothing iterates services.

> Componentise what you **iterate**. Singletons are resources; leave them alone.

`InputObject` is a different story and is a genuine entity family — see
[§6.6](#66--inputobject--per-keypress-allocation).

---

## 6 · Before / after, by class

### 6.1 · `Instance` — eager signals

Before (`datatypes/Instance.hpp:105-113`):

```cpp
G_SIGNAL(ChildAdded, Instance::Pointer);          // = Signal<T>::Pointer x =
G_SIGNAL(ChildRemoved, Instance::Pointer);        //     std::make_shared<Signal<T>>()
G_SIGNAL(DescendantAdded, Instance::Pointer);
G_SIGNAL(DescendantRemoved, Instance::Pointer);
G_SIGNAL(AncestryChanged, AncestryChangedArguments);
G_SIGNAL(Destroying, std::monostate);
```

Six `shared_ptr`s, 96 of 216 bytes, six allocations in the constructor, for
signals that on a typical part nobody ever connects to.

After — one pointer to a lazily allocated block:

```cpp
struct SignalBlock {   // allocated on first Connect; never, for most instances
    Signal<Instance::Pointer>::Pointer ChildAdded, ChildRemoved;
    Signal<Instance::Pointer>::Pointer DescendantAdded, DescendantRemoved;
    Signal<AncestryChangedArguments>::Pointer AncestryChanged;
    Signal<std::monostate>::Pointer Destroying;
};
std::unique_ptr<SignalBlock> Signals;   // 8 bytes, usually null

Signal<Instance::Pointer>::Pointer &GetChildAdded() {
    if (!Signals) Signals = std::make_unique<SignalBlock>();
    if (!Signals->ChildAdded)
        Signals->ChildAdded = std::make_shared<Signal<Instance::Pointer>>();
    return Signals->ChildAdded;
}
```

Fire sites become `if (Signals && Signals->ChildAdded) Signals->ChildAdded->Fire(child);`.

The Luau side is unaffected: swap `G_UD_READONLY_PROP(Instance, ChildAdded, …)`
for a getter calling `GetChildAdded()`, and `part.ChildAdded:Connect(f)` still
works — it just materialises the signal at that moment.

**216 to 128 bytes; 7 allocations to 1.**

Worth noting as a general lesson: `G_SIGNAL` is a macro that hides a
`make_shared` inside a member initialiser, so the cost is invisible at the
declaration site. Any macro that expands to an allocation deserves this audit.

### 6.2 · `BasePart` — the cold tail

Before — everything on the object whether or not it is used:

```cpp
class BasePart : public Instance {
    bool Anchored, CanCollide, CanQuery, CanTouch, CastShadow, Locked, Massless;
    CFrame CFrame;  Color3 Color;  glm::vec3 Size;
    Enums::Material Material;  float Reflectance, Transparency;

    std::string CollisionGroup = "Default";                     // 32 B  @304
    std::optional<PhysicalProperties> CustomPhysicalProperties; // 24 B
    std::shared_ptr<Camera> SurfaceCamera;                      // 16 B
    std::shared_ptr<EditableImage> SurfaceImage;                // 16 B
    Enums::NormalId SurfaceFace;                                //  4 B
    Vector2 SurfaceTiling, SurfaceOffset;                       // 16 B
    uint8_t SurfaceTextureSlot;                                 //  1 B
    glm::mat4 PreviousModelMatrix; bool HasPreviousModelMatrix; // 65 B  @424
};                                                              // = 496
```

After — slim core plus side tables:

```cpp
class BasePart : public Instance {
    // Hot: read by the renderer and physics every frame
    CFrame CFrame;            // 48
    glm::vec3 Size;           // 12
    Color3 Color;             // 12
    float Reflectance, Transparency;
    Enums::Material Material;
    bool CanCollide, CanQuery, CanTouch, CastShadow;
    uint8_t MeshId;
    uint32_t WorldIndex;
};   // ~140 B core, ~240 B total with lazy signals from 6.1
```

```cpp
struct SurfaceComponent {
    std::shared_ptr<Camera> Camera;
    std::shared_ptr<EditableImage> Image;
    Enums::NormalId Face = Enums::NormalId::Front;
    Vector2 Tiling{1, 1}, Offset{0, 0};
    uint8_t TextureSlot = 0;
};
SparseSet<SurfaceComponent> Surfaces;
SparseSet<PhysicalProperties> CustomPhysProps;
SparseSet<uint16_t> CollisionGroups;   // "Default" is the absent case
```

`PreviousModelMatrix` does not go to a side table at all — it goes into
`PartRow`. Its own comment already says *"Bookkeeping, not a property… Only kept
while something asks for motion vectors"*, yet it is stored unconditionally on
every part.

The Luau surface is untouched; only the accessor moves:

```cpp
// before
[](lua_State *L, Instance *i) -> int {
    StackValue<Instance::Pointer>::Push(L, i->Cast<BasePart>()->SurfaceCamera);
    return 1;
},
// after
[](lua_State *L, Instance *i) -> int {
    auto *s = Surfaces.Find(i->Cast<BasePart>());
    StackValue<Instance::Pointer>::Push(L, s ? s->Camera : nullptr);
    return 1;
},
```

The machinery that exists purely to dodge the cold data —
`WorldHasSurfaceCameras`, `WorldHasSurfaces`, `SurfaceRowsStale`,
`RebuildSurfaceRows()` (`render/RenderProvider.hpp:151-193`) — collapses into
"iterate `Surfaces`", because the set *is* the list of parts that have one.

Combined with 6.1, per part:

| | bytes/part | allocs/part | 10 000 parts |
|---|---|---|---|
| Before | 496 + ~384 signals | 7 | ~8.8 MB, 70 000 allocs |
| After | ~240 | 1, from an arena | ~2.4 MB, ~40 chunks |

### 6.3 · `IsA` — string walk to two comparisons

Before (`datatypes/Instance.cpp:314`) — a hash lookup *and* a `string_view`
compare per level:

```cpp
bool Instance::IsA(std::string_view className) {
    auto def = ClassRegistry::GetDefinition(this);
    while (def) {
        if (def->Name == className) return true;
        if (!def->Superclass) return false;
        def = ClassRegistry::GetDefinitionByName(*def->Superclass);   // map probe
    }
    return false;
}
```

After:

```cpp
inline bool Instance::IsA(const ClassDefinition &target) const {
    const auto *def = CachedDefinition;
    return def->Depth >= target.Depth && def->Ancestors[target.Depth] == target.ClassId;
}
```

Hot call sites gain most. `WorldRoot`'s child hook currently pays the full
string walk on every parent operation:

```cpp
if (instance->IsA("BasePart")) { ... }          // before
if (instance->IsA(ClassIds::BasePart)) { ... }  // after, two integer compares
```

Same for the 77 `dynamic_cast` / `IsA` sites across the engine; `Cast<T>()`
becomes a checked `static_cast` behind the same test. The string overload stays
for Luau's `:IsA("Part")`, resolving the name once via `GetDefinitionByName`.

### 6.4 · `WorldRoot` — hand-rolled storage to reusable registry

Before (`classes/WorldRoot.cpp:13-51`) — 38 lines of sparse-set logic inside one
class, wired to `ChildAdded`:

```cpp
WorldRoot::WorldRoot() {
    ChildAdded->Connect([this](Instance::Pointer instance) {
        if (instance->IsA("BasePart")) {
            auto part = std::static_pointer_cast<BasePart>(instance);
            part->WorldIndex = (uint32_t)this->Parts.size();
            this->Parts.push_back(part);
            this->RawParts.push_back(part.get());
            part->ChangeList = &this->DirtyParts;
            part->MarkChanged();
        }
    });
    ChildRemoved->Connect([this](Instance::Pointer instance) {
        /* ...swap with last, fix back-index, 20 more lines... */
    });
}
```

After:

```cpp
WorldRoot::WorldRoot() { Parts.Attach(this); }   // InstanceRegistry<BasePart>
```

> **Bug to fix while extracting this.** The current code hooks `ChildAdded`, not
> `DescendantAdded`. A `Part` inside a `Model` inside `Workspace` will never
> enter `Parts` and will never render. `Model` does not exist yet, which is why
> this has not surfaced — but build descendant tracking into the registry now.
> `SetParent` already collects the whole subtree (`Instance.cpp:127-128`), so
> the signals fire correctly for reparented groups.

### 6.5 · Physics — greenfield, so build it right

`box3d` is vendored and linked (`CMakeLists.txt:131`) but nothing uses it yet.
The physics layout is a free choice.

The trap is mirroring the render side's original mistake and hanging state off
`BasePart`:

```cpp
// DON'T — grows BasePart back past 496 bytes and makes render walks drag
// physics state through cache
class BasePart : public Instance {
    glm::vec3 Velocity, AngularVelocity;
    b3BodyId Body;
    float InvMass;
};
```

Instead, physics gets its own registry and rows, mirroring `RenderProvider`:

```cpp
struct RigidBody {          // sparse — movers only
    glm::vec3 Velocity{0}, AngularVelocity{0};
    float InvMass = 1.0f;
    glm::vec3 InvInertia{1};
    b3BodyId Body;
    uint32_t PartIndex;     // back into Transforms[]
};

struct BroadphaseRow {      // dense — every collidable part
    glm::vec3 Min, Max;     // AABB, refreshed off the change list
    uint16_t CollisionGroupId;
    uint8_t  Flags;         // CanCollide | CanQuery | CanTouch
};
```

Two things fall out:

- `BroadphaseRow` is 32 bytes, so one cache line holds two parts' AABBs and the
  sweep prefetches cleanly. Reading the same data off `BasePart` means a
  496-byte stride and a hard miss per part.
- `CollisionGroup` stops being a `std::string` on every part — 32 bytes plus a
  heap allocation for anything past SSO — and becomes a `uint16_t` id resolved
  through a name table on the service. The string exists only where a script
  wrote it.

Physics also gets its **own change list** rather than sharing `DirtyParts`: a
part that only changed `Color` should not wake the broadphase, and a part that
only moved should not rebuild its surface row.

### 6.6 · `InputObject` — per-keypress allocation

Before (`services/UserInputService.hpp:30-31`):

```cpp
std::unordered_map<Enums::KeyCode, std::shared_ptr<InputObject>> ActiveKeys;
std::unordered_map<Enums::UserInputType, std::shared_ptr<InputObject>> ActiveMouseButtons;
```

`sizeof(InputObject)` is **248 bytes** plus six inherited signals. Pressing a key
does a hash insert, one `make_shared` for the object, and six more for signals:
**7 allocations and ~630 bytes per keypress**, all freed on release. During WASD
movement with modifiers that is steady churn on the input path.

After — dense state, entity materialised on demand:

```cpp
struct InputState {                     // 12 bytes
    glm::vec3 Position{0};              // axis value / mouse pos / wheel delta
    Enums::UserInputState State;
};
std::array<InputState, KEYCODE_COUNT> Keys{};   // ~3.6 KB, fixed, zero allocation

ObjectPool<InputObject> InputObjects;   // recycled, not rebuilt
```

`IsKeyDown(code)` becomes an array index rather than a hash probe. An
`InputObject` is pulled from the pool only when a signal actually carries one
into Luau, and returned on release. Held keys stop allocating entirely.

### 6.7 · Adding the next class

This is the real argument, because the near-term roadmap is largely "more
instance classes".

Before — every system that needs "all X" reimplements 6.4. Adding `Lighting` and
`PointLight` means a second copy of the vector pair, the swap-remove, the
back-index field, and the change list, plus a second set of bugs in it.

After — the class declares its data and joins a registry:

```cpp
class PointLight : public Instance {
public:
    static const ClassDefinition DEFINITION;   // unchanged in shape
    Color3 Color{1, 1, 1};
    float Brightness = 1.0f, Range = 8.0f;
    bool Shadows = false;
    uint32_t WorldIndex = 0;
};

class Lighting : public Instance {
    InstanceRegistry<PointLight> Lights;
    Lighting() { Lights.Attach(GetWorld()); }
};

struct LightRow { glm::vec3 Position; float Range; glm::vec3 Colour; uint32_t Flags; };
```

The renderer then does what it already does for parts — walk `Lights.Dirty()` to
refresh rows, walk `LightRows` to cull and fill — with no new storage machinery.

### 6.8 · Summary of layout changes

| Subsystem | Before | After |
|---|---|---|
| `Instance` | 216 B, 6 eager signals, 7 allocs | 128 B, lazy signals, 1 alloc |
| `BasePart` | 496 B, all fields always | ~140 B core + 5 component sets |
| Anchored parts | `bool` + branch over all parts | absent from the `RigidBody` set |
| Mesh | (proposed) component per part | interned resource + `uint8`/`uint32` handle |
| `CollisionGroup` | `std::string`, 32 B per part | `uint16_t` id + name table |
| `IsA` | string compare + map probe per level | two integer compares |
| `WorldRoot` | 38 lines of hand-rolled sparse set | `InstanceRegistry<BasePart>`, one line |
| Physics | not built | own registry, 32 B `BroadphaseRow`, own change list |
| `UserInputService` | 816 B singleton | unchanged — leave it |
| `InputObject` | 248 B + 6 signals, 7 allocs per keypress | 12 B dense state + pooled instances |

Note what does **not** change anywhere above: `ClassDefinition`'s shape, the
`G_UD_READWRITE_PROP` tables, `Flatten()`, the `.d.luau` generator, or a single
line of Luau. Every one of these is a storage change behind a stable reflection
layer. That is the practical test for whether an ECS refactor is worth doing —
if it forces the scripting API to change, you are rebuilding the object model
rather than the memory layout, and the memory layout was the goal.

---

## 7 · Migration order

1. **`ClassId` + ancestor array**, giving O(1) `IsA`. No layout risk, no API
   change, kills 77 `dynamic_cast` / string-walk sites. Independent of
   everything else, so do it first.
2. **Lazy signals** (6.1). Biggest win per line of code; 7 allocations per
   instance down to 1.
3. **Extract `InstanceRegistry<T>`** from `WorldRoot`, fixing descendant
   tracking. Prove it with a second user — `Lighting` or `Tween` — before
   trusting the abstraction.
4. **Cold fields out of `BasePart`** into sparse sets (6.2). Shrinks the part
   and makes step 5 pay more.
5. **Per-class arena allocation** via `std::allocate_shared` with an arena on
   `ClassDefinition`. `Instance.new("Part")` bump-allocates from a chunk and the
   `RawParts` walk starts hitting shared pages.
6. **Build physics component-first** (6.5) rather than retrofitting it.
7. **Only then** consider SoA-splitting `PartRow` — for example `Centre` and
   `Radius` as a bare `vec4` array for the cull loop. Measure first; the grid
   may already have eaten this win.

---

## 8 · What not to do

- **Do not build a generic archetype ECS.** Dynamic component add/remove solves
  a problem the Roblox object model does not have. You would pay for migration
  machinery that never runs.
- **Do not make components mirror the class tree.** `Part` is a `ClassId`, not a
  component.
- **Do not reduce `Instance` to an integer handle.** The scripting layer,
  signals, and `:Destroy()` semantics all need a stable object.
- **Do not componentise services.** One instance, no iteration, no benefit.
  `UserInputService` is fine as it is.
- **Do not put shared assets in components.** Meshes, textures, and shaders are
  interned resources; components hold handles.
- **Do not share a change list between systems.** A colour write should not wake
  the broadphase.

---

## Appendix · Reproducing the measurements

The sizes in [§4](#4--measured-layout-today) come from a probe translation unit
that provokes the compiler into printing them, since linking a standalone binary
against the engine's static initialisers is more trouble than it is worth:

```cpp
#include "gargantuan/classes/Part.hpp"
#include <cstddef>
using namespace gargantuan;
template <size_t N> struct SHOW;          // declared, never defined
SHOW<sizeof(Instance)> a1;                // error: incomplete type 'SHOW<216>'
SHOW<sizeof(BasePart)> a2;                // error: incomplete type 'SHOW<496>'
SHOW<offsetof(BasePart, CollisionGroup)> c1;
```

Compile with `-fsyntax-only -Wno-invalid-offsetof` using the flags for
`src/classes/BasePart.cpp` out of `build/compile_commands.json`, and read the
numbers out of the error messages. Re-run after each migration step to confirm
the layout moved the way the plan says it should.

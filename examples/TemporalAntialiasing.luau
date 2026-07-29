-- Swaps the engine's edge-blur antialiasing for a real temporal one, then
-- measures how it improves as it is given more frames to work with.
--
-- RenderSettings.AntialiasShader is the socket. A pass put in it runs on every
-- camera with Antialiasing on, and what it can ask the engine for is what makes
-- a temporal pass possible:
--
--   Enum.RenderTexture.History       this camera's finished picture, last frame
--   Enum.RenderTexture.Velocity      where each pixel was on that frame
--   Enum.RenderTexture.Depth         how far away each one is now, in studs
--   Enum.RenderTexture.DepthHistory  how far away whatever was there was then
--   builtin.Jitter                   read by the shader, which puts the
--                                    camera's projection on a sub-pixel wander
--
-- Without velocity it can only judge history by colour, which smears behind
-- anything moving. Without the depths it is blind to a surface emerging from
-- behind another of the same shade. Without the jitter it is merely a blur.

local RenderSettings = game:GetService("RenderSettings")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local VIEWPORT = Vector2.new(256, 256)
-- Far enough back to see the whole wall and the block crossing it
local VIEW = CFrame.lookAt(Vector3.new(0, 12, 42), Vector3.new(0, 11, 0))

local baseplate = Instance.new("Part")
baseplate.Name = "Baseplate"
baseplate.Anchored = true
baseplate.Color = Color3.fromHSV(0, 0, 0.35)
baseplate.Size = Vector3.new(512, 20, 512)
baseplate.Position = Vector3.new(0, -10, 0)
baseplate.Parent = Workspace

-- Turned a few degrees off square, so its edges cross the pixel grid at a
-- shallow angle. That is where aliasing is worst and where antialiasing has
-- something to prove. Raised until its low corner clears the ground, so the
-- edge being measured is the wall's own and not where it enters the floor.
local wall = Instance.new("Part")
wall.Name = "Wall"
wall.Anchored = true
wall.Color = Color3.fromRGB(240, 240, 240)
wall.Size = Vector3.new(30, 20, 1)
wall.Orientation = Vector3.new(0, 0, 8)
wall.Position = Vector3.new(0, 13, 0)
wall.Parent = Workspace

-- Something that actually moves, which is the case colour rejection alone
-- cannot handle: it ghosts unless the pass knows where the pixel came from
local mover = Instance.new("Part")
mover.Name = "Mover"
mover.Anchored = true
mover.Color = Color3.fromRGB(255, 60, 60)
mover.Size = Vector3.new(4, 4, 4)
mover.Position = Vector3.new(-12, 9, 6)
mover.Parent = Workspace

-- The window looks down -Z from an unrotated camera, so putting it back along
-- +Z frames the wall without touching Pitch or Yaw -- which the freecam rebuilds
-- its rotation from the moment the mouse moves, and which would snap the view
-- away from anything set here as a CFrame alone.
Workspace.CurrentCamera.CFrame = CFrame.new(Vector3.new(0, 12, 46))

-- Rendered at four times the size with no antialiasing, averaged back down.
--
-- Counting part-way pixels sounds like it measures antialiasing and does not: a
-- half-resolved edge scores higher than a correct one, whose neighbours are
-- close together. Distance from the truth has no such blind spot.
local SUPERSAMPLE = 4

local function groundTruth()
	local probe = Instance.new("Camera")
	probe.Name = "GroundTruth"
	probe.CameraType = Enum.CameraType.Scriptable
	probe.FPS = -1
	-- Raw samples, so nothing has softened them before the averaging does
	probe.Antialiasing = false
	probe.ViewportSize = VIEWPORT * SUPERSAMPLE
	probe.CFrame = VIEW
	probe.Parent = Workspace

	local large = probe:Render()
	probe:Destroy()

	local pixels = large:ReadPixelsBuffer(Vector2.zero, large.Size)
	local wide = math.floor(large.Size.X)
	local width = math.floor(VIEWPORT.X)
	local height = math.floor(VIEWPORT.Y)

	local truth = table.create(width * height)
	for y = 0, height - 1 do
		for x = 0, width - 1 do
			local total = 0
			for sy = 0, SUPERSAMPLE - 1 do
				local row = (y * SUPERSAMPLE + sy) * wide
				for sx = 0, SUPERSAMPLE - 1 do
					total += buffer.readu8(pixels, (row + x * SUPERSAMPLE + sx) * 4)
				end
			end
			truth[y * width + x + 1] = total / (SUPERSAMPLE * SUPERSAMPLE)
		end
	end

	return truth
end

local function errorAgainst(image, truth)
	local pixels = image:ReadPixelsBuffer(Vector2.zero, image.Size)
	local width = math.floor(image.Size.X)
	local height = math.floor(image.Size.Y)

	local total = 0
	for i = 0, width * height - 1 do
		total += math.abs(buffer.readu8(pixels, i * 4) - truth[i + 1])
	end

	return total / (width * height)
end

-- A camera each, because the accumulation lives on the target and "one frame"
-- has to start from none. On demand so nothing but the Render() calls below
-- draws it, or the answer would depend on how many frames the engine slipped in.
local function resolve(frames)
	local probe = Instance.new("Camera")
	probe.Name = "Probe"
	probe.CameraType = Enum.CameraType.Scriptable
	probe.FPS = -1
	probe.ViewportSize = VIEWPORT
	probe.CFrame = VIEW
	probe.Parent = Workspace

	local capture
	for _ = 1, frames do
		capture = probe:Render()
	end

	probe:Destroy()
	return capture
end

local truth = groundTruth()
print(`ground truth: {VIEWPORT.X * SUPERSAMPLE}x{VIEWPORT.Y * SUPERSAMPLE} averaged down to {VIEWPORT.X}x{VIEWPORT.Y}`)

-- The built-in pass first, for something to compare against. It works from one
-- frame, so more of them change nothing.
RenderSettings.AntialiasShader = nil
local builtin = resolve(1)
local builtinError = errorAgainst(builtin, truth)
builtin:Save("captures/taa_builtin.png")
print(`built-in antialias: {string.format("%.3f", builtinError)} mean error from the truth`)

-- The swap. Every binding is needed and the order matters: samplers are bound
-- in the order they were first set, after the camera's own output at slot 0,
-- and taa.frag declares them from binding 1 in exactly this order.
local taa = Instance.new("PostProcessShader")
taa.Name = "TemporalAntialias"
taa.Preset = Enum.PresetShaders.TemporalAntialias

local bindings = taa.Properties
bindings:SetRenderTexture(Enum.ShaderProperty.HistoryTexture, Enum.RenderTexture.History)
bindings:SetRenderTexture(Enum.ShaderProperty.VelocityTexture, Enum.RenderTexture.Velocity)
bindings:SetRenderTexture(Enum.ShaderProperty.DepthTexture, Enum.RenderTexture.Depth)
bindings:SetRenderTexture(Enum.ShaderProperty.DepthHistoryTexture, Enum.RenderTexture.DepthHistory)
-- All optional; these are the defaults the shader falls back to
bindings:SetNumber(Enum.ShaderProperty.Feedback, 0.92)
bindings:SetNumber(Enum.ShaderProperty.Clamping, 1.25)
bindings:SetNumber(Enum.ShaderProperty.Disocclusion, 0.02)

RenderSettings.AntialiasShader = taa
print(`swapped in {taa.Name}, which reads {#taa.Properties:ListImages()} engine textures`)

-- The curve is not a straight climb. Frame one has no history and ties the
-- built-in; frame two is the worst of all, being a sub-pixel-shifted copy of
-- something that was not antialiased either. It takes several passes through
-- the eight jitter positions for those offsets to average out.
--
-- That is the bargain: behind for a fraction of a second, ahead after.
for _, frames in { 1, 2, 8, 32, 128 } do
	local capture = resolve(frames)
	local error = errorAgainst(capture, truth)
	local against = string.format("%.2fx", error / math.max(builtinError, 0.0001))
	capture:Save(`captures/taa_{frames}.png`)
	print(`temporal antialias, {frames} frame(s): {string.format("%.3f", error)} mean error, {against} the built-in`)
end

-- Now the part that needs motion vectors. Walk the red block across the wall a
-- pixel or so a frame and capture it mid-stride: with reprojection it stays a
-- block, and without it the history smears a trail behind it.
local walker = Instance.new("Camera")
walker.Name = "Walker"
walker.CameraType = Enum.CameraType.Scriptable
walker.FPS = -1
walker.ViewportSize = VIEWPORT
walker.CFrame = VIEW
walker.Parent = Workspace

local moving
for step = 1, 48 do
	mover.Position = Vector3.new(-12 + step * 0.5, 9, 6)
	moving = walker:Render()
end
walker:Destroy()

moving:Save("captures/taa_moving.png")
print("saved captures/taa_moving.png -- the red block should have no trail behind it")

-- Putting it back is the same one line. The engine's own pass returns, and the
-- cameras stop jittering and stop keeping a history, because nothing is asking
-- for either any more.
RenderSettings.AntialiasShader = nil
-- Back where the truth was rendered from, or the comparison would be against a
-- picture with the block somewhere else in it
mover.Position = Vector3.new(-12, 9, 6)
print(`restored the built-in pass: {string.format("%.3f", errorAgainst(resolve(64), truth))} mean error`)

RunService.PreRender:Connect(function()
	mover.Position = Vector3.new(math.sin(Workspace.DistributedGameTime) * 12, 9, 6)
end)

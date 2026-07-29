-- Swaps the engine's edge-blur antialiasing for a real temporal one, then
-- measures the difference on the thing that shows it worst: a long shallow
-- diagonal, where a hard edge has to be described by whole pixels.
--
-- RenderSettings.AntialiasShader is the socket. A pass put in it runs on every
-- camera with Antialiasing on, and what it can ask the engine for is what makes
-- a temporal pass possible:
--
--   Enum.RenderTexture.History   this camera's own finished picture, last frame
--   Enum.RenderTexture.Velocity  where each pixel was on that frame
--   builtin.Jitter               read by the shader, which puts the camera's
--                                projection on a sub-pixel wander
--
-- Without the last two a pass can only decide whether to trust its history by
-- comparing colours, which drags a smear behind anything that moves; without
-- the jitter it has nothing new to average and is merely a blur.

local RenderSettings = game:GetService("RenderSettings")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local VIEWPORT = Vector2.new(256, 256)
-- Long enough for the accumulation to settle. Each frame adds one more
-- sub-pixel sample, and the sequence repeats every eight.
local SETTLE_FRAMES = 24

local baseplate = Instance.new("Part")
baseplate.Name = "Baseplate"
baseplate.Anchored = true
baseplate.Color = Color3.fromHSV(0, 0, 0.35)
baseplate.Size = Vector3.new(512, 20, 512)
baseplate.Position = Vector3.new(0, -10, 0)
baseplate.Parent = Workspace

-- Turned a few degrees off square, so its edges cross the pixel grid at a
-- shallow angle. That is where aliasing is worst and where antialiasing has
-- something to prove.
local wall = Instance.new("Part")
wall.Name = "Wall"
wall.Anchored = true
wall.Color = Color3.fromRGB(240, 240, 240)
wall.Size = Vector3.new(30, 30, 1)
wall.Position = Vector3.new(0, 8, 0)
wall.Orientation = Vector3.new(0, 0, 8)
wall.Parent = Workspace

-- Something that actually moves, which is the case colour rejection alone
-- cannot handle: it ghosts unless the pass knows where the pixel came from
local mover = Instance.new("Part")
mover.Name = "Mover"
mover.Anchored = true
mover.Color = Color3.fromRGB(255, 60, 60)
mover.Size = Vector3.new(4, 4, 4)
mover.Position = Vector3.new(-12, 8, 6)
mover.Parent = Workspace

local camera = Instance.new("Camera")
camera.Name = "Subject"
camera.CameraType = Enum.CameraType.Scriptable
camera.ViewportSize = VIEWPORT
camera.CFrame = CFrame.lookAt(Vector3.new(0, 10, 34), Vector3.new(0, 8, 0))
camera.Parent = Workspace

-- Counts pixels sitting partway between the two sides of an edge.
--
-- An aliased edge is every pixel wholly one side or the other, because one
-- sample per pixel can only answer which side its centre fell on. A pixel the
-- edge cuts in half should be half each, and the only way to arrive at that is
-- to sample it in more than one place -- which across frames is exactly what
-- the jitter is for. So counting the in-between pixels counts how much of the
-- edge's real shape the picture managed to keep.
local function countPartialCoverage(image)
	local pixels = image:ReadPixelsBuffer(Vector2.zero, image.Size)
	local width = math.floor(image.Size.X)
	local height = math.floor(image.Size.Y)

	local function red(x, y)
		return buffer.readu8(pixels, (y * width + x) * 4)
	end

	local partial = 0
	for y = 1, height - 2 do
		for x = 1, width - 2 do
			local left, here, right = red(x - 1, y), red(x, y), red(x + 1, y)
			local low, high = math.min(left, right), math.max(left, right)
			-- A real edge either side of it, and this pixel somewhere between
			-- the two rather than committed to one
			if high - low > 40 and here > low + 8 and here < high - 8 then
				partial += 1
			end
		end
	end

	return partial
end

local function render(frames)
	local capture
	for _ = 1, frames do
		capture = camera:Render()
	end
	return capture
end

-- The built-in pass first, for something to compare against. It softens edges
-- by looking at one frame, so a single render is all it ever has.
local builtin = render(1)
local builtinPartial = countPartialCoverage(builtin)
print(`built-in antialias: {builtinPartial} pixels of partial coverage`)
builtin:Save("captures/taa_builtin.png")

-- The swap. Both bindings are needed and the order matters: samplers are bound
-- in the order they were first set, after the camera's own output at slot 0,
-- and taa.frag declares History at binding 1 and Velocity at binding 2.
local taa = Instance.new("PostProcessShader")
taa.Name = "TemporalAntialias"
taa.Preset = Enum.PresetShaders.TemporalAntialias
taa:SetRenderTexture(Enum.ShaderProperty.HistoryTexture, Enum.RenderTexture.History)
taa:SetRenderTexture(Enum.ShaderProperty.VelocityTexture, Enum.RenderTexture.Velocity)
-- Both optional; these are the defaults the shader falls back to
taa:SetNumber(Enum.ShaderProperty.Feedback, 0.92)
taa:SetNumber(Enum.ShaderProperty.Clamping, 1.25)

RenderSettings.AntialiasShader = taa
print(`swapped in {taa.Name}, which reads {#taa:ListImages()} engine textures`)

local resolved = render(SETTLE_FRAMES)
local resolvedPartial = countPartialCoverage(resolved)
print(`temporal antialias: {resolvedPartial} pixels of partial coverage`)
resolved:Save("captures/taa_resolved.png")

if resolvedPartial > builtinPartial then
	local ratio = string.format("%.1fx", resolvedPartial / math.max(builtinPartial, 1))
	print(`the temporal pass recovered {ratio} more edge detail`)
else
	print("the temporal pass did not resolve more edge detail than the built-in one")
end

-- Now the part that needs motion vectors. Walk the red block across the wall a
-- pixel or so a frame and capture it mid-stride: with reprojection it stays a
-- block, and without it the history smears a trail behind it.
for step = 1, SETTLE_FRAMES do
	mover.Position = Vector3.new(-12 + step * 0.9, 8, 6)
	camera:Render()
end

local moving = camera:Render()
moving:Save("captures/taa_moving.png")
print("saved captures/taa_moving.png -- the red block should have no trail behind it")

-- Putting it back is the same one line. The engine's own pass returns, and the
-- camera stops jittering and stops keeping a history, because nothing is asking
-- for either any more.
RenderSettings.AntialiasShader = nil
local restored = render(2)
print(`restored the built-in pass: {countPartialCoverage(restored)} pixels of partial coverage`)

RunService.PreRender:Connect(function()
	camera.CFrame = CFrame.lookAt(camera.CFrame.Position, wall.Position)
end)

-- A SkyGrid: blocks on a lattice with a block-sized gap between them, floating
-- in the void, the way Minecraft's SkyGrid map does it. There is no terrain and
-- nothing is placed by hand -- every block comes from its own grid coordinate,
-- which is what makes it a stress test rather than a scene. The part count is
-- EXTENT squared times the layers, so widening the grid costs four times what
-- it looks like it should.
--
-- It prints the part count, build time and frame rate it settled at. Press F6
-- for the flame chart, or start with --profile to write the report and quit.
--
-- On scale: a 10000x10000 stud field at this spacing is 5000 x 5000 = twenty
-- five million parts in ONE layer, which is gigabytes before a triangle is
-- drawn. Raise EXTENT until the report says otherwise.

local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

-- Blocks along each horizontal side, and how many layers above and below the
-- origin. Total parts is EXTENT * EXTENT * (LAYERS_ABOVE + LAYERS_BELOW), so
-- 128 x 128 x 8 is a little over a hundred and thirty thousand.
--
-- The grid hangs downward as well as standing up, which is what a skygrid
-- actually looks like from inside: blocks above, below and every way out.
local EXTENT = 128
local LAYERS_ABOVE = 4
local LAYERS_BELOW = 4

-- A block and the gap beside it are the same size, which is the whole idea of
-- a skygrid: you can see and fall through every space between them
local CUBE = 4
local SPACING = CUBE * 2
local LAYER_HEIGHT = SPACING * 2

-- Shapes in the mix, so the profiler's per-class counters have more than one
-- class to report. Weighted towards blocks the way a real grid would be.
local SHAPES = {
	Enum.PartType.Block,
	Enum.PartType.Block,
	Enum.PartType.Block,
	Enum.PartType.Ball,
	Enum.PartType.Cylinder,
	Enum.PartType.Wedge,
}

local PALETTE = {
	Color3.fromRGB(126, 176, 90),
	Color3.fromRGB(150, 120, 84),
	Color3.fromRGB(122, 122, 130),
	Color3.fromRGB(196, 178, 120),
	Color3.fromRGB(92, 128, 176),
	Color3.fromRGB(168, 96, 96),
	Color3.fromRGB(210, 200, 170),
	Color3.fromRGB(70, 96, 74),
}

-- Shared: an EditableImage is uploaded once and kept until it changes, so four
-- of them is four uploads however many parts point at them
local function makeTexture(base, accent, kind)
	local image = Instance.new("EditableImage")
	image:Resize(Vector2.new(32, 32))
	image:DrawRectangle(Vector2.zero, Vector2.new(32, 32), base, 0, Enum.ImageCombineType.Overwrite)

	if kind == "checker" then
		for y = 0, 3 do
			for x = 0, 3 do
				if (x + y) % 2 == 0 then
					image:DrawRectangle(
						Vector2.new(x * 8, y * 8),
						Vector2.new(8, 8),
						accent,
						0,
						Enum.ImageCombineType.Overwrite
					)
				end
			end
		end
	elseif kind == "stripes" then
		for y = 0, 7 do
			image:DrawRectangle(Vector2.new(0, y * 4), Vector2.new(32, 2), accent, 0, Enum.ImageCombineType.Overwrite)
		end
	elseif kind == "spot" then
		image:DrawCircle(Vector2.new(16, 16), 10, accent, 0, Enum.ImageCombineType.BlendSourceOver)
	else
		image:DrawRectangle(Vector2.new(2, 2), Vector2.new(28, 28), accent, 0, Enum.ImageCombineType.Overwrite)
		image:DrawRectangle(Vector2.new(8, 8), Vector2.new(16, 16), base, 0, Enum.ImageCombineType.Overwrite)
	end

	return image
end

local TEXTURES = {
	makeTexture(Color3.fromRGB(200, 190, 170), Color3.fromRGB(120, 100, 80), "checker"),
	makeTexture(Color3.fromRGB(90, 130, 180), Color3.fromRGB(210, 220, 235), "stripes"),
	makeTexture(Color3.fromRGB(150, 90, 90), Color3.fromRGB(240, 200, 120), "spot"),
	makeTexture(Color3.fromRGB(110, 150, 110), Color3.fromRGB(60, 80, 60), "frame"),
}

-- Parts sharing a mesh and a texture batch together, so this decides how many
-- batches the grid ends up in
local TEXTURED_SHARE = 0.20

local started = os.clock()
local random = Random.new(4242)
local half = (EXTENT - 1) * SPACING / 2

for layer = -LAYERS_BELOW, LAYERS_ABOVE - 1 do
	for column = 0, EXTENT - 1 do
		for row = 0, EXTENT - 1 do
			local block = Instance.new("Part")
			block.Anchored = true
			block.Size = Vector3.new(CUBE, CUBE, CUBE)
			block.Position = Vector3.new(
				column * SPACING - half,
				layer * LAYER_HEIGHT,
				row * SPACING - half
			)
			block.Shape = SHAPES[random:NextInteger(1, #SHAPES)]
			block.Color = PALETTE[random:NextInteger(1, #PALETTE)]
			-- The shadow map covers a fixed box near the origin, so all but the
			-- middle of the grid falls outside it anyway. Leaving it on would
			-- put every one of these through the shadow pass to no effect.
			block.CastShadow = false

			if random:NextNumber() < TEXTURED_SHARE then
				block.SurfaceImage = TEXTURES[random:NextInteger(1, #TEXTURES)]
				block.SurfaceFace = Enum.NormalId.Front
			end

			block.Parent = Workspace
		end
	end
end

local layers = LAYERS_ABOVE + LAYERS_BELOW
local parts = EXTENT * EXTENT * layers
local built = os.clock() - started
print(
	`skygrid: {parts} parts in a {EXTENT}x{EXTENT} grid, {layers} layers, `
		.. `{math.floor(TEXTURED_SHARE * 100)}% textured, built in {string.format("%.2f", built)}s`
)

-- Each is a real camera rendering the same world, so the strip is more full
-- passes over the scene -- the point is to see what that costs
-- Opposite corners, so the two panes are looking at the grid down the same
-- diagonal from either end and never see the same thing
local SIDES = {
	{ Name = "NorthEast", Offset = Vector3.new(1, 0, -1).Unit },
	{ Name = "SouthWest", Offset = Vector3.new(-1, 0, 1).Unit },
}

local watchCentre = Vector3.new(0, (LAYERS_ABOVE - LAYERS_BELOW - 1) * LAYER_HEIGHT / 2, 0)

-- How far off centre each pane drifts, and how fast. A cone rather than a full
-- orbit: enough that the pane is plainly alive and its frustum keeps changing,
-- little enough that it stays pointed at the grid.
local CONE = half * 0.35
local watchers = {}

for index, side in SIDES do
	local watcher = Instance.new("Camera")
	watcher.Name = side.Name
	watcher.CameraType = Enum.CameraType.Scriptable
	watcher.FieldOfView = 70
	-- Drawn into the window alongside the main view rather than into a target
	-- of its own, each in its own quarter of a strip along the top
	watcher.DrawToWindow = true
	watcher.WindowPosition = UDim2.new((index - 1) * 0.5, 0, 0, 0)
	watcher.WindowSize = UDim2.new(0.5, 0, 0.28, 0)
	-- Redrawn this often, blitted every frame regardless, so between redraws
	-- it costs a copy rather than a pass over the world
	watcher.FPS = 30
	watcher.Parent = Workspace

	watchers[index] = {
		Camera = watcher,
		Position = watchCentre + side.Offset * (half * 1.25) + Vector3.new(0, layers * LAYER_HEIGHT * 0.3, 0),
		-- Started apart so the four are never sweeping in step
		Phase = (index - 1) * (math.pi / 2),
	}
end
print(`skygrid: {#SIDES} diagonal cameras across the top, {watchers[1].Camera.FPS} fps each`)

-- Moving on its own is a measurement decision: a still camera lets the engine
-- reuse the picture it already had, and a stress test that stops drawing is not
-- testing anything. Scriptable so Camera:Step leaves this CFrame alone.
local camera = Workspace.CurrentCamera
camera.CameraType = Enum.CameraType.Scriptable

local ORBIT_SECONDS = 40
local orbitRadius = half * 1.15
-- Above the middle of the stack, looking back down into it, so the layers
-- below the origin are in the picture rather than behind the camera
local orbitCentre = Vector3.new(0, (LAYERS_ABOVE - LAYERS_BELOW - 1) * LAYER_HEIGHT / 2, 0)
local orbitHeight = layers * LAYER_HEIGHT * 0.45
local angle = 0.0

-- The report. Frame times are collected for a few seconds and then printed
-- once, rather than logged every frame, because logging every frame is itself
-- enough work to change the answer.
local SETTLE = 2.0
local MEASURE = 5.0

local elapsed = 0.0
local measured = 0.0
local frames = 0
local slowest = 0.0
local reported = false

RunService.PreRender:Connect(function(deltaTime)
	local now = Workspace.DistributedGameTime

	-- A cone by moving what they look at, not where they are: moving them
	-- would be an orbit and would leave the grid
	for _, watcher in watchers do
		local wobble = Vector3.new(
			math.cos(now * 0.6 + watcher.Phase) * CONE,
			math.sin(now * 0.8 + watcher.Phase) * CONE * 0.4,
			math.sin(now * 0.5 + watcher.Phase) * CONE
		)
		watcher.Camera.CFrame = CFrame.lookAt(watcher.Position, watchCentre + wobble)
	end

	angle += deltaTime * (math.pi * 2 / ORBIT_SECONDS)
	camera.CFrame = CFrame.lookAt(
		orbitCentre + Vector3.new(math.cos(angle) * orbitRadius, orbitHeight, math.sin(angle) * orbitRadius),
		orbitCentre
	)

	elapsed += deltaTime
	if elapsed < SETTLE or reported then
		return
	end

	measured += deltaTime
	frames += 1
	slowest = math.max(slowest, deltaTime)

	if measured >= MEASURE then
		reported = true
		local average = frames / measured
		print(
			`skygrid: {parts} parts held {string.format("%.1f", average)} fps `
				.. `({string.format("%.2f", 1000 / average)} ms a frame), `
				.. `worst frame {string.format("%.1f", slowest * 1000)} ms`
		)
		print("skygrid: press F3 for the counter and F6 for the breakdown")
	end
end)

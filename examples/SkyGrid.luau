-- A SkyGrid: blocks on a lattice with a block-sized gap between them, floating
-- in the void, the way Minecraft's SkyGrid map does it. There is no terrain and
-- nothing is placed by hand -- every block comes from its own grid coordinate,
-- which is what makes it a stress test rather than a scene. The part count is
-- EXTENT squared times the layers, so widening the grid costs four times what
-- it looks like it should.
--
-- It is here to find where the engine stops coping, so it says so out loud:
-- after a few seconds it prints the part count, the build time and the frame
-- rate it settled at. Press F6 for the flame chart while it runs, or start with
-- --profile to have it write the report and quit, and the answer to "what does
-- a hundred thousand parts cost" is a row on the chart rather than a guess.
--
-- Measured at 131072 parts on an unoptimised build: 3.4 fps, of which the
-- opaque pass is 220 ms and the frustum walk 54 ms, and the GPU is idle the
-- whole time. Every part costs a matrix, two uniform pushes and a draw call,
-- and nothing about that is batched yet.
--
-- On the scale of the request: a 10000x10000 stud field at this spacing is
-- 5000 x 5000 = twenty five million blocks in ONE layer. Each part carries a
-- CFrame, a size, a colour and its physical properties, so that alone is
-- several gigabytes before a single triangle is drawn -- and three separate
-- per-frame walks visit every part in the world (the scene signature, each
-- camera's frustum test, and the opaque pass). The numbers below are what this
-- engine can actually stand up; raise EXTENT until the report says otherwise.

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
local BLOCK = 4
local SPACING = BLOCK * 2
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

local started = os.clock()
local random = Random.new(4242)
local half = (EXTENT - 1) * SPACING / 2

for layer = -LAYERS_BELOW, LAYERS_ABOVE - 1 do
	for column = 0, EXTENT - 1 do
		for row = 0, EXTENT - 1 do
			local block = Instance.new("Part")
			block.Anchored = true
			block.Size = Vector3.new(BLOCK, BLOCK, BLOCK)
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
			block.Parent = Workspace
		end
	end
end

local layers = LAYERS_ABOVE + LAYERS_BELOW
local parts = EXTENT * EXTENT * layers
local built = os.clock() - started
print(`skygrid: {parts} parts in a {EXTENT}x{EXTENT} grid, {layers} layers, built in {string.format("%.2f", built)}s`)

-- Flown round the grid on its own, which is a measurement decision as much as a
-- presentation one: a camera that never moves lets the engine notice nothing
-- has changed and reuse the picture it already had, and a stress test that
-- stops drawing is not testing anything. Turning slowly means every frame walks
-- the frustum and draws for real.
--
-- Scriptable rather than Freecam, so Camera:Step leaves the CFrame set here
-- alone instead of rebuilding it from its own pitch and yaw.
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

-- A stage with a crowd in front of it and a camera down each wing, both looking
-- back out at the audience the way a concert's crowd cameras do. What each one
-- sees goes up on the screen behind the stage on its own side, as geometry with
-- a live picture on it rather than as a shader effect: the screens are lit,
-- shadowed and reflected in the world like any other part, and they happen to
-- be showing a camera.
--
-- Two of each because the interesting part is that they disagree: a block near
-- one camera is a speck on the other, and the wave runs the opposite way.
--
-- The crowd is a grid. Everything that varies is worked out from a row and a
-- column, through a seeded Random, so the same crowd comes back every run.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local FEED = Vector2.new(320, 240)

-- Columns run across the stage, rows back away from it
local COLUMNS = 20
local ROWS = 12
local SPACING = 3.0
-- Where the front row stands, in front of the stage's leading edge
local CROWD_FRONT = 5.0

local STAGE_WIDTH = 44
local STAGE_DEPTH = 20
local STAGE_TOP = 4

-- The sun is only up between 6 and 18; outside that everything falls back to
-- the shader's flat ambient and the venue goes black. The latitude is negative
-- to swing it behind the audience, because a stage faces its crowd and every
-- face worth seeing -- the screens especially -- points that way.
Lighting.ClockTime = 14
Lighting.GeographicLatitude = -40

local function block(name, size, position, colour)
	local part = Instance.new("Part")
	part.Name = name
	part.Anchored = true
	part.Size = size
	part.Position = position
	part.Color = colour
	part.Parent = Workspace
	return part
end

-- The venue floor, wide enough that the crowd never runs off the end of it
block("Ground", Vector3.new(400, 2, 400), Vector3.new(0, -1, 0), Color3.fromRGB(52, 50, 58))

-- The deck. Its leading edge is at z = 0, so everything in front of it is
-- audience and everything behind it is stage.
local deckCentre = -STAGE_DEPTH / 2
block(
	"Deck",
	Vector3.new(STAGE_WIDTH, STAGE_TOP, STAGE_DEPTH),
	Vector3.new(0, STAGE_TOP / 2, deckCentre),
	Color3.fromRGB(74, 71, 84)
)
block(
	"DeckLip",
	Vector3.new(STAGE_WIDTH, 0.6, 0.8),
	Vector3.new(0, STAGE_TOP, 0),
	Color3.fromRGB(210, 170, 70)
)

local BACKDROP_Z = -STAGE_DEPTH - 1
block(
	"Backdrop",
	Vector3.new(STAGE_WIDTH, 18, 1),
	Vector3.new(0, STAGE_TOP + 9, BACKDROP_Z),
	Color3.fromRGB(40, 38, 50)
)

-- Towers either side carrying the lighting beam across the top
for _, side in { -1, 1 } do
	block(
		"Tower",
		Vector3.new(2, 20, 2),
		Vector3.new(side * (STAGE_WIDTH / 2 - 1), STAGE_TOP + 10, BACKDROP_Z + 2),
		Color3.fromRGB(78, 76, 88)
	)
end
block(
	"Truss",
	Vector3.new(STAGE_WIDTH, 1.2, 1.2),
	Vector3.new(0, STAGE_TOP + 19, BACKDROP_Z + 2),
	Color3.fromRGB(96, 94, 106)
)

-- Nothing emits light yet, so a lamp can only be its colour or be black. A run
-- of that along the beam reads as lamps switching on and off.
local LAMP_COLOURS = {
	Color3.fromRGB(255, 90, 70),
	Color3.fromRGB(255, 190, 80),
	Color3.fromRGB(90, 220, 160),
	Color3.fromRGB(90, 170, 255),
	Color3.fromRGB(190, 110, 255),
}
local LAMP_OFF = Color3.fromRGB(16, 16, 20)

local lamps = {}
for index = 0, 8 do
	local lit = LAMP_COLOURS[index % #LAMP_COLOURS + 1]
	local lamp = block(
		"Lamp",
		Vector3.new(1.4, 1.4, 1.4),
		Vector3.new(-18 + index * 4.5, STAGE_TOP + 17.8, BACKDROP_Z + 2),
		lit
	)
	lamp.Shape = Enum.PartType.Ball
	lamp.CastShadow = false

	table.insert(lamps, {
		Part = lamp,
		Lit = lit,
		-- Each lamp is a little further through the cycle than the one to its
		-- left, so the run travels along the beam rather than the whole rig
		-- blinking at once
		Phase = index * 0.55,
	})
end

-- Speaker stacks, one per wing, just inside where the cameras sit
for _, side in { -1, 1 } do
	for tier = 0, 3 do
		block(
			"Speaker",
			Vector3.new(3.4, 2.6, 3),
			Vector3.new(side * 17, STAGE_TOP + 1.3 + tier * 2.6, deckCentre - 4),
			Color3.fromRGB(34, 33, 40)
		)
	end
end

local performer = block("Performer", Vector3.new(2, 5, 2), Vector3.new(0, STAGE_TOP + 2.5, deckCentre + 2), Color3.fromRGB(240, 220, 180))

-- The crowd. Row and column decide everything: where a block stands, how tall
-- it is, what colour it is and when it jumps.
local random = Random.new(20240729)
local crowd = table.create(COLUMNS * ROWS)

for row = 0, ROWS - 1 do
	for column = 0, COLUMNS - 1 do
		local x = (column - (COLUMNS - 1) / 2) * SPACING
		local z = CROWD_FRONT + row * SPACING

		-- Nudged off the exact lattice, or the grid reads as a car park. Small
		-- enough that the rows and columns still line up when seen down the
		-- length of the crowd, which is what the wing cameras are looking at.
		x += random:NextNumber(-0.5, 0.5)
		z += random:NextNumber(-0.4, 0.4)

		local height = random:NextNumber(3.4, 4.8)
		-- Cooler towards the back, so depth reads without needing fog
		local depth = row / math.max(ROWS - 1, 1)
		local colour = Color3.fromHSV(
			random:NextNumber(0, 1),
			0.42 - depth * 0.16,
			0.88 - depth * 0.26
		)

		local part = block("Fan", Vector3.new(1.5, height, 1.5), Vector3.new(x, height / 2, z), colour)
		-- The shadow map covers a fixed box around the origin, so most of the
		-- crowd falls outside it anyway. Turning it off here keeps the shadow
		-- pass to the stage, where it lands on something.
		part.CastShadow = false

		table.insert(crowd, {
			Part = part,
			X = x,
			Z = z,
			Rest = height / 2,
			Lift = random:NextNumber(0.5, 1.4),
			-- The wave travels back through the crowd, so how far a block is
			-- from the stage decides when it goes up
			Phase = z * 0.28 + x * 0.05,
		})
	end
end

print(`crowd of {#crowd} blocks in a {COLUMNS}x{ROWS} grid`)

-- One camera per wing, at the front corner of the deck, turned back on the
-- audience rather than the act
local function wingCamera(name, side)
	local camera = Instance.new("Camera")
	camera.Name = name
	camera.CameraType = Enum.CameraType.Scriptable
	camera.ViewportSize = FEED
	camera.FieldOfView = 62
	-- The feeds are worth less than the view the player is looking at, so they
	-- redraw at their own rate rather than every frame
	camera.FPS = 30
	camera.CFrame = CFrame.lookAt(
		Vector3.new(side * (STAGE_WIDTH / 2 - 3), STAGE_TOP + 3.5, -1),
		Vector3.new(-side * 6, 3, CROWD_FRONT + ROWS * SPACING * 0.45)
	)
	camera.Parent = Workspace
	return camera
end

local stageLeft = wingCamera("StageLeft", -1)
local stageRight = wingCamera("StageRight", 1)

-- A screen per camera, on the backdrop, each showing its own wing. Placed on
-- the far side from the camera that feeds it, so the audience sees the wing
-- opposite the one they are nearest and the two pictures never agree.
local function screen(name, x, camera)
	local part = block(
		name,
		Vector3.new(13, 9, 0.6),
		Vector3.new(x, STAGE_TOP + 10, BACKDROP_Z + 0.9),
		Color3.new(1, 1, 1)
	)
	part.SurfaceCamera = camera
	-- Back is +Z, which is the side the crowd is standing on
	part.SurfaceFace = Enum.NormalId.Back
	part.CastShadow = false
	return part
end

screen("ScreenLeft", -14, stageRight)
screen("ScreenRight", 14, stageLeft)
print("two screens on the backdrop, each showing the opposite wing")

-- Looking down -Z from an unrotated camera puts the stage straight ahead, which
-- is what the freecam rebuilds its rotation to the moment the mouse moves. Set
-- as a rotation instead, the first mouse movement would snap the view away.
Workspace.CurrentCamera.CFrame = CFrame.new(Vector3.new(0, 12, 40))

RunService.PreRender:Connect(function()
	local now = Workspace.DistributedGameTime

	for _, fan in crowd do
		-- Rectified rather than a plain sine, so a block leaves the ground,
		-- comes down and waits, instead of sinking into the floor for half of
		-- every cycle
		local hop = math.max(math.sin(now * 3.4 - fan.Phase), 0)
		fan.Part.Position = Vector3.new(fan.X, fan.Rest + hop * hop * fan.Lift, fan.Z)
	end

	performer.Orientation = Vector3.new(0, math.sin(now * 1.6) * 35, 0)

	-- Hard on or off: dimming reads as a ball changing colour, switching
	-- reads as a lamp
	for _, lamp in lamps do
		local lit = math.sin(now * 6.0 - lamp.Phase) > 0.1
		lamp.Part.Color = lit and lamp.Lit or LAMP_OFF
	end
end)

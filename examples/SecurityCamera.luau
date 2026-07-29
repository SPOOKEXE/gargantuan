-- Four fixed cameras watching one room, each through the security_camera
-- post-process, composited into a single quad-view monitor.
--
-- The feeds come off the GPU; the monitor furniture on top of them -- borders,
-- the recording dot, the timestamp -- is drawn on the CPU with EditableImage.
-- There is no font yet, so the clock is drawn as seven-segment digits.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local FEED = Vector2.new(320, 240)
local MONITOR = Vector2.new(FEED.X * 2 + 12, FEED.Y * 2 + 12)

local AMBER = Color3.fromRGB(255, 190, 60)
local DIM = Color3.fromRGB(70, 70, 78)

Lighting.ClockTime = 12
Lighting.GeographicLatitude = 0

local function part(name, size, position, colour)
	local instance = Instance.new("Part")
	instance.Name = name
	instance.Anchored = true
	instance.Size = size
	instance.Position = position
	instance.Color = colour
	instance.Parent = Workspace
	return instance
end

-- A room worth watching: floor, back walls, some furniture and a subject
part("Floor", Vector3.new(60, 2, 60), Vector3.new(0, -1, 0), Color3.fromRGB(150, 148, 140))
part("WallNorth", Vector3.new(60, 24, 2), Vector3.new(0, 12, -30), Color3.fromRGB(200, 198, 190))
part("WallWest", Vector3.new(2, 24, 60), Vector3.new(-30, 12, 0), Color3.fromRGB(190, 188, 182))

part("Crate", Vector3.new(6, 6, 6), Vector3.new(-12, 3, -8), Color3.fromRGB(150, 110, 60))
part("Crate2", Vector3.new(5, 5, 5), Vector3.new(-12, 8.5, -8), Color3.fromRGB(140, 100, 55))
part("Shelf", Vector3.new(16, 1, 4), Vector3.new(14, 8, -20), Color3.fromRGB(120, 120, 128))
part("Pillar", Vector3.new(4, 24, 4), Vector3.new(10, 12, 6), Color3.fromRGB(180, 178, 172))
part("DoorFrame", Vector3.new(1, 14, 8), Vector3.new(-29, 7, 14), Color3.fromRGB(90, 70, 50))

local subject = part("Subject", Vector3.new(3, 7, 3), Vector3.new(0, 3.5, 4), Color3.fromRGB(210, 70, 60))

-- Four fixed positions, the last one on infrared
local layout = {
	{ name = "CAM 01", position = Vector3.new(-19, 13, 18), night = false },
	{ name = "CAM 02", position = Vector3.new(20, 14, 16), night = false },
	{ name = "CAM 03", position = Vector3.new(3, 16, -21), night = false },
	{ name = "CAM 04", position = Vector3.new(-20, 9, -19), night = true },
}

local feeds = {}
for index, entry in layout do
	local camera = Instance.new("Camera")
	camera.Name = entry.name
	camera.CameraType = Enum.CameraType.Scriptable
	camera.ViewportSize = FEED
	camera.CFrame = CFrame.lookAt(entry.position, subject.Position)
	camera.Parent = Workspace

	local shader = Instance.new("PostProcessShader")
	shader.Name = "SecurityLook"
	shader.Source = Enum.PresetShaders.SecurityCamera
	-- Interlacing and sensor noise are driven by builtin.Time, so this pass
	-- has to keep running once the room stops moving
	shader.RedrawEveryFrame = true

	local look = shader.Properties
	look:SetNumber(Enum.ShaderProperty.Distortion, entry.night and 0.5 or 0.32)
	look:SetNumber(Enum.ShaderProperty.Scanlines, 1)
	look:SetNumber(Enum.ShaderProperty.Noise, entry.night and 0.10 or 0.05)
	look:SetNumber(Enum.ShaderProperty.NightVision, entry.night and 1 or 0)
	look:SetNumber(Enum.ShaderProperty.Contrast, entry.night and 1.05 or 1.12)
	look:SetNumber(Enum.ShaderProperty.Gain, entry.night and 1 or 1.55)

	camera:AddShader(shader)

	feeds[index] = { camera = camera, entry = entry }
end

-- Seven-segment digits, since there is no text drawing yet. Each segment is a
-- rectangle; the table says which are lit for each numeral.
local SEGMENTS = {
	[0] = { "a", "b", "c", "d", "e", "f" },
	[1] = { "b", "c" },
	[2] = { "a", "b", "g", "e", "d" },
	[3] = { "a", "b", "g", "c", "d" },
	[4] = { "f", "g", "b", "c" },
	[5] = { "a", "f", "g", "c", "d" },
	[6] = { "a", "f", "g", "e", "c", "d" },
	[7] = { "a", "b", "c" },
	[8] = { "a", "b", "c", "d", "e", "f", "g" },
	[9] = { "a", "b", "c", "d", "f", "g" },
}

local function drawDigit(image, x, y, width, height, digit, colour)
	local thickness = math.max(math.floor(width / 5), 1)
	local half = height / 2
	local rects = {
		a = { x, y, width, thickness },
		b = { x + width - thickness, y, thickness, half },
		c = { x + width - thickness, y + half, thickness, half },
		d = { x, y + height - thickness, width, thickness },
		e = { x, y + half, thickness, half },
		f = { x, y, thickness, half },
		g = { x, y + half - thickness / 2, width, thickness },
	}

	-- Unlit segments still show faintly, the way a real display does
	for _, name in { "a", "b", "c", "d", "e", "f", "g" } do
		local r = rects[name]
		image:DrawRectangle(Vector2.new(r[1], r[2]), Vector2.new(r[3], r[4]), DIM, 0.75, Enum.ImageCombineType.BlendSourceOver)
	end

	for _, name in SEGMENTS[digit] do
		local r = rects[name]
		image:DrawRectangle(Vector2.new(r[1], r[2]), Vector2.new(r[3], r[4]), colour, 0, Enum.ImageCombineType.BlendSourceOver)
	end
end

local function drawNumber(image, x, y, value, digits, colour)
	local width, height, gap = 9, 16, 4
	for position = digits - 1, 0, -1 do
		local digit = math.floor(value / 10 ^ position) % 10
		drawDigit(image, x, y, width, height, digit, colour)
		x += width + gap
	end
	return x
end

local function drawColon(image, x, y, colour)
	image:DrawRectangle(Vector2.new(x, y + 4), Vector2.new(3, 3), colour, 0, Enum.ImageCombineType.BlendSourceOver)
	image:DrawRectangle(Vector2.new(x, y + 11), Vector2.new(3, 3), colour, 0, Enum.ImageCombineType.BlendSourceOver)
	return x + 8
end

-- A short bar code standing in for the camera's name, one bar per character
local function drawLabel(image, x, y, text, colour)
	for index = 1, #text do
		local byte = string.byte(text, index)
		if byte ~= 32 then
			local height = 3 + (byte % 5)
			image:DrawRectangle(Vector2.new(x, y + 8 - height), Vector2.new(2, height), colour, 0, Enum.ImageCombineType.BlendSourceOver)
		end
		x += 4
	end
end

local function composeMonitor(images, seconds, blink)
	local monitor = Instance.new("EditableImage")
	monitor:Resize(MONITOR)
	monitor:DrawRectangle(Vector2.zero, MONITOR, Color3.fromRGB(12, 12, 16), 0, Enum.ImageCombineType.Overwrite)

	local origins = {
		Vector2.new(4, 4),
		Vector2.new(FEED.X + 8, 4),
		Vector2.new(4, FEED.Y + 8),
		Vector2.new(FEED.X + 8, FEED.Y + 8),
	}

	for index, image in images do
		local origin = origins[index]
		monitor:DrawImage(origin, image, Enum.ImageCombineType.Overwrite)

		-- Pane border
		local corner = origin + FEED
		monitor:DrawLine(origin, Vector2.new(corner.X, origin.Y), DIM, 0.3, 1, Enum.ImageCombineType.BlendSourceOver)
		monitor:DrawLine(Vector2.new(origin.X, corner.Y), corner, DIM, 0.3, 1, Enum.ImageCombineType.BlendSourceOver)
		monitor:DrawLine(origin, Vector2.new(origin.X, corner.Y), DIM, 0.3, 1, Enum.ImageCombineType.BlendSourceOver)
		monitor:DrawLine(Vector2.new(corner.X, origin.Y), corner, DIM, 0.3, 1, Enum.ImageCombineType.BlendSourceOver)

		drawLabel(monitor, origin.X + 8, origin.Y + 8, layout[index].name, AMBER)

		-- Crosshair in the middle of each pane
		local centre = origin + FEED / 2
		monitor:DrawLine(centre - Vector2.new(6, 0), centre + Vector2.new(6, 0), AMBER, 0.6, 1, Enum.ImageCombineType.BlendSourceOver)
		monitor:DrawLine(centre - Vector2.new(0, 6), centre + Vector2.new(0, 6), AMBER, 0.6, 1, Enum.ImageCombineType.BlendSourceOver)
	end

	-- Timestamp along the bottom, hours:minutes:seconds
	local hours = math.floor(seconds / 3600) % 24
	local minutes = math.floor(seconds / 60) % 60
	local wholeSeconds = math.floor(seconds) % 60

	local x = 14
	local y = MONITOR.Y - 26
	x = drawNumber(monitor, x, y, hours, 2, AMBER)
	x = drawColon(monitor, x, y, AMBER)
	x = drawNumber(monitor, x, y, minutes, 2, AMBER)
	x = drawColon(monitor, x, y, AMBER)
	drawNumber(monitor, x, y, wholeSeconds, 2, AMBER)

	-- Recording dot, blinking
	if blink then
		monitor:DrawCircle(Vector2.new(MONITOR.X - 26, y + 8), 6, Color3.fromRGB(240, 40, 40), 0, Enum.ImageCombineType.BlendSourceOver)
	end

	return monitor
end

-- Capture every feed, then build the monitor from them
local captured = {}
for index, feed in feeds do
	task.wait(0.05)
	local image = feed.camera:Render()
	captured[index] = image

	local path = `security/feed_{index}.png`
	if image:Save(path) then
		print(`saved {path} ({feed.entry.name}{feed.entry.night and ", infrared" or ""})`)
	else
		warn(`could not save {path}: {image.SaveError}`)
	end
end

local monitor = composeMonitor(captured, Workspace.DistributedGameTime + 8 * 3600 + 47 * 60, true)
if monitor:Save("security/monitor.png") then
	print(`saved security/monitor.png at {monitor.Size.X}x{monitor.Size.Y}`)
else
	warn(`could not save the monitor: {monitor.SaveError}`)
end

-- A second still with the dot off, to show the blink
local blinkOff = composeMonitor(captured, Workspace.DistributedGameTime + 8 * 3600 + 47 * 60 + 1, false)
blinkOff:Save("security/monitor_blink_off.png")

-- Leave the cameras running so the window shows the room live
RunService.PreRender:Connect(function()
	for _, feed in feeds do
		feed.camera.CFrame = CFrame.lookAt(feed.camera.CFrame.Position, subject.Position)
	end
end)

print("done")

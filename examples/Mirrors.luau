--   gargantuan --uncapped --script Mirrors.luau
-- This demo uses reflected view direction without reflected position or mirror-plane clipping.

local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")
local Lighting = game:GetService("Lighting")

local SAVE_DIRECTORY = "."

Lighting.ClockTime = 10

local WALL_Z = -14
local PANEL_THICKNESS = 0.6
local NORMAL = Vector3.new(0, 0, 1)

local viewer = Workspace.CurrentCamera
viewer.CameraType = Enum.CameraType.Scriptable
viewer.FieldOfView = 70
-- Zero removes Camera.FPS's default 24 FPS cap; --uncapped also disables vsync.
viewer.FPS = 0

-- Tint is required; vignette.frag multiplies by it and an unset vec4 is zero.
local vignette = Instance.new("PostProcessShader")
vignette.Preset = Enum.PresetShaders.Vignette
vignette.Properties:SetNumber("Intensity", 0.35)
vignette.Properties:SetColor3("Tint", Color3.new(1, 1, 1))
viewer:AddShader(vignette)

local floor = Instance.new("Part")
floor.Shape = Enum.PartType.Block
floor.Size = Vector3.new(120, 1, 90)
floor.Position = Vector3.new(0, -1, 5)
floor.Color = Color3.new(0.62, 0.64, 0.70)
floor.Anchored = true
floor.CastShadow = true
floor.Parent = Workspace

for index = 1, 9 do
	local pillar = Instance.new("Part")
	pillar.Shape = if index % 3 == 0 then Enum.PartType.Ball else Enum.PartType.Block
	pillar.Size = Vector3.new(6, 26, 6)
	pillar.Position = Vector3.new(-32 + (index - 1) * 8, 12, 36)
	pillar.Color = Color3.fromHSV((index - 1) / 9, 0.85, 0.95)
	pillar.Anchored = true
	pillar.CastShadow = true
	pillar.Parent = Workspace
end

local props = {}
for index, spec in
	ipairs({
		{ position = Vector3.new(-19, 3, 6), colour = Color3.new(0.95, 0.25, 0.20), shape = Enum.PartType.Block },
		{ position = Vector3.new(0, 3, 14), colour = Color3.new(0.20, 0.85, 0.35), shape = Enum.PartType.Ball },
		{ position = Vector3.new(19, 3, 6), colour = Color3.new(0.25, 0.45, 0.95), shape = Enum.PartType.Block },
		{ position = Vector3.new(-27, 6, 16), colour = Color3.new(0.98, 0.80, 0.15), shape = Enum.PartType.Ball },
		{ position = Vector3.new(27, 7, 16), colour = Color3.new(0.85, 0.30, 0.85), shape = Enum.PartType.Block },
	})
do
	local prop = Instance.new("Part")
	prop.Shape = spec.shape
	prop.Size = Vector3.new(4, if index % 2 == 0 then 4 else 6, 4)
	prop.Position = spec.position
	prop.Color = spec.colour
	prop.Anchored = true
	prop.CastShadow = true
	prop.Parent = Workspace
	table.insert(props, prop)
end

local MIRRORS = {
	{ x = -21, label = "plain", kind = "none" },
	{ x = -10.5, label = "greyscale", kind = "compute", preset = Enum.PresetShaders.BlackWhite,
		params = { Strength = 1.0 } },
	{ x = 0, label = "thermal", kind = "compute", preset = Enum.PresetShaders.ThermalRed,
		params = { Contrast = 1.6 } },
	{ x = 10.5, label = "swirl", kind = "fragment", preset = Enum.PresetShaders.Swirl,
		params = { Strength = 2.2, Radius = 0.75 } },
	{ x = 21, label = "dither", kind = "compute", preset = Enum.PresetShaders.Dither,
		params = { Levels = 3 } },
}

for _, mirror in ipairs(MIRRORS) do
	local camera = Instance.new("Camera")
	camera.CameraType = Enum.CameraType.Scriptable
	camera.ViewportSize = Vector2.new(384, 384)
	-- Zero renders every frame and bypasses RenderSettings.MaxCameraFPS.
	camera.FPS = 0
	camera.Enabled = true
	camera.Parent = Workspace
	mirror.Camera = camera

	if mirror.kind ~= "none" then
		-- The shader class must match the preset stage.
		local shader = Instance.new(if mirror.kind == "compute" then "ComputeShader" else "PostProcessShader")
		shader.Preset = mirror.preset
		for name, value in pairs(mirror.params) do
			shader.Properties:SetNumber(name, value)
		end
		camera:AddShader(shader)
		mirror.Shader = shader
	end

	local panel = Instance.new("Part")
	panel.Shape = Enum.PartType.Block
	panel.Size = Vector3.new(10, 10, PANEL_THICKNESS)
	panel.Position = Vector3.new(mirror.x, 6, WALL_Z)
	panel.Color = Color3.new(1, 1, 1)
	panel.Anchored = true
	panel.CastShadow = true
	panel.Parent = Workspace
	panel.SurfaceCamera = camera
	-- Back is the +Z face visible to the viewer.
	panel.SurfaceFace = Enum.NormalId.Back
	panel.SurfaceTiling = Vector2.new(1, 1)
	mirror.Panel = panel

	local frame = Instance.new("Part")
	frame.Shape = Enum.PartType.Block
	frame.Size = Vector3.new(11, 11, 0.4)
	frame.Position = Vector3.new(mirror.x, 6, WALL_Z - 0.5)
	frame.Color = Color3.new(0.12, 0.12, 0.14)
	frame.Anchored = true
	frame.CastShadow = true
	frame.Parent = Workspace
end

local function Reflect(direction)
	return direction - NORMAL * (2 * direction:Dot(NORMAL))
end

local SHOT_TIMES = { 1.5, 4.0 }

local elapsed = 0
local shots = 0

RunService.PreRender:Connect(function(deltaTime)
	elapsed += deltaTime

	local sway = math.sin(elapsed * 0.4)
	local eye = Vector3.new(sway * 11, 7.5, 19)
	local target = Vector3.new(sway * 4, 6.5, WALL_Z)
	viewer.CFrame = CFrame.lookAt(eye, target)

	props[2].Position = Vector3.new(math.sin(elapsed * 1.5) * 12, 3, 14)

	local forward = (target - eye).Unit
	local reflected = Reflect(forward)
	for _, mirror in ipairs(MIRRORS) do
		local at = Vector3.new(mirror.x, 6, WALL_Z + PANEL_THICKNESS)
		mirror.Camera.CFrame = CFrame.lookAt(at, at + reflected)
	end

	-- Capture by elapsed time; frame counts are unstable when uncapped.
	if shots < #SHOT_TIMES and elapsed >= SHOT_TIMES[shots + 1] then
		shots += 1
		local index = shots
		task.spawn(function()
			local image = viewer:Render()
			if not image then
				print("Mirrors: Render() returned nothing")
				return
			end
			local path = `{SAVE_DIRECTORY}/mirrors-{index}.png`
			local ok = image:Save(path, Enum.SaveFormat.PNG)
			print(`Mirrors: shot {index} {image.Size.X}x{image.Size.Y} saved={ok} error="{image.SaveError}"`)

			if index == 1 then
				for _, mirror in ipairs(MIRRORS) do
					local feed = mirror.Camera:Render()
					if feed then
						local saved = feed:Save(`{SAVE_DIRECTORY}/mirror-{mirror.label}.png`, Enum.SaveFormat.PNG)
						print(`Mirrors: feed {mirror.label} {feed.Size.X}x{feed.Size.Y} saved={saved}`)
					else
						print(`Mirrors: feed {mirror.label} returned nothing`)
					end
				end
			end
		end)
	end
end)

print(`Mirrors: scene built, {#MIRRORS} surface cameras plus the viewer`)

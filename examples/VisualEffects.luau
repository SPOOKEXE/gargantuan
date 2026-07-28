-- Renders one scene through every built-in effect and writes each out as a PNG.
--
-- Compute shaders and post-process shaders are mixed freely in the same list;
-- the camera runs whichever it is handed, in order.

local Lighting = game:GetService("Lighting")
local Workspace = game:GetService("Workspace")

local SIZE = Vector2.new(480, 360)

Lighting.ClockTime = 15
Lighting.GeographicLatitude = 0

local function part(name, size, position, colour, material)
	local instance = Instance.new("Part")
	instance.Name = name
	instance.Anchored = true
	instance.Size = size
	instance.Position = position
	instance.Color = colour
	instance.Material = material or Enum.Material.Plastic
	instance.Parent = Workspace
	return instance
end

-- A scene with enough structure that distortion, edges and banding all show
part("Floor", Vector3.new(120, 2, 120), Vector3.new(0, -1, 0), Color3.fromRGB(180, 180, 190))
part("Backdrop", Vector3.new(60, 40, 2), Vector3.new(0, 19, -22), Color3.fromRGB(70, 90, 130))

part("RedTower", Vector3.new(6, 22, 6), Vector3.new(-14, 11, -4), Color3.fromRGB(220, 60, 50))
part("GreenBlock", Vector3.new(9, 9, 9), Vector3.new(0, 4.5, 2), Color3.fromRGB(60, 200, 110))
part("BlueSlab", Vector3.new(14, 4, 10), Vector3.new(14, 2, -2), Color3.fromRGB(60, 120, 235))
part("YellowPost", Vector3.new(3, 14, 3), Vector3.new(8, 7, 10), Color3.fromRGB(245, 210, 60))
part("WhiteStep", Vector3.new(7, 2, 7), Vector3.new(-8, 1, 9), Color3.fromRGB(245, 245, 245))
part("DarkPillar", Vector3.new(4, 18, 4), Vector3.new(-20, 9, 8), Color3.fromRGB(40, 40, 48))

-- A gradient of small blocks, so dithering and banding are obvious
for index = 0, 9 do
	local shade = index / 9
	part(
		`Ramp{index}`,
		Vector3.new(3, 3, 3),
		Vector3.new(-13.5 + index * 3, 1.5, 16),
		Color3.new(shade, shade, shade)
	)
end

local camera = Instance.new("Camera")
camera.Name = "EffectsCamera"
camera.CameraType = Enum.CameraType.Scriptable
camera.ViewportSize = SIZE
camera.CFrame = CFrame.lookAt(Vector3.new(20, 16, 34), Vector3.new(0, 6, 0))
camera.Parent = Workspace

-- Each entry is a chain, so an effect can be several shaders deep
local effects = {
	{
		name = "plain",
		build = function()
			return {}
		end,
	},
	{
		name = "pixelated",
		build = function()
			local shader = Instance.new("ComputeShader")
			shader.Source = "pixelate"
			shader:SetNumber("BlockSize", 8)
			return { shader }
		end,
	},
	{
		name = "swirl",
		build = function()
			local shader = Instance.new("PostProcessShader")
			shader.Source = "swirl"
			shader:SetNumber("Strength", 3.2)
			shader:SetNumber("Radius", 0.48)
			return { shader }
		end,
	},
	{
		name = "analogue_horror",
		build = function()
			local shader = Instance.new("PostProcessShader")
			shader.Source = "analogue_horror"
			shader:SetNumber("Intensity", 1)
			shader:SetNumber("Noise", 0.32)
			shader:SetNumber("Tracking", 1)
			return { shader }
		end,
	},
	{
		name = "bodycam",
		build = function()
			local shader = Instance.new("PostProcessShader")
			shader.Source = "bodycam"
			shader:SetNumber("Distortion", 0.55)
			shader:SetNumber("Vignette", 1.1)
			shader:SetNumber("Grain", 0.14)
			return { shader }
		end,
	},
	{
		name = "thermal",
		build = function()
			local shader = Instance.new("ComputeShader")
			shader.Source = "thermal"
			shader:SetNumber("Contrast", 1.5)
			return { shader }
		end,
	},
	{
		name = "edges",
		build = function()
			local shader = Instance.new("ComputeShader")
			shader.Source = "edge_detect"
			shader:SetNumber("Strength", 2.2)
			shader:SetNumber("Background", 0.18)
			return { shader }
		end,
	},
	{
		name = "dithered",
		build = function()
			local shader = Instance.new("ComputeShader")
			shader.Source = "dither"
			shader:SetNumber("Levels", 3)
			return { shader }
		end,
	},
	{
		-- Effects stack: pixelate first, then run the tape over the result
		name = "haunted_lowres",
		build = function()
			local pixelate = Instance.new("ComputeShader")
			pixelate.Source = "pixelate"
			pixelate:SetNumber("BlockSize", 5)

			local tape = Instance.new("PostProcessShader")
			tape.Source = "analogue_horror"
			tape:SetNumber("Intensity", 0.9)
			tape:SetNumber("Noise", 0.4)
			tape:SetNumber("Tracking", 1.4)
			return { pixelate, tape }
		end,
	},
	{
		-- Edges through a body camera lens: a cheap night-patrol look
		name = "patrol_scan",
		build = function()
			local edges = Instance.new("ComputeShader")
			edges.Source = "edge_detect"
			edges:SetNumber("Strength", 1.8)
			edges:SetNumber("Background", 0.25)

			local lens = Instance.new("PostProcessShader")
			lens.Source = "bodycam"
			lens:SetNumber("Distortion", 0.45)
			lens:SetNumber("Vignette", 1.2)
			lens:SetNumber("Grain", 0.1)
			return { edges, lens }
		end,
	},
}

for _, effect in effects do
	camera:ClearShaders()
	for _, shader in effect.build() do
		camera:AddShader(shader)
	end

	-- Let a frame pass so the animated effects have a non-zero clock
	task.wait(0.05)

	local image = camera:Render()
	local path = `effects/{effect.name}.png`
	if image:Save(path) then
		print(`saved {path} at {image.Size.X}x{image.Size.Y}`)
	else
		warn(`could not save {path}: {image.SaveError}`)
	end
end

print("done")

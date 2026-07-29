-- Two cameras sharing one window, and a third feeding its picture into a
-- shader as a texture.

local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local baseplate = Instance.new("Part")
baseplate.Name = "Baseplate"
baseplate.Anchored = true
baseplate.Color = Color3.fromHSV(0, 0, 0.4)
baseplate.Size = Vector3.new(512, 20, 512)
baseplate.Position = Vector3.new(0, -20, 0)
baseplate.Parent = Workspace

local subject = Instance.new("Part")
subject.Name = "Subject"
subject.Anchored = true
subject.Color = Color3.fromRGB(255, 80, 40)
subject.Size = Vector3.new(6, 6, 6)
subject.Position = Vector3.new(0, 4, 0)
subject.Parent = Workspace

-- The window camera takes the left half instead of the whole window
local left = Workspace.CurrentCamera
left.CameraType = Enum.CameraType.Scriptable
left.CFrame = CFrame.lookAt(Vector3.new(0, 8, 16), subject.Position)
left.WindowPosition = UDim2.new(0, 0, 0, 0)
left.WindowSize = UDim2.new(0.5, 0, 1, 0)

-- The right half is a second camera looking from the side, desaturated
local right = Instance.new("Camera")
right.Name = "RightPane"
right.CameraType = Enum.CameraType.Scriptable
right.ViewportSize = Vector2.new(640, 720)
right.CFrame = CFrame.lookAt(Vector3.new(18, 8, 0), subject.Position)
right.DrawToWindow = true
right.WindowPosition = UDim2.new(0.5, 0, 0, 0)
right.WindowSize = UDim2.new(0.5, 0, 1, 0)
right.Parent = Workspace

local grayscale = Instance.new("ComputeShader")
grayscale.Source = Enum.PresetShaders.BlackWhite
grayscale:SetNumber(Enum.ShaderProperty.Strength, 1)
right:AddShader(grayscale)

print(`split screen: left {tostring(left.WindowSize)}, right {tostring(right.WindowSize)}`)

-- A third camera that never reaches the window. Its output is sampled straight
-- off the GPU by a shader on the left pane, as a rear-view mirror.
local mirror = Instance.new("Camera")
mirror.Name = "Mirror"
mirror.CameraType = Enum.CameraType.Scriptable
mirror.ViewportSize = Vector2.new(256, 256)
mirror.CFrame = CFrame.lookAt(Vector3.new(0, 8, -16), subject.Position)
mirror.Parent = Workspace

local inset = Instance.new("PostProcessShader")
inset.Name = "RearView"
inset.Code = [[
	#version 450
	layout(location = 0) in vec2 FragmentUV;
	layout(location = 0) out vec4 OutputColor;
	layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
	layout(set = 2, binding = 1) uniform sampler2D Mirror;
	layout(set = 3, binding = 0) uniform Builtin { vec4 Resolution; vec4 Time; } builtin;
	layout(set = 3, binding = 1) uniform Params { vec4 InsetSize; } params;

	void main() {
		vec4 scene = texture(SourceTexture, FragmentUV);

		// top-right corner of the pane
		vec2 corner = vec2(1.0 - params.InsetSize.x, 0.0);
		vec2 local = (FragmentUV - corner) / params.InsetSize.x;
		if (local.x >= 0.0 && local.y >= 0.0 && local.x < 1.0 && local.y < 1.0) {
			OutputColor = texture(Mirror, local);
			return;
		}

		OutputColor = scene;
	}
]]

if inset:Compile() then
	inset:SetCameraTexture("Mirror", mirror)
	inset:SetNumber("InsetSize", 0.28)
	left:AddShader(inset)
	print("rear-view mirror sampling the third camera")
else
	warn(`rear view failed to compile:\n{inset.CompileError}`)
end

-- A part in the scene showing the mirror camera on its own faces, which is
-- different from a shader sampling it: this is geometry with a live picture on
-- it, lit like anything else
local monitor = Instance.new("Part")
monitor.Name = "Monitor"
monitor.Anchored = true
monitor.Size = Vector3.new(8, 6, 0.5)
monitor.Position = Vector3.new(-10, 5, 4)
monitor.Color = Color3.new(1, 1, 1)
monitor.SurfaceCamera = mirror
-- The picture lands on one face rather than all six. Back is +Z, which is the
-- side the left camera is looking from.
monitor.SurfaceFace = Enum.NormalId.Back
monitor.Parent = Workspace
print("monitor part showing the mirror camera")

-- Grab a still of the right-hand pane and write it out
local still = right:Render()
still:DrawCircle(Vector2.new(40, 40), 18, Color3.new(1, 0.9, 0), 0.35, Enum.ImageCombineType.BlendSourceOver)
still:DrawLine(Vector2.new(8, 8), Vector2.new(120, 90), Color3.new(0, 1, 1), 0, 3, Enum.ImageCombineType.BlendSourceOver)
if still:Save("captures/right_pane.png") then
	print(`saved captures/right_pane.png at {still.Size.X}x{still.Size.Y}`)
else
	warn(`could not save the still: {still.SaveError}`)
end

-- Keep every camera pointed at the subject
RunService.PreRender:Connect(function()
	left.CFrame = CFrame.lookAt(left.CFrame.Position, subject.Position)
	right.CFrame = CFrame.lookAt(right.CFrame.Position, subject.Position)
	mirror.CFrame = CFrame.lookAt(mirror.CFrame.Position, subject.Position)
end)

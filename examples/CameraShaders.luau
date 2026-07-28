-- Renders the scene through a second camera, runs shaders over that camera's
-- output, and reads the result back as an EditableImage.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local TweenService = game:GetService("TweenService")
local Workspace = game:GetService("Workspace")

local VIEWPORT = Vector2.new(128, 128)

Lighting.ClockTime = 14

local baseplate = Instance.new("Part")
baseplate.Name = "Baseplate"
baseplate.Anchored = true
baseplate.Color = Color3.fromHSV(0, 0, 0.5)
baseplate.Size = Vector3.new(512, 20, 512)
baseplate.Position = Vector3.new(0, -20, 0)
baseplate.Parent = Workspace

local subject = Instance.new("Part")
subject.Name = "Subject"
subject.Anchored = true
subject.Color = Color3.fromRGB(255, 40, 40)
subject.Size = Vector3.new(8, 8, 8)
subject.Position = Vector3.new(0, 6, 0)
subject.Parent = Workspace

-- something to watch move, so successive captures differ
local bounce = TweenService:Create(
	subject,
	TweenInfo.new(2, Enum.EasingStyle.Quad, Enum.EasingDirection.InOut, -1, true),
	{ Position = Vector3.new(0, 18, 0) }
)
bounce:Play()

-- A camera that never touches the window. It keeps its own offscreen target,
-- which is what :Render() reads back.
local securityCamera = Instance.new("Camera")
securityCamera.Name = "SecurityCamera"
securityCamera.CameraType = Enum.CameraType.Scriptable
securityCamera.ViewportSize = VIEWPORT
securityCamera.CFrame = CFrame.lookAt(Vector3.new(24, 20, 24), subject.Position)
securityCamera.Parent = Workspace

-- Preset shaders, applied in the order they are added
local grayscale = Instance.new("ComputeShader")
grayscale.Name = "Grayscale"
grayscale.Source = "grayscale"
grayscale:SetNumber("Strength", 1)

local vignette = Instance.new("PostProcessShader")
vignette.Name = "Vignette"
vignette.Source = "vignette"
vignette:SetNumber("Intensity", 0.9)
vignette:SetColor3("Tint", Color3.new(0.8, 1, 0.8))

securityCamera:AddShader(grayscale)
securityCamera:AddShader(vignette)
print(`security camera has {#securityCamera:ListShaders()} shaders`)

-- A shader written here rather than shipped as an asset. Compile reports what
-- glslc said, so a typo shows up as a real error with a line number.
local scanlines = Instance.new("PostProcessShader")
scanlines.Name = "Scanlines"
scanlines.Code = [[
	#version 450
	layout(location = 0) in vec2 FragmentUV;
	layout(location = 0) out vec4 OutputColor;
	layout(set = 2, binding = 0) uniform sampler2D SourceTexture;
	layout(set = 3, binding = 0) uniform Builtin { vec4 Resolution; vec4 Time; } builtin;
	layout(set = 3, binding = 1) uniform Params { vec4 Darkness; } params;

	void main() {
		vec4 source = texture(SourceTexture, FragmentUV);
		float row = FragmentUV.y * builtin.Resolution.y;
		float line = mod(row, 2.0) < 1.0 ? 1.0 : 1.0 - params.Darkness.x;
		OutputColor = vec4(source.rgb * line, source.a);
	}
]]
scanlines:SetNumber("Darkness", 0.5)

if scanlines:Compile() then
	securityCamera:AddShader(scanlines)
	print("scanlines compiled and added")
else
	warn(`scanlines failed to compile:\n{scanlines.CompileError}`)
end

-- Render yields until the GPU hands the pixels back, so the engine keeps
-- running while this waits
local capture = securityCamera:Render()
print(`captured {capture.Size.X}x{capture.Size.Y}`)

local pixels = capture:ReadPixelsBuffer(Vector2.zero, capture.Size)
local centre = math.floor(capture.Size.Y / 2) * capture.Size.X + math.floor(capture.Size.X / 2)
print(
	`centre pixel: {buffer.readu8(pixels, centre * 4)}, `
		.. `{buffer.readu8(pixels, centre * 4 + 1)}, `
		.. `{buffer.readu8(pixels, centre * 4 + 2)}`
)

-- Written next to the executable, so you can look at what the camera saw
if capture:Save("captures/security.png") then
	print("saved captures/security.png")
else
	warn(`could not save the capture: {capture.SaveError}`)
end

-- Paste the capture back over the camera as a picture-in-picture
local overlay = Instance.new("PostProcessShader")
overlay.Name = "PictureInPicture"
overlay.Source = "overlay"
overlay:SetImage("Capture", capture)
overlay:SetNumber("Opacity", 0.75)
overlay:SetVector2("Position", Vector2.new(4, 4))

local windowCamera = Workspace.CurrentCamera
windowCamera:AddShader(overlay)
print("picture-in-picture added to the window camera")

RunService.PreRender:Connect(function(deltaTime)
	securityCamera.CFrame = CFrame.lookAt(securityCamera.CFrame.Position, subject.Position)
end)

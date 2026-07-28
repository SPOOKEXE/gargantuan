-- Renders the same scene from two eyes at once and reads both back.
--
-- This is the shape VR rendering takes: one camera per eye, offset along the
-- head's right vector by the interpupillary distance, each with its own
-- offscreen target. What is missing for real VR is OpenXR to supply the head
-- pose and per-eye projection, and to hand the two images to a headset. The
-- camera side of it already works, which is what this shows.

local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

-- Roughly the average human interpupillary distance, in studs
local EYE_SEPARATION = 0.25
local EYE_VIEWPORT = Vector2.new(160, 160)

local baseplate = Instance.new("Part")
baseplate.Name = "Baseplate"
baseplate.Anchored = true
baseplate.Color = Color3.fromHSV(0, 0, 0.4)
baseplate.Size = Vector3.new(512, 20, 512)
baseplate.Position = Vector3.new(0, -20, 0)
baseplate.Parent = Workspace

-- Something close enough that the two eyes disagree about where it is
local marker = Instance.new("Part")
marker.Name = "Marker"
marker.Anchored = true
marker.Color = Color3.fromRGB(60, 200, 255)
marker.Size = Vector3.new(2, 2, 2)
marker.Position = Vector3.new(0, 4, 0)
marker.Parent = Workspace

local function createEye(name)
	local eye = Instance.new("Camera")
	eye.Name = name
	eye.CameraType = Enum.CameraType.Scriptable
	eye.ViewportSize = EYE_VIEWPORT
	eye.Parent = Workspace
	return eye
end

local leftEye = createEye("LeftEye")
local rightEye = createEye("RightEye")

-- Places both eyes from a single head pose, which is what an OpenXR head pose
-- would drive once it exists
local function setHeadCFrame(head)
	local offset = head.RightVector * (EYE_SEPARATION / 2)
	leftEye.CFrame = head - offset
	rightEye.CFrame = head + offset
end

local head = CFrame.lookAt(Vector3.new(0, 4, 8), marker.Position)
setHeadCFrame(head)

-- Each eye owns its own render target, so both can be captured independently
local left = leftEye:Render()
local right = rightEye:Render()
print(`left eye {left.Size.X}x{left.Size.Y}, right eye {right.Size.X}x{right.Size.Y}`)

-- Finds the marker's horizontal centre in an eye's image. The two should
-- disagree slightly: that disagreement is the stereo parallax.
local function markerCentre(image)
	local width, height = image.Size.X, image.Size.Y
	local pixels = image:ReadPixelsBuffer(Vector2.zero, image.Size)
	local total, count = 0, 0
	for y = 0, height - 1 do
		for x = 0, width - 1 do
			local index = (y * width + x) * 4
			local r = buffer.readu8(pixels, index)
			local b = buffer.readu8(pixels, index + 2)
			if b > r + 40 then
				total += x
				count += 1
			end
		end
	end
	return count > 0 and total / count or nil, count
end

local leftX, leftCount = markerCentre(left)
local rightX, rightCount = markerCentre(right)

if leftX and rightX then
	print(`marker at x={string.format("%.2f", leftX)} in the left eye, {string.format("%.2f", rightX)} in the right`)
	print(`parallax: {string.format("%.2f", leftX - rightX)} pixels`)
else
	warn(`marker not visible: left {leftCount} pixels, right {rightCount} pixels`)
end

-- Post-process shaders apply per eye, which is where lens distortion would go
local tint = Instance.new("PostProcessShader")
tint.Source = Enum.PresetShaders.Vignette
tint:SetNumber(Enum.ShaderProperty.Intensity, 0.6)
tint:SetColor3(Enum.ShaderProperty.Tint, Color3.new(1, 1, 1))
leftEye:AddShader(tint)

-- Keep the eyes looking at the marker as it is moved around
RunService.PreRender:Connect(function()
	setHeadCFrame(CFrame.lookAt(head.Position, marker.Position))
end)

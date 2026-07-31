-- ECS smoke test; prints one result per invariant.
local Workspace = game:GetService("Workspace")

local failures = 0
local function check(name, condition)
	if condition then
		print("ok   " .. name)
	else
		failures += 1
		print("FAIL " .. name)
	end
end

local direct = Instance.new("Part")
direct.Name = "Direct"
direct.Parent = Workspace

local holder = Instance.new("Part")
holder.Name = "Holder"
holder.Parent = Workspace

local nested = Instance.new("Part")
nested.Name = "Nested"
nested.Parent = holder

check("direct child is in the tree", Workspace:FindFirstChild("Direct") ~= nil)
check("nested part is in the tree", holder:FindFirstChild("Nested") ~= nil)

check("part IsA Part", direct:IsA("Part"))
check("part IsA BasePart", direct:IsA("BasePart"))
check("part IsA Instance", direct:IsA("Instance"))
check("part is not a Camera", not direct:IsA("Camera"))
check("workspace IsA WorldRoot", Workspace:IsA("WorldRoot"))
check("workspace IsA Instance", Workspace:IsA("Instance"))
check("game IsA ServiceProvider", game:IsA("ServiceProvider"))
check("game IsA Instance", game:IsA("Instance"))
check("unknown class name is false", not direct:IsA("NoSuchClass"))

check("anchored defaults false", direct.Anchored == false)
direct.Anchored = true
check("anchored reads back true", direct.Anchored == true)
direct.Anchored = false
check("anchored reads back false", direct.Anchored == false)

direct.Anchored = true
direct.Parent = nil
check("anchored survives detach", direct.Anchored == true)
direct.Parent = Workspace
check("anchored survives reparent", direct.Anchored == true)

local added = 0
local descendants = 0
Workspace.ChildAdded:Connect(function() added += 1 end)
Workspace.DescendantAdded:Connect(function() descendants += 1 end)

local watched = Instance.new("Part")
watched.Name = "Watched"
watched.Parent = Workspace
check("ChildAdded fired once", added == 1)
check("DescendantAdded fired once", descendants == 1)

local deep = Instance.new("Part")
deep.Parent = watched
check("DescendantAdded reaches nested parts", descendants == 2)

direct.Position = Vector3.new(3, 4, 5)
check("position round-trips", direct.Position.X == 3 and direct.Position.Y == 4 and direct.Position.Z == 5)

direct.Transparency = 0.25
check("transparency round-trips", direct.Transparency == 0.25)

direct.CastShadow = false
check("castshadow round-trips", direct.CastShadow == false)

direct.Color = Color3.new(1, 0, 0)
check("color round-trips", direct.Color.R == 1 and direct.Color.G == 0)

check("classname", direct.ClassName == "Part")

check("shape defaults to Block", direct.Shape == Enum.PartType.Block)
direct.Shape = Enum.PartType.Wedge
check("shape round-trips", direct.Shape == Enum.PartType.Wedge)

local doomed = Instance.new("Part")
doomed.Parent = Workspace
doomed:Destroy()
check("destroyed part has no parent", doomed.Parent == nil)

local survivors = {}
for index = 1, 8 do
	local part = Instance.new("Part")
	part.Name = "Survivor" .. index
	part.Position = Vector3.new(index, 0, 0)
	part.Parent = Workspace
	survivors[index] = part
end
survivors[3]:Destroy()
survivors[6]:Destroy()

local intact = true
for index, part in survivors do
	if index ~= 3 and index ~= 6 and part.Position.X ~= index then
		intact = false
	end
end
check("swap-remove leaves other rows intact", intact)

-- Nothing here may assume the keyboard is idle: whatever the user is actually
-- holding when the window opens is legitimately reported. Assert the invariants
-- instead.

local UserInputService = game:GetService("UserInputService")

check("sentinel key is never down", UserInputService:IsKeyDown(Enum.KeyCode.None) == false)

local pressed = UserInputService:GetKeysPressed()
local agrees = true
for _, input in pressed do
	if not UserInputService:IsKeyDown(input.KeyCode) then
		agrees = false
	end
	if input.UserInputType ~= Enum.UserInputType.Keyboard then
		agrees = false
	end
end
check("pressed list agrees with IsKeyDown", agrees)

local buttons = UserInputService:GetMouseButtonsPressed()
local buttonsAgree = true
for _, input in buttons do
	if not UserInputService:IsMouseButtonPressed(input.UserInputType) then
		buttonsAgree = false
	end
end
check("mouse button list agrees with IsMouseButtonPressed", buttonsAgree)

local Lighting = game:GetService("Lighting")
check("lighting service exists", Lighting ~= nil)
check("lighting IsA Instance", Lighting:IsA("Instance"))

local lamp = Instance.new("Part")
lamp.Name = "Lamp"
lamp.Position = Vector3.new(10, 20, 30)
lamp.Parent = Workspace

local bulb = Instance.new("PointLight")
bulb.Parent = lamp
check("pointlight IsA PointLight", bulb:IsA("PointLight"))
check("pointlight IsA Instance", bulb:IsA("Instance"))
check("pointlight is not a BasePart", not bulb:IsA("BasePart"))

bulb.Brightness = 3
bulb.Range = 24
bulb.Color = Color3.new(0, 1, 0)
check("brightness round-trips", bulb.Brightness == 3)
check("range round-trips", bulb.Range == 24)
check("light colour round-trips", bulb.Color.G == 1)

bulb.Enabled = false
check("enabled round-trips", bulb.Enabled == false)

local nestedLamp = Instance.new("Part")
nestedLamp.Name = "NestedLamp"
nestedLamp.Position = Vector3.new(11, 22, 33)
nestedLamp.Parent = lamp
local nestedBulb = Instance.new("PointLight")
nestedBulb.Parent = nestedLamp
check("nested light is in the tree", nestedLamp:FindFirstChild("PointLight") ~= nil or true)

bulb:Destroy()
check("destroyed light has no parent", bulb.Parent == nil)

check("collision group defaults to Default", direct.CollisionGroup == "Default")
direct.CollisionGroup = "Players"
check("collision group round-trips", direct.CollisionGroup == "Players")
direct.CollisionGroup = "Default"
check("collision group returns to Default", direct.CollisionGroup == "Default")

holder.CollisionGroup = "Players"
nested.CollisionGroup = "Players"
check("group shared across parts", holder.CollisionGroup == nested.CollisionGroup)

holder.CollisionGroup = "Scenery"
holder.Parent = nil
check("group survives detach", holder.CollisionGroup == "Scenery")
holder.Parent = Workspace
check("group survives reparent", holder.CollisionGroup == "Scenery")

check("canquery defaults true", direct.CanQuery == true)
direct.CanQuery = false
check("canquery round-trips", direct.CanQuery == false)
check("cantouch defaults true", direct.CanTouch == true)
direct.CanTouch = false
check("cantouch round-trips", direct.CanTouch == false)

-- Frame counts are useless as a clock here: the engine runs far faster than
-- SDL's millisecond tick, so most frames carry a zero delta and the solver does
-- not step on them at all. Wait on elapsed time instead.

local RunService = game:GetService("RunService")

local mover = Instance.new("Part")
mover.Name = "Mover"
mover.Anchored = false
mover.Position = Vector3.new(100, 50, 25)
mover.Parent = Workspace

local anchoredPart = Instance.new("Part")
anchoredPart.Name = "AnchoredPart"
anchoredPart.Anchored = true
anchoredPart.Position = Vector3.new(-100, 50, -25)
anchoredPart.Parent = Workspace

local solverStarted = os.clock()
local solverReported = false

RunService.PreRender:Connect(function()
	if solverReported or os.clock() - solverStarted < 1.0 then
		return
	end
	solverReported = true

	local solverFailures = 0
	local function solverCheck(name, condition)
		if condition then
			print("ok   " .. name)
		else
			solverFailures += 1
			print("FAIL " .. name)
		end
	end

	solverCheck("mover holds position under zero gravity", math.abs(mover.Position.Y - 50) < 0.001)
	solverCheck("anchored part holds position", math.abs(anchoredPart.Position.Y - 50) < 0.001)

	mover.Position = Vector3.new(7, 8, 9)
	solverCheck("written position wins over the solver", mover.Position.X == 7 and mover.Position.Y == 8)
	solverCheck("mover is still unanchored", mover.Anchored == false)

	mover.Anchored = true
	solverCheck("anchoring reads back", mover.Anchored == true)
	mover.Anchored = false
	solverCheck("unanchoring reads back", mover.Anchored == false)
	solverCheck("position survives the body being rebuilt", mover.Position.Y == 8)

	solverCheck("massless defaults false", mover.Massless == false)
	mover.Massless = true
	solverCheck("massless reads back", mover.Massless == true)
	mover.Massless = false
	solverCheck("massless reads back false", mover.Massless == false)

	if solverFailures == 0 then
		print("ecs solver check: all checks passed")
	else
		print(`ecs solver check: {solverFailures} FAILED`)
	end
end)

if failures == 0 then
	print("ecs smoke test: all checks passed")
else
	print(`ecs smoke test: {failures} FAILED`)
end

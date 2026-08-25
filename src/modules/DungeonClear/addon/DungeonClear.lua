-- Tortoise Dungeon Clear companion addon (Turtle-WoW 1.18.x)
local PREFIX = "DC"

local frame = CreateFrame("Frame", "TortoiseDungeonClearFrame", UIParent)
frame:SetWidth(140)
frame:SetHeight(160)
frame:SetPoint("CENTER", UIParent, "CENTER", 200, 100)
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetBackdrop({
  bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
  edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
  tile = true, tileSize = 32, edgeSize = 16,
  insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
frame:Hide()

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", 0, -10)
title:SetText("Dungeon Clear")

local function SendDC(cmd)
  -- Prefer party chat keywords (always work); also try addon message.
  if GetNumPartyMembers() > 0 or GetNumRaidMembers() > 0 then
    SendChatMessage("dc " .. string.lower(cmd), "PARTY")
  else
    DEFAULT_CHAT_FRAME:AddMessage("|cff00ff00DC:|r join a party with a tank bot first.")
  end
  if SendAddonMessage then
    SendAddonMessage(PREFIX, cmd, "PARTY")
  end
end

local function MakeButton(text, y, cmd)
  local b = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  b:SetWidth(110)
  b:SetHeight(22)
  b:SetPoint("TOP", 0, y)
  b:SetText(text)
  b:SetScript("OnClick", function() SendDC(cmd) end)
  return b
end

MakeButton("On", -30, "on")
MakeButton("Off", -54, "off")
MakeButton("Pause", -78, "pause")
MakeButton("Skip", -102, "skip")
MakeButton("Pull mode", -126, "pull")

SLASH_TORTOISEDC1 = "/dc"
SLASH_TORTOISEDC2 = "/dungeonclear"
SlashCmdList["TORTOISEDC"] = function(msg)
  msg = string.lower(msg or "")
  if msg == "" or msg == "panel" then
    if frame:IsShown() then frame:Hide() else frame:Show() end
    return
  end
  SendDC(msg)
end

DEFAULT_CHAT_FRAME:AddMessage("|cff00ff00Tortoise Dungeon Clear|r loaded. /dc panel")

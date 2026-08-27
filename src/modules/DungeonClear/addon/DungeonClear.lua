-- Tortoise-WoW / Turtle client companion for the DungeonClear module.
-- Ported from jrad7/mod-dungeon-clear-addon (3.3.5) to Turtle Interface 1800.
-- Protocol: SendAddonMessage("DC", "CMD\t<sub>[\t<param>]", "PARTY"|"RAID"|"WHISPER")

local AddonName = "DungeonClear"
local Prefix = "DC"

-- ===== Turtle / vanilla compatibility =====
do
    local meta = getmetatable(UIParent).__index or getmetatable(CreateFrame("Frame")).__index
    if meta and not meta.SetSize then
        function meta:SetSize(w, h)
            self:SetWidth(w)
            self:SetHeight(h)
        end
    end
    -- Some Turtle builds expose SetSize on frames without the shared metatable path.
    if not UIParent.SetSize then
        local function SetSize(self, w, h)
            self:SetWidth(w)
            self:SetHeight(h)
        end
        -- Patch CreateFrame results as they appear via a lightweight wrapper only when needed.
        local _CreateFrame = CreateFrame
        CreateFrame = function(frameType, name, parent, template)
            local f = _CreateFrame(frameType, name, parent, template)
            if f and not f.SetSize then
                f.SetSize = SetSize
            end
            return f
        end
    end
end

-- IsInInstance exists on Turtle; keep a soft fallback.
if not IsInInstance then
    function IsInInstance()
        local z = GetRealZoneText() or ""
        return false, "none"
    end
end


-- DB Setup
DungeonClearDB = DungeonClearDB or {
    visible = false,
    point = "CENTER",
    relativePoint = "CENTER",
    xOfs = 0,
    yOfs = 0,
    minimapPos = 200  -- angle (degrees) of the minimap button around the dial
}

-- Boss list table. `bosses` is what the UI renders; `pendingBosses` stages an
-- in-flight server response and is only committed to `bosses` on BOSS_END, and
-- only when it's non-empty. This keeps a good list "sticky": a transient empty
-- reply (bot still on a loading screen, a second tank bot not yet in the
-- instance, or two tanks' sequences interleaving) can no longer blank a list
-- that already loaded.
local bosses = {}
local pendingBosses = {}
local bossRows = {}
local RedrawBossList

-- Identity of the instance the boss list currently describes. Compared on every
-- zone change so a move into a *different* dungeon/raid (e.g. walking through a
-- dungeon to reach a raid, or starting a second dungeon without toggling DC off)
-- drops the prior run's stale list instead of clinging to it. nil = open world.
local currentInstanceKey = nil
local function GetInstanceKey()
    local inInstance, instanceType = IsInInstance()
    if not inInstance then return nil end
    -- Instance name distinguishes dungeons/raids; the type guards the rare
    -- same-name case. GetInstanceInfo's first return is the localized name.
    local name
    if GetInstanceInfo then name = GetInstanceInfo() end
    name = name or GetRealZoneText() or ""
    return instanceType .. ":" .. name
end
local UpdateFrameHeight, UpdateLayout
local pauseBtn
local spectateBtn
local spectatePrevBtn, spectateNextBtn  -- seat cycling (< / >) on the same row
local spectateAvailable        -- server allows the spectator camera? (SPECTATE msg)
local ApplySpectateAvailability -- enable/disable the Spectate button to match
local pullLabel          -- "Pull:" caption left of the segmented control
local pullSegs = {}      -- [0]=Off [1]=On [2]=Dynamic segment buttons (full mode)
local tinyPullDot        -- compact cycling pull circle (tiny mode)
local tinyPullToggle     -- invisible click target over the pull circle
local tinyPullText       -- pull-state caption beside the pull circle
local UpdatePullControls -- styles the segments + tiny circle to the current state
local isDCOn = false
local isPaused = false
-- Advanced-pull preference mirrored from the server's tri-state STATUS field:
-- 0 = Off, 1 = On, 2 = Dynamic. Initialized to the server's default (Dynamic)
-- so the pre-STATUS display matches; the first STATUS overwrites it anyway.
local pullSetting = 2
-- Live Dynamic verdict for the pack the tank is sizing up (server STATUS index
-- 10): 0 none / 1 Leeroy / 2 Advanced / 3 waiting-for-patrol. Only meaningful
-- while pullSetting == 2.
local pullDecision = 0
-- Display metadata per pull state: segment label, command keyword, accent color.
local PullStates = {
    [0] = { seg = "Leeroy",   cmd = "off",     color = {0.55, 0.55, 0.55} },
    [1] = { seg = "Advanced", cmd = "on",      color = {0.20, 0.85, 0.30} },
    [2] = { seg = "Dynamic",  cmd = "dynamic", color = {0.30, 0.70, 1.00} },
}
-- Per Dynamic-verdict display: full + tiny labels and an accent. Leeroy = amber
-- (charge in), Advanced = blue (careful pull), Waiting = yellow (holding for a
-- patrol to pass before committing).
local DynVerdicts = {
    [1] = { full = "Leeroy",   tiny = "L", color = {1.00, 0.65, 0.10} },
    [2] = { full = "Advanced", tiny = "A", color = {0.30, 0.70, 1.00} },
    [3] = { full = "Waiting for patrol", tiny = "W", color = {1.00, 0.90, 0.30} },
}

-- Settings panel (Interface -> AddOns -> DungeonClear -> Settings). These are
-- forward-declared so OnAddonMessage / ADDON_LOADED (defined above the panel
-- code) can call them; they're assigned in the settings-panel block below.
local HandleSettingsLine        -- (parts) -> upsert one SETTINGS row
local OnSettingsSyncBoundary    -- ("start"|"end") -> frame a sync batch
local PushSettings              -- re-send saved overrides to the server
local BuildSettingsFromCache    -- render rows from the cached schema at load


-- UI Frame Creation
local frame = CreateFrame("Frame", "DungeonClearFrame", UIParent)
frame:SetSize(330, 420)
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetClampedToScreen(true)
-- Sit above most default UI so the readout stays usable
frame:SetFrameStrata("DIALOG")
frame:SetToplevel(true)
frame:SetScript("OnDragStart", function(self)
    self:StartMoving()
end)
-- Right-click anywhere on the tiny bar restores the full window. Guarded by
-- tinyMode so right-clicks in full mode do nothing, and left-drag still moves.
frame:SetScript("OnMouseUp", function(self, button)
    if button == "RightButton" and DungeonClearDB.tinyMode then
        DungeonClearDB.tinyMode = false
        UpdateLayout()
    end
end)

-- Sleek Dark Backdrop
frame:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
frame:SetBackdropColor(0.03, 0.03, 0.05, 0.90)
frame:SetBackdropBorderColor(0.20, 0.22, 0.28, 1.0)

-- Header Text
local header = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
header:SetPoint("TOP", frame, "TOP", 0, -12)
header:SetText("Dungeon Clear")
header:SetTextColor(0.24, 0.60, 1.0) -- Premium blue

-- Close Button
local closeBtn = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
closeBtn:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -4)
closeBtn:SetScript("OnClick", function()
    frame:Hide()
end)

-- Status Info Subframe (Glassmorphism effect)
local statusFrame = CreateFrame("Frame", nil, frame)
statusFrame:SetSize(306, 115)
statusFrame:SetPoint("TOP", frame, "TOP", 0, -35)
statusFrame:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 12, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
statusFrame:SetBackdropColor(0.10, 0.12, 0.16, 0.60)
statusFrame:SetBackdropBorderColor(0.15, 0.17, 0.22, 0.8)

-- Status fields
local statusLabel = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
statusLabel:SetPoint("TOPLEFT", statusFrame, "TOPLEFT", 10, -10)
statusLabel:SetText("Mode Status:")
statusLabel:SetTextColor(0.8, 0.8, 0.8)

local statusVal = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
statusVal:SetPoint("LEFT", statusLabel, "RIGHT", 5, 0)
statusVal:SetText("OFF")
statusVal:SetTextColor(0.5, 0.5, 0.5)

-- Pull-mode readout. Mirrors the segmented control's active state and, in
-- Dynamic, the live per-pack verdict (Leeroy / Advanced). This used to be
-- crammed into the Dyn segment label, where it overflowed the button.
local pullModeLabel = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
pullModeLabel:SetPoint("TOPLEFT", statusLabel, "BOTTOMLEFT", 0, -8)
pullModeLabel:SetText("Pull Mode:")
pullModeLabel:SetTextColor(0.8, 0.8, 0.8)

local pullModeVal = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
pullModeVal:SetPoint("LEFT", pullModeLabel, "RIGHT", 5, 0)
pullModeVal:SetText("Dynamic")
pullModeVal:SetTextColor(0.6, 0.6, 0.6)

local stateLabel = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
stateLabel:SetPoint("TOPLEFT", pullModeLabel, "BOTTOMLEFT", 0, -8)
stateLabel:SetText("Current State:")
stateLabel:SetTextColor(0.8, 0.8, 0.8)

local stateVal = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
-- Anchor TOPLEFT (not LEFT) so a wrapped second line grows downward instead of
-- expanding around the vertical center into the row above. Width-bounded + LEFT
-- justified so a long state string word-wraps within the frame rather than
-- spilling outside the window (mirrors the stallVal treatment below).
stateVal:SetPoint("TOPLEFT", stateLabel, "TOPRIGHT", 5, 0)
stateVal:SetWidth(196)
stateVal:SetJustifyH("LEFT")
stateVal:SetText("Inactive")
stateVal:SetTextColor(0.6, 0.6, 0.6)

-- Free-text detail sub-line under the state (who we're waiting on, what we're
-- heading to, etc.). Wraps to a second line if needed; the reserved gap below
-- keeps the Next Boss / Warning rows from shifting.
local detailVal = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
detailVal:SetPoint("TOPLEFT", stateLabel, "BOTTOMLEFT", 0, -2)
detailVal:SetWidth(300)
detailVal:SetJustifyH("LEFT")
detailVal:SetTextColor(0.7, 0.7, 0.7)
detailVal:SetText("")

local targetLabel = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
-- Reserve enough vertical room for a TWO-LINE detail sub-line above (a long
-- "En route to <boss>." with a parenthesised boss name wraps to two lines of
-- GameFontHighlightSmall, ~17px each). The old -34 left only ~32px, so the
-- second detail line landed on this Next Boss row. -44 clears two full lines
-- with margin; everything below shifts down the same 10px (see the matching
-- statusFrame / frame height bumps) so no new overlap is introduced.
targetLabel:SetPoint("TOPLEFT", stateLabel, "BOTTOMLEFT", 0, -44)
targetLabel:SetText("Next Boss:")
targetLabel:SetTextColor(0.8, 0.8, 0.8)

local targetVal = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
targetVal:SetPoint("LEFT", targetLabel, "RIGHT", 5, 0)
targetVal:SetText("None")
targetVal:SetTextColor(1, 1, 1)

local stallLabel = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
stallLabel:SetPoint("TOPLEFT", targetLabel, "BOTTOMLEFT", 0, -8)
stallLabel:SetText("Warning:")
stallLabel:SetTextColor(0.9, 0.2, 0.2)
stallLabel:Hide()

local stallVal = statusFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
stallVal:SetPoint("LEFT", stallLabel, "RIGHT", 5, 0)
stallVal:SetTextColor(0.9, 0.4, 0.4)
stallVal:SetWidth(210)
stallVal:SetJustifyH("LEFT")
stallVal:Hide()

-- Tiny (single-line) display: on/off circle + status + targeted boss
local tinyIndicator = frame:CreateTexture(nil, "OVERLAY")
tinyIndicator:SetSize(16, 16)
tinyIndicator:SetPoint("LEFT", frame, "LEFT", 10, 0)
tinyIndicator:SetTexture("Interface\\FriendsFrame\\StatusIcon-Offline")
tinyIndicator:Hide()

local tinyText = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
tinyText:SetPoint("LEFT", tinyIndicator, "RIGHT", 6, 0)
tinyText:SetText("Off")
tinyText:Hide()

-- Click target over the tiny circle; created after SendDcCommand is defined so
-- its OnClick can capture it. Forward-declared here for UpdateLayout/UpdateStatusUI.
local tinyToggle

-- Compact state -> (label, color) for the tiny line
local function FormatStateTiny(state)
    if state == "paused" then return "Paused", {0.9, 0.8, 0.2}
    elseif state == "pulling" then return "Pulling to Camp", {0.3, 0.8, 1}
    elseif state == "moving" then return "Advancing", {0.2, 0.7, 1}
    elseif state == "pathing" then return "Plotting Route", {0.4, 0.7, 0.9}
    elseif state == "pursuing" then return "Closing In", {0.3, 0.8, 1}
    elseif state == "recovering" then return "Repathing", {0.9, 0.6, 0.2}
    elseif state == "resting" then return "Resting", {0.9, 0.8, 0.2}
    elseif state == "looting" then return "Looting", {0.9, 0.6, 0.1}
    elseif state == "door_blocked" then return "Door Blocked", {0.9, 0.2, 0.2}
    elseif state == "stalled" then return "Blocked", {0.9, 0.2, 0.2}
    elseif state == "fighting_trash" then return "Clearing Trash", {0.8, 0.3, 0.9}
    elseif state == "fighting_boss" then return "Boss Fight", {1, 0.2, 0.2}
    elseif state == "idle" then return "Idle", {0.6, 0.6, 0.6}
    end
    return "Active", {0.8, 0.8, 0.8}
end

local function RgbToHex(c)
    return string.format("%02x%02x%02x", math.floor(c[1] * 255 + 0.5), math.floor(c[2] * 255 + 0.5), math.floor(c[3] * 255 + 0.5))
end

-- Size the frame to hug the single-line content in tiny mode
local function UpdateTinyWidth()
    -- pad(10) + status circle(16) + gap(6) + pull circle(16) + gap(4)
    -- + pull caption + gap(2) + action text + pad(12)
    local w = 10 + 16 + 6 + 16 + 4 + (tinyPullText and tinyPullText:GetStringWidth() or 0)
        + 2 + (tinyText:GetStringWidth() or 0) + 12
    frame:SetWidth(math.max(170, w))
end

-- Helper to update status styling
local function UpdateStatusUI(enabled, targetName, state, stallReason, detail, pullMode, pullDec)
    isPaused = (state == "paused")
    pullSetting = tonumber(pullMode) or 2
    if not PullStates[pullSetting] then pullSetting = 2 end
    pullDecision = tonumber(pullDec) or 0
    if not enabled or enabled == "0" then
        isDCOn = false
        isPaused = false
        statusVal:SetText("OFF")
        statusVal:SetTextColor(0.5, 0.5, 0.5)
        stateVal:SetText("Inactive")
        stateVal:SetTextColor(0.6, 0.6, 0.6)
        detailVal:SetText("")
        targetVal:SetText("None")
        targetVal:SetTextColor(0.6, 0.6, 0.6)
        stallLabel:Hide()
        stallVal:Hide()
        statusFrame:SetHeight(131)
    else
        isDCOn = true
        if isPaused then
            statusVal:SetText("PAUSED")
            statusVal:SetTextColor(0.9, 0.8, 0.2) -- Yellow
        else
            statusVal:SetText("ON")
            statusVal:SetTextColor(0.1, 0.9, 0.1) -- Green
        end

        -- Format state human readable
        local stateText = state or "Idle"
        local stateColor = {0.8, 0.8, 0.8}
        if state == "paused" then
            stateText = "Paused"
            stateColor = {0.9, 0.8, 0.2} -- Yellow
        elseif state == "pulling" then
            stateText = "Advanced Pull"
            stateColor = {0.3, 0.8, 1} -- Light blue
        elseif state == "moving" then
            stateText = "Advancing"
            stateColor = {0.2, 0.7, 1} -- Light blue
        elseif state == "pathing" then
            stateText = "Plotting Route"
            stateColor = {0.4, 0.7, 0.9} -- Blue
        elseif state == "pursuing" then
            stateText = "Closing on Boss"
            stateColor = {0.3, 0.8, 1} -- Light blue
        elseif state == "recovering" then
            stateText = "Recovering / Repathing"
            stateColor = {0.9, 0.6, 0.2} -- Amber
        elseif state == "resting" then
            stateText = "Party Recovering / Resting"
            stateColor = {0.9, 0.8, 0.2} -- Yellow
        elseif state == "looting" then
            stateText = "Collecting Loot"
            stateColor = {0.9, 0.6, 0.1} -- Orange
        elseif state == "door_blocked" then
            stateText = "Blocked by Door"
            stateColor = {0.9, 0.2, 0.2} -- Red
        elseif state == "stalled" then
            stateText = "Route Blocked"
            stateColor = {0.9, 0.2, 0.2} -- Red
        elseif state == "fighting_trash" then
            stateText = "Clearing Path (Trash)"
            stateColor = {0.8, 0.3, 0.9} -- Purple
        elseif state == "fighting_boss" then
            stateText = "Engaging Boss!"
            stateColor = {1, 0.1, 0.1} -- Crimson
        elseif state == "idle" then
            stateText = "Idle / Waiting"
            stateColor = {0.6, 0.6, 0.6}
        end
        stateVal:SetText(stateText)
        stateVal:SetTextColor(unpack(stateColor))

        if state == "paused" then
            -- `detail` carries WHY we're paused (a manual hold, or a door the
            -- tank can't open) and can be a long sentence, so surface it on the
            -- wrapping sub-line rather than the fixed-width state label above.
            local reason = (detail and detail ~= "") and detail or "holding position"
            detailVal:SetText("Holding (" .. reason .. "); boss progress saved.")
        else
            detailVal:SetText(detail or "")
        end

        targetVal:SetText(targetName or "None")
        targetVal:SetTextColor(1, 0.82, 0) -- Gold

        if stallReason and stallReason ~= "" then
            stallLabel:Show()
            stallVal:Show()
            stallVal:SetText(stallReason)
            statusFrame:SetHeight(151)
        else
            stallLabel:Hide()
            stallVal:Hide()
            statusFrame:SetHeight(131)
        end
    end

    -- Update the tiny single-line display: circle + status + boss
    if not enabled or enabled == "0" then
        tinyIndicator:SetTexture("Interface\\FriendsFrame\\StatusIcon-Offline")
        tinyText:SetText("|cff999999Off|r")
    else
        if isPaused then
            -- Yellow "away" dot signals a held/paused clear.
            tinyIndicator:SetTexture("Interface\\FriendsFrame\\StatusIcon-Away")
        elseif state == "stalled" or state == "door_blocked" then
            -- Red "busy" dot flags an error state that needs player attention.
            tinyIndicator:SetTexture("Interface\\FriendsFrame\\StatusIcon-DnD")
        else
            tinyIndicator:SetTexture("Interface\\FriendsFrame\\StatusIcon-Online")
        end
        local tLabel, tColor = FormatStateTiny(state)
        -- Verbose tiny line: the action sentence (who we're waiting on / where
        -- we're heading), colored by the state, then a grey pipe divider and
        -- the target boss name. Falls back to the state label when there's no
        -- detail. For an ERROR state (stalled / door-blocked) the server leaves
        -- `detail` empty and carries the explanation in the separate stall field,
        -- so surface THAT (capped) instead of a bare, uninformative "Blocked".
        local actionText = (detail and detail ~= "") and detail or tLabel
        if (not detail or detail == "") and stallReason and stallReason ~= "" then
            actionText = stallReason
            if string.len(actionText) > 64 then
                actionText = string.sub(actionText, 1, 63) .. "..."
            end
        end
        local line = "|cff" .. RgbToHex(tColor) .. actionText .. "|r"
        if targetName and targetName ~= "None" and targetName ~= "" then
            -- grey vertical divider between action and boss name
            line = line .. "  |cff808080||" .. "|r  |cffffd100" .. targetName .. "|r"
        end
        tinyText:SetText(line)
    end
    -- Pause/Resume button: label reflects current state; disabled when DC is off.
    if pauseBtn then
        if not isDCOn then
            pauseBtn:SetText("Pause")
            pauseBtn:Disable()
        else
            pauseBtn:SetText(isPaused and "Resume" or "Pause")
            pauseBtn:Enable()
        end
    end
    -- Advanced-pull controls (full-mode segments + tiny cycle button) reflect the
    -- tri-state preference and DC's enabled gate.
    if UpdatePullControls then UpdatePullControls() end

    if DungeonClearDB.tinyMode then
        UpdateTinyWidth()
    end

    if UpdateFrameHeight then
        UpdateFrameHeight()
    end
end

-- Command sender via addon messages (silent, no audio cue)
-- Uses PARTY distribution with LANG_ADDON prefix; the server-side hook
-- intercepts and dispatches before any chat processing occurs.
--
-- Solo transport: a player with no group has no PARTY or RAID channel, so this
-- used to refuse outright — which is what stopped a GM from watching a test run
-- from OUTSIDE the bot party. Spectate is session plumbing and never needed a
-- group; the group requirement lived only here, in the transport. A whisper to
-- oneself is the standard addon channel with no group, and the server hook
-- accepts it (IsDcAddonCommand). Bot commands sent this way still need a tank
-- bot in the sender's group and are refused server-side with that reason, which
-- is a truer error than a client-side guess.
local function SendDcCommand(subCmd, param, silent)
    local inRaid = GetNumRaidMembers() and GetNumRaidMembers() > 0
    local inParty = GetNumPartyMembers() and GetNumPartyMembers() > 0

    local payload = "CMD\t" .. subCmd
    if param and param ~= "" then
        payload = payload .. "\t" .. tostring(param)
    end

    if inRaid or inParty then
        -- In a raid, addon messages on the PARTY channel only reach the sender's
        -- own subgroup, so a tank bot in another subgroup never gets the command.
        -- Send on RAID when in a raid so it reaches every subgroup; PARTY covers
        -- the ordinary 5-man case. The server hook accepts both.
        SendAddonMessage("DC", payload, inRaid and "RAID" or "PARTY")
        return
    end

    local me = UnitName("player")
    if me and me ~= "" then
        SendAddonMessage("DC", payload, "WHISPER", me)
    elseif not silent and param ~= "addon" then
        DEFAULT_CHAT_FRAME:AddMessage("|cffff3333DungeonClear: cannot send bot commands right now.|r")
    end
end

-- Action Buttons Panel
-- Four-up action row: On / Off / Skip / Pause-Resume (narrowed to fit one row).
local onBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
onBtn:SetSize(68, 24)
onBtn:SetPoint("TOPLEFT", statusFrame, "BOTTOMLEFT", 0, -8)
onBtn:SetText("On")
onBtn:SetScript("OnClick", function()
    SendDcCommand("on")
    -- The leader tank is elected on "on"; push the player's overrides right
    -- after so the run starts with their settings rather than the defaults.
    if PushSettings then PushSettings() end
end)

local offBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
offBtn:SetSize(68, 24)
offBtn:SetPoint("LEFT", onBtn, "RIGHT", 11, 0)
offBtn:SetText("Off")
offBtn:SetScript("OnClick", function() SendDcCommand("off") end)

local skipBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
skipBtn:SetSize(68, 24)
skipBtn:SetPoint("LEFT", offBtn, "RIGHT", 11, 0)
skipBtn:SetText("Skip")
skipBtn:SetScript("OnClick", function() SendDcCommand("skip") end)

-- Pause/Resume toggle. Label + enabled state are driven by UpdateStatusUI.
-- The click sends the label's INTENT ("pause"/"resume") rather than a bare
-- toggle: a bare "pause" flips whatever the server's flag happens to be, so a
-- click aimed at "Pause" landing just after an auto-pause (door, Wait at Boss)
-- would resume the run instead. With the intent the server no-ops the
-- already-holding case and just resyncs our label.
pauseBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
pauseBtn:SetSize(68, 24)
pauseBtn:SetPoint("LEFT", skipBtn, "RIGHT", 11, 0)
pauseBtn:SetText("Pause")
pauseBtn:SetScript("OnClick", function()
    SendDcCommand("pause", isPaused and "resume" or "pause")
end)

-- Advanced-pull control on a second row: a "Pull:" caption + a 3-segment
-- Off / On / Dynamic picker (replaces the old full-width toggle button). Each
-- segment sends an explicit state so there's no client/server cycle drift; the
-- active segment is highlighted + accent-colored by UpdatePullControls. Dynamic
-- is wired through but is a no-op stub server-side for now.
pullLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
pullLabel:SetPoint("TOPLEFT", onBtn, "BOTTOMLEFT", 2, -14)
pullLabel:SetText("Pull:")
pullLabel:SetTextColor(0.8, 0.8, 0.8)

local PULL_SEG_W = 86
for i = 0, 2 do
    local seg = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    seg:SetSize(PULL_SEG_W, 24)
    if i == 0 then
        seg:SetPoint("LEFT", pullLabel, "RIGHT", 8, 0)
    else
        seg:SetPoint("LEFT", pullSegs[i - 1], "RIGHT", 2, 0)
    end
    seg:SetText(PullStates[i].seg)
    seg:SetScript("OnClick", function()
        SendDcCommand("pull", PullStates[i].cmd)
    end)
    pullSegs[i] = seg
end

-- Spectate toggle on its own row: detaches the player into a free-flying
-- camera while their character keeps running under bot AI (server-side
-- possession of an invisible dummy). Stateless label v1 — the server messages
-- confirm on/off. Independent of DC on/off, but the server can disable the
-- feature entirely (DungeonClear.SpectateEnable = 0): when it does, the SPECTATE
-- message flips spectateAvailable false and the button greys out (see
-- ApplySpectateAvailability). Assume available until the server says otherwise.
spectateBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
spectateBtn:SetSize(100, 24)
-- The pull-row segments span -8..-32 below onBtn (24px buttons centered on
-- the label); start this row at -40 to keep the 8px row gap.
spectateBtn:SetPoint("TOPLEFT", onBtn, "BOTTOMLEFT", 0, -40)
spectateBtn:SetText("Spectate")
-- Left-click = the free-flying camera. Right-click (or shift-click) = follow
-- cam: the view rides the run's tank instead of flying free, which is what you
-- want when watching rather than exploring. Both are the same server toggle
-- family, so a second click of either ends the camera.
spectateBtn:RegisterForClicks("LeftButtonUp", "RightButtonUp")
spectateBtn:SetScript("OnClick", function(self, button)
    if button == "RightButton" or IsShiftKeyDown() then
        SendDcCommand("spectate", "follow")
    else
        SendDcCommand("spectate")
    end
end)

-- Grey out and disable the Spectate button when the server has the feature
-- switched off, so the player can't click into a refusal. A disabled
-- UIPanelButtonTemplate is automatically dimmed and unclickable; the tooltip
-- explains why.
spectateAvailable = false  -- Tortoise: spectate not implemented yet
ApplySpectateAvailability = function()
    if not spectateBtn then return end
    -- The cycle buttons are the same feature; grey them out with it, or they
    -- would click into the same refusal the Spectate button is greyed to avoid.
    for _, b in ipairs({ spectateBtn, spectatePrevBtn, spectateNextBtn }) do
        if b then
            if spectateAvailable then b:Enable() else b:Disable() end
        end
    end
end

spectateBtn:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    if not spectateAvailable then
        GameTooltip:SetText("Spectator mode disabled", 1, 1, 1)
        GameTooltip:AddLine("This server has turned off the spectator camera.",
            0.8, 0.8, 0.8, true)
        GameTooltip:Show()
        return
    end
    GameTooltip:SetText("Spectate", 1, 1, 1)
    GameTooltip:AddLine("Left-click: free-flying camera.", 0.8, 0.8, 0.8, true)
    GameTooltip:AddLine("Right-click: follow cam \226\128\148 your view rides the tank.",
        0.8, 0.8, 0.8, true)
    GameTooltip:Show()
end)
spectateBtn:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- Seat cycling, on the same row as Spectate. The camera can sit on ANY bot in
-- the instance, not just the tank — watch the healer through a wipe, a DPS
-- through a burn — and clicking beats typing a randomised bot name. From no
-- camera at all these also start one (server side treats a cycle from cold as
-- "take the default seat"), so this row is a complete spectator control.
spectatePrevBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
spectatePrevBtn:SetSize(30, 24)
spectatePrevBtn:SetPoint("LEFT", spectateBtn, "RIGHT", 6, 0)
spectatePrevBtn:SetText("|cffffd100<|r")
spectatePrevBtn:SetScript("OnClick", function() SendDcCommand("spectate", "prev") end)
spectatePrevBtn:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText("Previous bot", 1, 1, 1)
    GameTooltip:AddLine("Move the camera to the previous bot in the instance.",
        0.8, 0.8, 0.8, true)
    GameTooltip:Show()
end)
spectatePrevBtn:SetScript("OnLeave", function() GameTooltip:Hide() end)

spectateNextBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
spectateNextBtn:SetSize(30, 24)
spectateNextBtn:SetPoint("LEFT", spectatePrevBtn, "RIGHT", 4, 0)
spectateNextBtn:SetText("|cffffd100>|r")
spectateNextBtn:SetScript("OnClick", function() SendDcCommand("spectate", "next") end)
spectateNextBtn:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText("Next bot", 1, 1, 1)
    GameTooltip:AddLine("Move the camera to the next bot in the instance. " ..
        "Starts the follow cam if it isn't running.", 0.8, 0.8, 0.8, true)
    GameTooltip:Show()
end)
spectateNextBtn:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- Tortoise: spectate is unavailable until the server implements it.
if ApplySpectateAvailability then ApplySpectateAvailability() end

-- Invisible click target over the tiny circle. Off -> start DC; running ->
-- toggle pause/resume. Only shown in tiny mode (see UpdateLayout). Sits over
-- just the 16x16 dot so dragging the rest of the bar still works.
tinyToggle = CreateFrame("Button", "DungeonClearTinyToggle", frame)
tinyToggle:SetAllPoints(tinyIndicator)
tinyToggle:EnableMouse(true)
tinyToggle:RegisterForClicks("LeftButtonUp", "RightButtonUp")
tinyToggle:SetScript("OnClick", function(self, button)
    -- Right-click over the circle expands back to the full window (matches the
    -- frame-level OnMouseUp handler that covers the rest of the tiny bar).
    if button == "RightButton" then
        DungeonClearDB.tinyMode = false
        UpdateLayout()
        return
    end
    if not isDCOn then
        SendDcCommand("on")
        if PushSettings then PushSettings() end
    else
        -- Send the intent, not a bare toggle — see pauseBtn's OnClick note.
        SendDcCommand("pause", isPaused and "resume" or "pause")
    end
end)
tinyToggle:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP")
    if not isDCOn then
        GameTooltip:AddLine("Dungeon Clear")
        GameTooltip:AddLine("Left-click to start the clear", 0.8, 0.8, 0.8, true)
    else
        GameTooltip:AddLine(isPaused and "Paused" or "Clearing")
        GameTooltip:AddLine(isPaused and "Left-click to resume" or "Left-click to pause", 0.8, 0.8, 0.8, true)
    end
    GameTooltip:AddLine("Right-click to expand the window", 0.8, 0.8, 0.8, true)
    GameTooltip:Show()
end)
tinyToggle:SetScript("OnLeave", function() GameTooltip:Hide() end)
tinyToggle:Hide()

-- Tiny-mode advanced-pull control: a small colored circle right after the status
-- (pause) circle that cycles Off -> On -> Dynamic, mirroring the pause dot. The
-- live pull state reads out as a short caption beside it, capped with a grey "|"
-- pipe that separates it from the action/boss line that follows.
tinyPullDot = frame:CreateTexture(nil, "OVERLAY")
tinyPullDot:SetSize(16, 16)
tinyPullDot:SetPoint("LEFT", tinyIndicator, "RIGHT", 6, 0)
-- Reuse the FriendsFrame status dots (same family as the pause circle, which is
-- known to render). The texture is swapped per state by UpdatePullControls.
tinyPullDot:SetTexture("Interface\\FriendsFrame\\StatusIcon-Offline")

tinyPullText = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
tinyPullText:SetPoint("LEFT", tinyPullDot, "RIGHT", 4, 0)
tinyPullText:SetText("Off")

-- The action/boss status text now trails the pull caption.
tinyText:ClearAllPoints()
tinyText:SetPoint("LEFT", tinyPullText, "RIGHT", 2, 0)

-- Invisible click target over the pull circle: left-click cycles Off -> On ->
-- Dynamic; right-click expands back to the full window (matching the other tiny
-- controls). Live whether or not DC is running -- the pull mode is a preference
-- the server stores and applies on the next `dc on`, so it is settable ahead of
-- the run. Shown only in tiny mode.
tinyPullToggle = CreateFrame("Button", "DungeonClearTinyPull", frame)
tinyPullToggle:SetAllPoints(tinyPullDot)
tinyPullToggle:EnableMouse(true)
tinyPullToggle:RegisterForClicks("LeftButtonUp", "RightButtonUp")
tinyPullToggle:SetScript("OnClick", function(self, button)
    if button == "RightButton" then
        DungeonClearDB.tinyMode = false
        UpdateLayout()
        return
    end
    local nextState = (pullSetting + 1) % 3
    SendDcCommand("pull", PullStates[nextState].cmd)
end)
tinyPullToggle:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP")
    GameTooltip:AddLine("Pull Mode")
    GameTooltip:AddLine("Click to cycle: Leeroy / Advanced / Dynamic", 0.8, 0.8, 0.8, true)
    GameTooltip:Show()
end)
tinyPullToggle:SetScript("OnLeave", function() GameTooltip:Hide() end)
tinyPullDot:Hide()
tinyPullText:Hide()
tinyPullToggle:Hide()

-- Style the full-mode segments + tiny cycle button to the current pull state.
-- Active segment: locked highlight + accent text. Inactive: dim grey. The
-- controls stay clickable while DC is off: the pull mode is a preference the
-- server stores and applies on the next `dc on`, so it is configurable ahead of
-- the run. While off we still surface the chosen state, just dimmed to read as
-- "pending" rather than live.
UpdatePullControls = function()
    -- Live verdict shown only while Dynamic is the active state and DC is on.
    local verdict = (isDCOn and pullSetting == 2) and DynVerdicts[pullDecision] or nil

    -- Pull-mode readout in the status box: the active state, plus the live verdict
    -- when Dynamic. Dimmed while DC is off (the setting still applies on the next
    -- run, so show it rather than blanking it).
    if pullModeVal then
        local text = PullStates[pullSetting].seg
        if pullSetting == 2 then
            text = "Dynamic" .. (verdict and (" (" .. verdict.full .. ")") or "")
        end
        pullModeVal:SetText(text)
        if not isDCOn then
            pullModeVal:SetTextColor(0.5, 0.5, 0.5)
        else
            pullModeVal:SetTextColor(unpack(verdict and verdict.color or PullStates[pullSetting].color))
        end
    end

    for i = 0, 2 do
        local seg = pullSegs[i]
        if seg then
            local fs = seg:GetFontString()
            -- Segments keep their base labels (Leeroy / Advanced / Dynamic); the
            -- active state and its live verdict are surfaced in the Pull Mode
            -- readout above instead.
            seg:SetText(PullStates[i].seg)
            -- Always clickable: the pull mode is settable before the run starts.
            seg:Enable()
            if i == pullSetting then
                seg:LockHighlight()
                -- Active Dyn segment tints to the verdict colour (amber Leeroy /
                -- blue Advanced) so the live choice reads at a glance. While DC
                -- is off the choice is a pending preference, so dim the accent.
                local accent = (i == 2 and verdict) and verdict.color or PullStates[i].color
                if fs then
                    if isDCOn then
                        fs:SetTextColor(unpack(accent))
                    else
                        fs:SetTextColor(accent[1] * 0.6, accent[2] * 0.6, accent[3] * 0.6)
                    end
                end
            else
                seg:UnlockHighlight()
                if fs then fs:SetTextColor(0.7, 0.7, 0.7) end
            end
        end
    end

    if tinyPullDot then
        -- Always clickable: the pull mode is settable before the run starts.
        if tinyPullToggle then tinyPullToggle:Enable() end
        -- The caption is the pull state; in Dynamic with DC on it appends the live
        -- verdict (Leeroy / Advanced) and carries the precise accent colour. The
        -- dot is a coarse 3-state indicator: dark = Off, green = On, blue =
        -- Dynamic. A grey "|" pipe caps the caption to divide it from the action
        -- line. While DC is off the state is a pending preference, so a dim factor
        -- darkens both the dot and caption to read as "set, not yet running".
        local label = PullStates[pullSetting].seg
        local color = PullStates[pullSetting].color
        if verdict then
            label = label .. ": " .. verdict.full
            color = verdict.color
        end
        local dim = isDCOn and 1.0 or 0.6
        if pullSetting == 1 then
            tinyPullDot:SetTexture("Interface\\FriendsFrame\\StatusIcon-Online") -- green
            tinyPullDot:SetTexCoord(0, 1, 0, 1)
            tinyPullDot:SetVertexColor(dim, dim, dim)
        elseif pullSetting == 2 then
            -- Solid blue circle for Dynamic: the portrait alpha mask is a filled
            -- white disc (tints cleanly to blue). It fills its frame edge to edge,
            -- so pad it with an over-range texcoord (clamps to transparent) to
            -- match the inset of the StatusIcon dots; otherwise it reads oversized.
            tinyPullDot:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask")
            tinyPullDot:SetTexCoord(-0.35, 1.35, -0.35, 1.35)
            tinyPullDot:SetVertexColor(0.30 * dim, 0.70 * dim, 1.00 * dim)
        else
            tinyPullDot:SetTexture("Interface\\FriendsFrame\\StatusIcon-Offline")
            tinyPullDot:SetTexCoord(0, 1, 0, 1)
            tinyPullDot:SetVertexColor(dim, dim, dim)
        end
        if not isDCOn then
            color = {color[1] * dim, color[2] * dim, color[3] * dim}
        end
        if tinyPullText then
            tinyPullText:SetText("|cff" .. RgbToHex(color) .. label .. "|r |cff808080|| |r")
        end
        if DungeonClearDB.tinyMode then UpdateTinyWidth() end
    end
end

-- Boss List Label
local listLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
-- Below the pull + spectate rows: onBtn bottom, minus the 8px gap + 24px
-- segment row + 8px gap + 24px spectate row + 12px.
listLabel:SetPoint("TOPLEFT", onBtn, "BOTTOMLEFT", 0, -76)
listLabel:SetText("Dungeon Bosses")
listLabel:SetTextColor(0.24, 0.60, 1.0)

-- Boss List Scroll Frame container
local scrollContainer = CreateFrame("Frame", nil, frame)
scrollContainer:SetSize(306, 205)
scrollContainer:SetPoint("TOPLEFT", listLabel, "BOTTOMLEFT", 0, -4)
scrollContainer:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 12, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
scrollContainer:SetBackdropColor(0.05, 0.05, 0.08, 0.50)
scrollContainer:SetBackdropBorderColor(0.15, 0.17, 0.22, 0.8)

-- Uniform row height sized for the worst case: a two-line wrapped name plus a
-- folded-event note beneath it. FauxScrollFrame requires a single fixed row
-- height (all its offset/range math multiplies by it), so short rows are padded
-- to this height rather than shrinking. The note is anchored under the name (not
-- a fixed band) so the name can wrap freely and the note floats down with it.
local ROW_HEIGHT = 48
local VISIBLE_ROWS = 4

-- Scrollable boss list. FauxScrollFrame is the idiomatic WotLK pattern: a small
-- fixed pool of visible rows is reused while an offset selects which slice of
-- `bosses` they display, so the list scrolls without growing the window.
local scrollFrame = CreateFrame("ScrollFrame", "DungeonClearScrollFrame", scrollContainer, "FauxScrollFrameTemplate")
scrollFrame:SetPoint("TOPLEFT", scrollContainer, "TOPLEFT", 8, -8)
scrollFrame:SetPoint("BOTTOMRIGHT", scrollContainer, "BOTTOMRIGHT", -28, 8) -- leave room for the scrollbar
scrollFrame:EnableMouseWheel(true)
scrollFrame:SetScript("OnVerticalScroll", function(self, offset)
    FauxScrollFrame_OnVerticalScroll(self, offset, ROW_HEIGHT, RedrawBossList)
end)
scrollFrame:SetScript("OnMouseWheel", function(self, delta)
    local bar = DungeonClearScrollFrameScrollBar
    bar:SetValue(bar:GetValue() - delta * ROW_HEIGHT)
end)

-- Pre-create the visible row pool inside scrollContainer, anchored to scrollFrame
for i = 1, VISIBLE_ROWS do
    local row = CreateFrame("Frame", nil, scrollContainer)
    row:SetSize(262, ROW_HEIGHT - 2)
    row:SetPoint("TOPLEFT", scrollFrame, "TOPLEFT", 0, -(i - 1) * ROW_HEIGHT)

    -- Custom solid color texture instead of SetBackdrop to prevent client crashes
    local bg = row:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(row)
    bg:SetTexture(0.08, 0.10, 0.15, 0.4)
    row.bg = bg

    -- Text label
    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    row.text:SetPoint("LEFT", row, "LEFT", 8, 0)
    row.text:SetWidth(150)
    row.text:SetJustifyH("LEFT")
    row.text:SetWordWrap(false)

    -- Folded-event sub-line: a gating event (e.g. an Uldaman altar) shown under
    -- the boss it gates, instead of as its own row with a Go that can't resolve.
    -- Hidden unless the BOSS message carried an event note (field 10).
    row.sub = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.sub:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 16, 3)
    -- Near-full row width: on event rows the Go button is lifted onto the name
    -- line (see RedrawBossList) so the note owns the whole bottom band.
    row.sub:SetWidth(234)
    row.sub:SetJustifyH("LEFT")
    -- Single line only: a long note must truncate, never wrap down onto the
    -- next boss's row (that was the overlap bug).
    row.sub:SetWordWrap(false)
    row.sub:Hide()

    -- Status badge
    row.status = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.status:SetPoint("LEFT", row.text, "RIGHT", 5, 0)

    -- "Go" action button
    row.goBtn = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    row.goBtn:SetSize(46, 20)
    row.goBtn:SetPoint("RIGHT", row, "RIGHT", -6, 0)
    row.goBtn:SetText("Go")

    -- Hovering a row with a folded event shows the full note in a tooltip, so a
    -- name too long for the bottom band (which truncates) is still readable.
    -- RedrawBossList stashes the boss name + raw note on the row each draw.
    row:EnableMouse(true)
    row:SetScript("OnEnter", function(self)
        if not self.eventNoteFull or self.eventNoteFull == "" then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if self.bossName then
            GameTooltip:AddLine(self.bossName, 1, 1, 1)
        end
        -- Multiple folded events arrive joined by " | "; one tooltip line each.
        for note in string.gmatch(self.eventNoteFull, "[^|]+") do
            GameTooltip:AddLine(strtrim(note), 0.78, 0.63, 0.18, true)
        end
        GameTooltip:Show()
    end)
    row:SetScript("OnLeave", function() GameTooltip:Hide() end)
    -- EnableMouse swallows the wheel, so forward it to keep the list scrollable
    -- while the cursor is over a row.
    row:EnableMouseWheel(true)
    row:SetScript("OnMouseWheel", function(_, delta)
        local bar = DungeonClearScrollFrameScrollBar
        bar:SetValue(bar:GetValue() - delta * ROW_HEIGHT)
    end)

    bossRows[i] = row
end

-- Colour a folded-event sub-line by each event's completion state. The server
-- (DungeonClearChatActions) appends " (done)" / " (skipped)" to finished events
-- and leaves pending ones bare; several events gating one boss arrive joined by
-- " | ". Done events get a green ready-check tick + green text, skipped events
-- grey out, pending events keep the gold dash.
local function FormatEventNote(note)
    local out = {}
    for seg in string.gmatch(note, "[^|]+") do
        seg = strtrim(seg)
        if seg ~= "" then
            local piece
            if string.find(seg, "%(done%)$") then
                -- Ready-check tick doubles as the "completed" checkbox.
                piece = "|TInterface\\RaidFrame\\ReadyCheck-Ready:12:12|t |cff3fd03f" .. seg .. "|r"
            elseif string.find(seg, "%(skipped%)$") then
                piece = "|cff888888- " .. seg .. "|r"
            else
                piece = "|cffc8a02e- " .. seg .. "|r"
            end
            table.insert(out, piece)
        end
    end
    return table.concat(out, "  ")
end

-- Redraw Boss List rows (FauxScrollFrame implementation)
RedrawBossList = function()
    -- Never render a blank panel. Until a real list arrives, show a single
    -- placeholder row so the user sees the list is loading rather than empty.
    -- The OnUpdate ensure-loop keeps re-requesting until bosses populate.
    if #bosses == 0 then
        FauxScrollFrame_Update(scrollFrame, 0, VISIBLE_ROWS, ROW_HEIGHT)
        for i = 1, VISIBLE_ROWS do bossRows[i]:Hide() end
        local row = bossRows[1]
        row.eventNoteFull = nil
        row.text:ClearAllPoints()
        row.text:SetPoint("LEFT", row, "LEFT", 8, 0)
        row.text:SetText("Loading boss list...")
        row.text:SetTextColor(0.6, 0.6, 0.6)
        row.sub:Hide()
        row.status:SetText("")
        row.goBtn:Hide()
        row:Show()
        return
    end

    FauxScrollFrame_Update(scrollFrame, #bosses, VISIBLE_ROWS, ROW_HEIGHT)
    local offset = FauxScrollFrame_GetOffset(scrollFrame)

    for i = 1, VISIBLE_ROWS do
        local row = bossRows[i]
        -- 1-based ordinal position in the sorted list: clean sequential numbering
        -- even when a filtered wing yields non-contiguous encounter indices.
        local dataIndex = i + offset
        local boss = bosses[dataIndex]

        if boss then
            -- On split maps, tag the row with its region. Wing labels read like
            -- "Maraudon (Orange)"; show just the parenthetical ("Orange") to
            -- keep the row short, falling back to the full label otherwise.
            local label = dataIndex .. ". " .. boss.name
            if boss.wing then
                local region = boss.wing:match("%((.-)%)") or boss.wing
                label = label .. " |cff9999ff(" .. region .. ")|r"
            end
            row.text:SetText(label)
            -- Reset color: the loading placeholder dims row 1 to grey, so a real
            -- entry reusing that row must restore the normal white highlight.
            row.text:SetTextColor(1, 1, 1)

            -- Folded gating event: render it as a sub-line and lift the boss name
            -- to the top of the row so both fit; otherwise keep the name centered.
            -- Stash for the hover tooltip (full, untruncated note).
            row.eventNoteFull = boss.eventNote
            row.bossName = boss.name

            row.text:ClearAllPoints()
            row.goBtn:ClearAllPoints()
            row.sub:ClearAllPoints()
            if boss.eventNote then
                -- Name (+ Go) on top, the event note directly beneath it. The
                -- note is anchored to the name's BOTTOM (not a fixed row band) so
                -- the name is free to wrap to a second line — the note simply
                -- floats down with it instead of being collided into. Lifting Go
                -- onto the name line keeps it clear of the note.
                row.text:SetWordWrap(true)
                row.text:SetPoint("TOPLEFT", row, "TOPLEFT", 8, -3)
                row.goBtn:SetPoint("TOPRIGHT", row, "TOPRIGHT", -6, -2)
                row.sub:SetPoint("TOPLEFT", row.text, "BOTTOMLEFT", 8, -2)
                -- Per-event colouring: pending = gold dash, done = green tick,
                -- skipped = grey (see FormatEventNote).
                row.sub:SetText(FormatEventNote(boss.eventNote))
                row.sub:Show()
            else
                -- No sub-line: let a long name (e.g. "Objective: Atal'ai Defender
                -- (Mijan)") wrap to a second line instead of truncating. The row
                -- is tall enough for two lines, and the name owns the full height.
                row.text:SetWordWrap(true)
                row.text:SetPoint("LEFT", row, "LEFT", 8, 0)
                row.goBtn:SetPoint("RIGHT", row, "RIGHT", -6, 0)
                row.sub:Hide()
            end

            -- Style status color
            local statusLabelText = "Alive"
            local statusColor = {0.1, 0.9, 0.1}
            local showGo = true

            if boss.status == "dead" then
                statusLabelText = "Dead"
                statusColor = {0.6, 0.6, 0.6}
                showGo = false
            elseif boss.status == "skipped" then
                statusLabelText = "Skipped"
                statusColor = {0.9, 0.7, 0.1}
                showGo = true
            elseif boss.status == "missing" then
                statusLabelText = "Missing"
                statusColor = {0.5, 0.5, 0.7}
                showGo = true
            elseif boss.status == "waiting" then
                statusLabelText = "Waiting for spawn"
                statusColor = {0.9, 0.7, 0.1}
                showGo = true
            end

            -- Standalone off-path "Event:" rows (gates with no boss to drive to —
            -- they fire automatically as the tank passes) get no Go button; it
            -- could never resolve to a creature. Boss-gating events are folded
            -- into their boss row (above) and keep the boss's own working Go.
            if boss.name and string.sub(boss.name, 1, 6) == "Event:" then
                showGo = false
            end

            row.status:SetText(statusLabelText)
            row.status:SetTextColor(unpack(statusColor))

            if showGo then
                row.goBtn:Show()
                row.goBtn:SetScript("OnClick", function()
                    if not isDCOn then
                        SendDcCommand("on")
                        if PushSettings then PushSettings() end
                    end
                    SendDcCommand("go", tostring(boss.entry) .. ":" .. tostring(boss.encounterIndex))
                end)
            else
                row.goBtn:Hide()
            end

            row:Show()
        else
            row:Hide()
        end
    end
end

-- (Chat spam filter checkbox removed — addon messages are inherently silent)

-- Toggle Buttons and Layout Adjustments
local tinyBtn = CreateFrame("Button", "DungeonClearTinyButton", frame, "UIPanelButtonTemplate")
tinyBtn:SetSize(40, 20)
tinyBtn:SetPoint("RIGHT", closeBtn, "LEFT", 2, 0)
tinyBtn:SetText("Tiny")

local toggleBossesBtn = CreateFrame("Button", "DungeonClearToggleBossesButton", frame)
toggleBossesBtn:SetSize(24, 24)
toggleBossesBtn:SetPoint("LEFT", listLabel, "RIGHT", 6, 0)
toggleBossesBtn:SetNormalFontObject("GameFontNormal")
toggleBossesBtn:SetHighlightFontObject("GameFontHighlight")
toggleBossesBtn:SetText("[-]")
local btnText = toggleBossesBtn:GetFontString()
if btnText then
    btnText:SetTextColor(0.24, 0.60, 1.0)
end

UpdateFrameHeight = function()
    local hasStall = stallVal:IsShown()
    if DungeonClearDB.tinyMode then
        frame:SetHeight(28)
        UpdateTinyWidth()
    else
        frame:SetWidth(330)
        -- +32 each for the advanced-pull and spectate rows (24px button + 8px gap).
        -- The base figures include the +10 reserve added to statusFrame so a
        -- two-line detail sub-line clears the Next Boss row (see targetLabel).
        if DungeonClearDB.bossesFolded then
            frame:SetHeight(hasStall and 330 or 310)
        else
            frame:SetHeight(hasStall and 560 or 540)
        end
    end
end

UpdateLayout = function()
    if DungeonClearDB.tinyMode then
        -- Single-line readout only: no header, no close/tiny buttons, no panels
        header:Hide()
        closeBtn:Hide()
        tinyBtn:Hide()
        onBtn:Hide()
        offBtn:Hide()
        skipBtn:Hide()
        if pauseBtn then pauseBtn:Hide() end
        if pullLabel then pullLabel:Hide() end
        for i = 0, 2 do if pullSegs[i] then pullSegs[i]:Hide() end end
        if spectateBtn then spectateBtn:Hide() end
        if spectatePrevBtn then spectatePrevBtn:Hide() end
        if spectateNextBtn then spectateNextBtn:Hide() end
        listLabel:Hide()
        toggleBossesBtn:Hide()
        scrollContainer:Hide()
        statusFrame:Hide()

        tinyIndicator:Show()
        tinyText:Show()
        if tinyToggle then tinyToggle:Show() end
        if tinyPullDot then tinyPullDot:Show() end
        if tinyPullText then tinyPullText:Show() end
        if tinyPullToggle then tinyPullToggle:Show() end
    else
        tinyIndicator:Hide()
        tinyText:Hide()
        if tinyToggle then tinyToggle:Hide() end
        if tinyPullDot then tinyPullDot:Hide() end
        if tinyPullText then tinyPullText:Hide() end
        if tinyPullToggle then tinyPullToggle:Hide() end

        header:Show()
        closeBtn:Show()
        tinyBtn:Show()
        tinyBtn:SetText("Tiny")
        onBtn:Show()
        offBtn:Show()
        skipBtn:Show()
        if pauseBtn then pauseBtn:Show() end
        if pullLabel then pullLabel:Show() end
        for i = 0, 2 do if pullSegs[i] then pullSegs[i]:Show() end end
        if spectateBtn then spectateBtn:Show() end
        if spectatePrevBtn then spectatePrevBtn:Show() end
        if spectateNextBtn then spectateNextBtn:Show() end
        listLabel:Show()
        toggleBossesBtn:Show()
        statusFrame:Show()

        statusFrame:ClearAllPoints()
        statusFrame:SetPoint("TOP", frame, "TOP", 0, -35)

        if DungeonClearDB.bossesFolded then
            toggleBossesBtn:SetText("[+]")
            scrollContainer:Hide()

        else
            toggleBossesBtn:SetText("[-]")
            scrollContainer:Show()

        end
    end
    if UpdatePullControls then UpdatePullControls() end
    UpdateFrameHeight()
end

tinyBtn:SetScript("OnClick", function()
    DungeonClearDB.tinyMode = not DungeonClearDB.tinyMode
    UpdateLayout()
end)

toggleBossesBtn:SetScript("OnClick", function()
    DungeonClearDB.bossesFolded = not DungeonClearDB.bossesFolded
    UpdateLayout()
end)

-- Layout saving on drag stop
frame:SetScript("OnDragStop", function(self)
    self:StopMovingOrSizing()
    local point, _, relativePoint, xOfs, yOfs = self:GetPoint()
    DungeonClearDB.point = point
    DungeonClearDB.relativePoint = relativePoint
    DungeonClearDB.xOfs = xOfs
    DungeonClearDB.yOfs = yOfs
end)

-- Event Handling Frame
local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("CHAT_MSG_ADDON")
eventFrame:RegisterEvent("ZONE_CHANGED_NEW_AREA")
eventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")
eventFrame:RegisterEvent("PARTY_MEMBERS_CHANGED")
if eventFrame.RegisterEvent then
    -- Turtle may also fire the WotLK name on some builds.
    pcall(eventFrame.RegisterEvent, eventFrame, "GROUP_ROSTER_UPDATE")
end
eventFrame:RegisterEvent("PLAYER_REGEN_DISABLED")
eventFrame:RegisterEvent("PLAYER_REGEN_ENABLED")

-- Request the boss list from the tank bot. The server's "dungeon bosses" value
-- returns empty (and caches that for ~5s) whenever the bot isn't fully in the
-- dungeon yet, so a single query on zone-change is unreliable. Callers pair this
-- with the empty-list retry below to keep asking until a real list comes back;
-- once populated, the server pushes any further changes on its own.
local function RequestBossList()
    SendDcCommand("bosses", "addon")
end

-- The server is now event-driven: while a clear is running it recomputes the
-- tank's status every world tick and pushes a STATUS packet only when the state
-- changes (entered combat, pulled a boss, a boss died, stalled, looting, party
-- recovered, …), and likewise re-pushes the BOSS list whenever a boss's
-- alive/dead/skipped state or the committed target changes. So we no longer
-- poll for either — STATUS and BOSS arrive on their own the instant they move.
--
-- The one case the push path can't cover is browsing the boss list while DC is
-- OFF: with no clear running there's no server-side pusher, and the bot's
-- "dungeon bosses" value returns empty (cached ~5s) until it's fully zoned into
-- the dungeon. So keep a bounded retry that fires ONLY while the list is still
-- empty — it self-terminates the moment a real list arrives and never becomes a
-- steady poll. RedrawBossList preserves the scroll offset.
local bossEnsureElapsed = 0
local function OnUpdateHandler(self, elap)
    if not frame:IsVisible() then return end
    if #bosses > 0 then return end  -- populated: the server pushes updates from here

    bossEnsureElapsed = bossEnsureElapsed + elap
    if bossEnsureElapsed >= 2.0 then
        bossEnsureElapsed = 0
        local inInstance, instanceType = IsInInstance()
        if inInstance and (instanceType == "party" or instanceType == "raid") then
            RequestBossList()
        end
    end
end
frame:SetScript("OnUpdate", OnUpdateHandler)

-- Addon Messages parsing
local function OnAddonMessage(prefix, message, channel, sender)
    if prefix ~= "DC" then return end

    local parts = {}
    local start = 1
    while true do
        local pos = string.find(message, "\t", start)
        if not pos then
            table.insert(parts, string.sub(message, start))
            break
        end
        table.insert(parts, string.sub(message, start, pos - 1))
        start = pos + 1
    end

    if parts[1] == "STATUS" then
        local enabled = parts[2]
        local nextBossEntry = parts[3]
        local nextBossName = parts[4]
        local stallReason = parts[5]
        local skippedCount = parts[6]
        local state = parts[7]
        local detail = parts[8]
        -- Trailing field (index 9): advanced-pull toggle ("1"/"0"). Older servers
        -- omit it; nil reads as off.
        local pullMode = parts[9]
        -- Trailing field (index 10): live Dynamic verdict (0 none / 1 Leeroy /
        -- 2 Advanced). Older servers omit it; nil reads as none.
        local pullDec = parts[10]

        if nextBossName == "None" then nextBossName = nil end
        if stallReason == "" then stallReason = nil end
        if detail == "" then detail = nil end

        UpdateStatusUI(enabled, nextBossName, state, stallReason, detail, pullMode, pullDec)
    elseif parts[1] == "BOSS_START" then
        -- Stage into pendingBosses; the live list is untouched until BOSS_END
        -- so a response that turns out empty (or never finalizes) can't blank
        -- a list that's already showing.
        pendingBosses = {}
    elseif parts[1] == "BOSS" then
        local entry = tonumber(parts[2])
        local sortIndex = tonumber(parts[3]) or 0
        -- Newer servers keep the display/sort key separate from the real
        -- encounter index so Go can disambiguate entries whose order is
        -- overridden by an event patch. Older servers have no field 11 and
        -- safely fall back to field 3.
        local index = tonumber(parts[11]) or sortIndex
        local name = parts[4]
        local status = parts[5]
        local x = tonumber(parts[6])
        local y = tonumber(parts[7])
        local z = tonumber(parts[8])
        -- Optional trailing field: wing/region label on split maps (e.g.
        -- Maraudon "Orange"/"Purple"/"Pristine Waters"). Empty/absent on
        -- single-wing dungeons. Older servers omit it; nil is fine.
        local wing = parts[9]
        if wing == "" then wing = nil end
        -- Optional trailing field (index 10): folded-event note for an event that
        -- gates this boss (e.g. "Activate the Altar (pending)"). Rendered as a
        -- sub-line under the row. Older servers omit it; nil reads as none.
        local eventNote = parts[10]
        if eventNote == "" then eventNote = nil end

        table.insert(pendingBosses, {
            entry = entry,
            encounterIndex = index,
            sortIndex = sortIndex,
            eventNote = eventNote,
            -- Arrival order = the server's clear order. Used as the sort
            -- tiebreak so that several anchors sharing one encounterIndex
            -- (e.g. Sunken Temple's six forcefield defenders, all index 0)
            -- keep a stable, server-matching order in the panel — Lua's
            -- table.sort is not stable, so without this they'd shuffle.
            seq = #pendingBosses,
            name = name,
            status = status,
            x = x, y = y, z = z,
            wing = wing
        })
    elseif parts[1] == "BOSS_END" then
        if #pendingBosses > 0 then
            -- A real list arrived: commit it, sorted by the server's order key, with
            -- arrival order as a stable tiebreak so same-index anchors keep the
            -- server's clear order (see `seq` above).
            table.sort(pendingBosses, function(a, b)
                if a.sortIndex ~= b.sortIndex then
                    return a.sortIndex < b.sortIndex
                end
                return (a.seq or 0) < (b.seq or 0)
            end)
            bosses = pendingBosses
            pendingBosses = {}
            RedrawBossList()
        else
            -- Empty response. Never downgrade a good list to empty — that's the
            -- transient-empty case the ensure-loop will retry past. Only redraw
            -- (to show the "Loading" placeholder) if we have nothing yet.
            if #bosses == 0 then
                RedrawBossList()
            end
        end
    elseif parts[1] == "SYNCSTART" then
        if OnSettingsSyncBoundary then OnSettingsSyncBoundary("start") end
    elseif parts[1] == "SETTINGS" then
        -- One player-facing setting's effective value + schema (key, value, min,
        -- max, type, overridden). Renders/refreshes its control in the panel.
        if HandleSettingsLine then HandleSettingsLine(parts) end
    elseif parts[1] == "SPECTATE" then
        -- Server tells us whether the spectator camera is enabled (DungeonClear.
        -- SpectateEnable). Grey out / disable the button when it's off so a click
        -- can't run into a refusal. Sent in answer to our status poll.
        spectateAvailable = (parts[2] ~= "0")
        if ApplySpectateAvailability then ApplySpectateAvailability() end
    elseif parts[1] == "SYNCEND" then
        if OnSettingsSyncBoundary then OnSettingsSyncBoundary("end") end
    elseif parts[1] == "CHAT" then
        -- Bot announcements routed through addon channel (silent)
        local chatMsg = parts[2] or ""
        DEFAULT_CHAT_FRAME:AddMessage("|cff3da6ff[DC] " .. chatMsg .. "|r")
    elseif parts[1] == "ERROR" then
        -- The only error the server hook raises is "no tank bot found", which
        -- our background status/boss polls provoke constantly whenever the tank
        -- bot isn't in the instance with us. While DC is OFF that's expected and
        -- says nothing useful, so it must never reach chat — printing it spammed
        -- the player on every poll. We only act on it during a live clear: if we
        -- still think DC is active, the tank left mid-run, so revert to OFF
        -- (one-shot, since this flips isDCOn false) and say so once.
        if isDCOn then
            UpdateStatusUI("0", nil, "off", nil)
            DEFAULT_CHAT_FRAME:AddMessage("|cffff3333[DC] Tank bot is no longer in the group \226\128\148 dungeon clear turned off.|r")
        end
    end
end

-- Event handler
eventFrame:SetScript("OnEvent", function(self, event, ...)
    if event == "ADDON_LOADED" then
        local name = ...
        if name == AddonName then
            -- Initialize DB defaults if needed
            if DungeonClearDB.tinyMode == nil then DungeonClearDB.tinyMode = false end
            if DungeonClearDB.bossesFolded == nil then DungeonClearDB.bossesFolded = false end
            DungeonClearDB.hideChatSpam = nil -- Clean up legacy saved variable

            -- Per-player setting overrides + the last schema the server told us
            -- about (so the panel can render controls even before a sync lands).
            DungeonClearDB.settings = DungeonClearDB.settings or {}
            DungeonClearDB.schema = DungeonClearDB.schema or {}
            DungeonClearDB.schemaOrder = DungeonClearDB.schemaOrder or {}
            if BuildSettingsFromCache then BuildSettingsFromCache() end

            -- Restore layout
            frame:ClearAllPoints()
            frame:SetPoint(DungeonClearDB.point or "CENTER", UIParent, DungeonClearDB.relativePoint or "CENTER", DungeonClearDB.xOfs or 0, DungeonClearDB.yOfs or 0)

            if UpdateLayout then
                UpdateLayout()
            end

            if DungeonClearDB.visible then
                frame:Show()
            else
                frame:Hide()
            end

            -- WotLK addon message prefix registration (only needed/exists in 4.1+)
            if RegisterAddonMessagePrefix then
                RegisterAddonMessagePrefix(Prefix)
            end
        end
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = ...
        OnAddonMessage(prefix, message, channel, sender)
    elseif event == "ZONE_CHANGED_NEW_AREA" or event == "PLAYER_ENTERING_WORLD"
    or event == "PARTY_MEMBERS_CHANGED" or event == "GROUP_ROSTER_UPDATE" then
        local inInstance, instanceType = IsInInstance()

        -- Did we cross into a *different* instance since the list was built? If
        -- so, drop the stale boss list now. This re-arms the empty-list ensure
        -- loop (it only retries while #bosses == 0) so it keeps re-requesting
        -- until the new dungeon/raid's list arrives, and paints "Loading..."
        -- meanwhile — instead of showing the previous run's bosses until the
        -- player manually toggles DC on. Covers both walking a dungeon into a
        -- raid and starting a second dungeon without toggling off.
        local newKey = GetInstanceKey()
        if newKey ~= currentInstanceKey then
            currentInstanceKey = newKey
            bosses = {}
            pendingBosses = {}
            bossEnsureElapsed = 0
            RedrawBossList()
        end

        if inInstance and (instanceType == "party" or instanceType == "raid") then
            -- Auto-query bosses list when entering dungeon/raid
            -- Small delay to ensure party is fully loaded on the server
            local delayFrame = CreateFrame("Frame")
            local delayElapsed = 0
            delayFrame:SetScript("OnUpdate", function(sf, elap)
                delayElapsed = delayElapsed + elap
                if delayElapsed >= 3.0 then
                    RequestBossList()
                    -- Re-apply this player's saved overrides for the new run:
                    -- the server keeps them only in memory keyed to the leader
                    -- tank, so they must be pushed again each time we (re)enter.
                    if PushSettings then PushSettings() end
                    sf:SetScript("OnUpdate", nil)
                end
            end)
        end
    elseif event == "PLAYER_REGEN_DISABLED" or event == "PLAYER_REGEN_ENABLED" then
        if frame:IsVisible() and isDCOn then
            SendDcCommand("status", "addon")
        end
    end
end)

-- Window show/hide triggers status update
frame:SetScript("OnShow", function()
    DungeonClearDB.visible = true
    SendDcCommand("status", "addon")
    RequestBossList()
    -- Paint the loading placeholder now so the panel is never blank on open;
    -- it's replaced the moment a BOSS_END arrives (and the ensure-loop keeps
    -- re-requesting until then).
    bossEnsureElapsed = 0
    RedrawBossList()
end)

frame:SetScript("OnHide", function()
    DungeonClearDB.visible = false
end)

-- Interface -> AddOns options panel (informational front door)
-- A simple, read-only page registered under Game Menu -> Interface -> AddOns:
-- overview text, a command/control reference, and a button that opens the main
-- window exactly like typing /dc. No settings live here; all controls stay in
-- the floating window.
local optionsPanel = CreateFrame("Frame", "DungeonClearOptionsPanel", UIParent)
optionsPanel.name = "DungeonClear"

local optTitle = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
optTitle:SetPoint("TOPLEFT", optionsPanel, "TOPLEFT", 16, -16)
optTitle:SetText("Dungeon Clear")
optTitle:SetTextColor(0.24, 0.60, 1.0) -- match the main window header

local optSubtitle = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
optSubtitle:SetPoint("TOPLEFT", optTitle, "BOTTOMLEFT", 0, -4)
optSubtitle:SetText("Autonomous dungeon-clearing companion for Tortoise DungeonClear.")
optSubtitle:SetTextColor(0.6, 0.6, 0.6)

local optOverview = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
optOverview:SetPoint("TOPLEFT", optSubtitle, "BOTTOMLEFT", 0, -14)
optOverview:SetWidth(560)
optOverview:SetJustifyH("LEFT")
optOverview:SetText(
    "A mod-playerbots tank bot walks your party from boss to boss, clearing trash and " ..
    "pathing the route on its own. This addon is the front-end for that mode: it gives you " ..
    "one-click On / Off / Skip / Pause-Resume control, a live status readout (what the bot " ..
    "is doing and which boss it's heading for), and a boss list with a per-boss \"Go\" button. " ..
    "You must be in a party that contains a tank bot \226\128\148 the addon only relays commands.")

local optCmdHeader = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
optCmdHeader:SetPoint("TOPLEFT", optOverview, "BOTTOMLEFT", 0, -18)
optCmdHeader:SetText("Commands & Controls")
optCmdHeader:SetTextColor(0.24, 0.60, 1.0)

local optCmdList = optionsPanel:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
optCmdList:SetPoint("TOPLEFT", optCmdHeader, "BOTTOMLEFT", 0, -8)
optCmdList:SetWidth(560)
optCmdList:SetJustifyH("LEFT")
optCmdList:SetText(
    "|cffffd100/dc|r  \226\128\148  Toggle the main window (always reopens in full mode).\n" ..
    "|cffffd100On / Off|r  \226\128\148  Start or stop the autonomous clear.\n" ..
    "|cffffd100Skip|r  \226\128\148  Skip the current boss / objective and move to the next.\n" ..
    "|cffffd100Pause / Resume|r  \226\128\148  Hold the tank in place without ending the clear, then resume.\n" ..
    "|cffffd100Pull: Off / On / Dynamic|r  \226\128\148  LOS pull-to-camp control. |cff33d94dOn|r: the tank runs " ..
    "in to grab a pack and drags it back to where the party waits (passive) before everyone engages. " ..
    "|cff4db3ffDynamic|r: the tank scans each upcoming pack and auto-picks \226\128\148 it |cffffa61aLeeroys|r a lone " ..
    "pack (charges in) but uses the careful |cff4db3ffAdvanced|r pull when packs are bunched in a room. The live " ..
    "choice shows on the Dyn control. " ..
    "|cff8c8c8cOff|r: walk up and fight in place.\n" ..
    "|cffffd100Spectate|r  \226\128\148  Left-click detaches you into a free-flying camera while your " ..
    "character keeps running under bot AI. Right-click instead rides the tank (follow cam), handing off " ..
    "if it dies. Click again (or |cffffd100.dc spectate|r) to return to your body.\n" ..
    "|cffffd100< >|r (next to Spectate)  \226\128\148  Move the camera to any other bot in the instance, " ..
    "tank or not \226\128\148 healer during a wipe, DPS during a burn. Also starts the follow cam if it " ..
    "isn't running. |cffffd100.dc spectate next/prev/list|r, or " ..
    "|cffffd100.dc spectate follow <name>|r to jump straight to one.\n" ..
    "|cffffd100Go|r (per boss row)  \226\128\148  Send the tank straight to that boss (turns the clear on first).\n" ..
    "|cffffd100Tiny|r  \226\128\148  Collapse the window to a single-line, movable readout.\n" ..
    "|cffffd100Settings|r (sub-page)  \226\128\148  Override the server defaults (loot quality, rest %, " ..
    "party spread, pull tuning, …) for your own runs. Saved per character and re-applied each run.")

local openBtn = CreateFrame("Button", nil, optionsPanel, "UIPanelButtonTemplate")
openBtn:SetSize(160, 24)
openBtn:SetPoint("TOPLEFT", optCmdList, "BOTTOMLEFT", 0, -20)
openBtn:SetText("Open DungeonClear")
openBtn:SetScript("OnClick", function()
    -- Mirror the /dc (no-arg) open branch: always reopen in full (non-tiny) mode.
    DungeonClearDB.tinyMode = false
    UpdateLayout()
    frame:Show()
end)

if InterfaceOptions_AddCategory then
    InterfaceOptions_AddCategory(optionsPanel)
end

-- ===========================================================================
-- Settings sub-panel (Interface -> AddOns -> DungeonClear -> Settings)
-- ===========================================================================
-- Schema-driven per-player overrides. The server is the source of truth for
-- which settings exist and their type/range: each `sync` streams one SETTINGS
-- line per player-facing setting (key, value, min, max, type, overridden) and
-- this panel renders a control for each. A new setting added server-side shows
-- up here automatically with no addon change. Overrides are saved per character
-- in DungeonClearDB.settings and re-pushed to the server each run (it keeps them
-- only in memory, keyed to the leader tank).

-- Friendly labels + tooltips. Optional decoration only: any key missing here
-- still renders, falling back to the raw key as its label.
local SettingMeta = {
    PreventBotRelease    = { label = "Prevent Bot Release",
                             desc = "Dead bots stay as a corpse to be resurrected instead of releasing to the graveyard." },
    CombatRegroup        = { label = "Combat Regroup",
                             desc = "Keep followers grouped on the tank during a fight, not just on the route — a healer that drifts out of line of sight closes back in." },
    PartyMaxSpread       = { label = "Party Max Spread (yd)",
                             desc = "How far the tank may lead the party before it holds to let everyone catch up." },
    LootMinQuality       = { label = "Minimum Loot Quality",
                             desc = "Skip corpses whose best item is below this rarity. Quest items always loot." },
    IgnoreChests         = { label = "Ignore Chests",
                             desc = "Don't stop for treasure chests or other world objects while clearing — only loot creature corpses." },
    RestHealthPct        = { label = "Rest Health %",
                             desc = "Health the party eats up to between pulls, overriding the server's AiPlayerbot.AlmostFullHealth for this run. 0 = use the server default." },
    RestManaPct          = { label = "Rest Mana %",
                             desc = "Mana the party drinks up to between pulls, overriding the server's AiPlayerbot.HighMana for this run. 0 = use the server default. Ignored while Smart Rest is on." },
    SmartRest            = { label = "Smart Rest",
                             desc = "Push without stopping to eat or drink until someone falls below a trigger, then the whole party rests to FULL health and mana. Off = classic rest-to-target behavior (Rest Health/Mana % above)." },
    SmartRestHealthPct   = { label = "Smart Rest: Health Trigger %",
                             desc = "Any member below this health stops the party for a full rest. 0 disables the health trigger." },
    SmartRestDpsManaPct  = { label = "Smart Rest: DPS/Tank Mana Trigger %",
                             desc = "A DPS or tank mana user below this stops the party for a full rest. 0 disables." },
    SmartRestHealerManaPct = { label = "Smart Rest: Healer Mana Trigger %",
                             desc = "A healer below this mana stops the party for a full rest. 0 disables." },
    WaitAtBoss           = { label = "Wait at Boss",
                             desc = "Pause the run right before every boss pull and wait for you — hit Resume (or the tiny-mode dot) when your party is ready. Each boss waits once per run." },
    PullDynamicMaxLeeroyMobs = { label = "Desired maximum mobs per pull",
                             desc = "Dynamic pull only. The party's comfortable simultaneous-mob ceiling: the tank Leeroys a pack at or under this estimated aggro count, and pulls one above it back to camp." },
    PullDynamicPartyLag  = { label = "Pull: Party Lag (yd)",
                             desc = "Dynamic pull only. How far back the party trails while the tank scouts the next pack, so it reaches aggro range alone to decide Leeroy vs pull." },
}

-- The only settings exposed in the player-facing panel. The server streams many
-- more (and the conf file still tunes them all), but the rest are advanced
-- pathfinding/engage/pull-geometry knobs better left at the server default, so we
-- allowlist exactly the player-relevant ones here. UpsertSetting drops anything
-- not listed, so a live sync can't recreate a row for a setting we don't want.
local VisibleSettings = {
    PreventBotRelease        = true,
    IgnoreChests             = true,
    LootMinQuality           = true,
    RestHealthPct            = true,
    RestManaPct              = true,
    PartyMaxSpread           = true,
    PullDynamicMaxLeeroyMobs = true,
}


-- WoW item-quality id -> display name + color (used by the Minimum Loot Quality
-- dropdown). Mirrors the client's ITEM_QUALITY_COLORS / ITEM_QUALITYn_DESC but
-- hardcoded so the colored entries render identically regardless of locale.
local QualityInfo = {
    [0] = { name = "Poor",      hex = "ff9d9d9d" },
    [1] = { name = "Common",    hex = "ffffffff" },
    [2] = { name = "Uncommon",  hex = "ff1eff00" },
    [3] = { name = "Rare",      hex = "ff0070dd" },
    [4] = { name = "Epic",      hex = "ffa335ee" },
    [5] = { name = "Legendary", hex = "ffff8000" },
    [6] = { name = "Artifact",  hex = "ffe6cc80" },
}
local function QualityText(v)
    local info = QualityInfo[v] or QualityInfo[0]
    return "|c" .. info.hex .. info.name .. "|r"
end

-- Setting type ids mirror DcType in the server registry.
local DCT_BOOL, DCT_UINT, DCT_INT, DCT_FLOAT = 0, 1, 2, 3

-- Built-in fallback schema mirroring the server's DcSettingsRegistry. It lets
-- the panel render controls (with correct defaults/ranges) even with no live
-- sync yet — e.g. solo, or browsing the ESC menu outside a dungeon. A live
-- `sync` refines these with the server's real effective values and can add keys
-- not listed here, so the panel still auto-extends when the server gains a
-- setting; this table only needs touching to give a new setting nicer defaults.
local DefaultSchema = {
    PreventBotRelease        = { type = DCT_BOOL,  min = 0,  max = 1,  default = 1 },
    IgnoreChests             = { type = DCT_BOOL,  min = 0,  max = 1,  default = 1 },
    CombatRegroup            = { type = DCT_BOOL,  min = 0,  max = 1,  default = 1 },
    LootMinQuality           = { type = DCT_UINT,  min = 0,  max = 6,  default = 0 },
    RestHealthPct            = { type = DCT_UINT,  min = 0,  max = 100, default = 0 },
    RestManaPct              = { type = DCT_UINT,  min = 0,  max = 100, default = 0 },
    SmartRest                = { type = DCT_BOOL,  min = 0,  max = 1,  default = 0 },
    SmartRestHealthPct       = { type = DCT_UINT,  min = 0,  max = 100, default = 50 },
    SmartRestDpsManaPct      = { type = DCT_UINT,  min = 0,  max = 100, default = 10 },
    SmartRestHealerManaPct   = { type = DCT_UINT,  min = 0,  max = 100, default = 40 },
    WaitAtBoss               = { type = DCT_BOOL,  min = 0,  max = 1,  default = 0 },
    PartyMaxSpread           = { type = DCT_FLOAT, min = 10, max = 60, default = 25 },
    PullDynamicMaxLeeroyMobs = { type = DCT_UINT,  min = 1,  max = 20, default = 5 },
    PullDynamicPartyLag      = { type = DCT_FLOAT, min = 6,  max = 40, default = 15 },
}
local DefaultSchemaOrder = {
    "PreventBotRelease", "IgnoreChests",
    "LootMinQuality", "RestHealthPct", "RestManaPct",
    "PartyMaxSpread",
    "PullDynamicMaxLeeroyMobs",
}

local settingRows = {}     -- key -> row frame
local settingOrder = {}    -- insertion order for layout
local inSyncBatch = false

local function StepFor(stype) return stype == DCT_FLOAT and 0.5 or 1 end

local function RoundVal(stype, v)
    if stype == DCT_FLOAT then
        return math.floor(v * 2 + 0.5) / 2   -- snap to 0.5
    end
    return math.floor(v + 0.5)
end

local function FmtVal(stype, v)
    if stype == DCT_BOOL then return (v ~= 0) and "On" or "Off" end
    if stype == DCT_FLOAT then return string.format("%.1f", v) end
    return tostring(math.floor(v + 0.5))
end

-- Rest values use zero as an explicit "use the server default" sentinel.
-- Keep that sentinel out of the saved overrides, so it behaves like clicking
-- the row's Default button while the other controls can still save a value of
-- zero (for example LootMinQuality=0 or PreventBotRelease=0).
local function IsSavedDefault(key, value)
    return (key == "RestHealthPct" or key == "RestManaPct") and value == 0
end

local settingsPanel = CreateFrame("Frame", "DungeonClearSettingsPanel", UIParent)
settingsPanel.name = "Settings"
settingsPanel.parent = optionsPanel.name  -- nests under "DungeonClear"

local setTitle = settingsPanel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
setTitle:SetPoint("TOPLEFT", settingsPanel, "TOPLEFT", 16, -16)
setTitle:SetText("Dungeon Clear - Settings")
setTitle:SetTextColor(0.24, 0.60, 1.0)

local setIntro = settingsPanel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
setIntro:SetPoint("TOPLEFT", setTitle, "BOTTOMLEFT", 0, -4)
setIntro:SetWidth(580)
setIntro:SetJustifyH("LEFT")
setIntro:SetText(
    "These override the server defaults for your own dungeon runs. Changes apply " ..
    "immediately and are saved per character. You must be in a party with a tank " ..
    "bot for them to take effect; \"Default\" reverts a setting to the server value.")
setIntro:SetTextColor(0.6, 0.6, 0.6)

-- Reset-everything-to-server-default button.
local resetAllBtn = CreateFrame("Button", nil, settingsPanel, "UIPanelButtonTemplate")
resetAllBtn:SetSize(150, 22)
resetAllBtn:SetPoint("TOPLEFT", setIntro, "BOTTOMLEFT", 0, -10)
resetAllBtn:SetText("Reset All to Default")
resetAllBtn:SetScript("OnClick", function()
    DungeonClearDB.settings = {}
    SendDcCommand("reset", "", true)  -- empty key = clear the whole run
end)

-- Scroll area so the panel scales to any number of settings.
local setScroll = CreateFrame("ScrollFrame", "DungeonClearSettingsScroll", settingsPanel, "UIPanelScrollFrameTemplate")
setScroll:SetPoint("TOPLEFT", resetAllBtn, "BOTTOMLEFT", 0, -10)
setScroll:SetPoint("BOTTOMRIGHT", settingsPanel, "BOTTOMRIGHT", -28, 16)

local setContent = CreateFrame("Frame", "DungeonClearSettingsContent", setScroll)
setContent:SetSize(560, 10)
setScroll:SetScrollChild(setContent)

-- Control-type ordering for the panel: checkboxes, then dropdowns, then text
-- boxes, then sliders. Keeps like controls grouped regardless of insertion order.
local function ControlGroup(row)
    if row.stype == DCT_BOOL then return 1 end
    if row.isQuality then return 2 end
    return 4  -- slider
end

-- Position every known row top-to-bottom (grouped by control type) and size the
-- scroll child.
local function RelayoutSettings()
    -- Stable sort by control group, preserving insertion order within a group.
    local idx, keys = {}, {}
    for i, key in ipairs(settingOrder) do idx[key] = i end
    for _, key in ipairs(settingOrder) do
        if settingRows[key] then table.insert(keys, key) end
    end
    table.sort(keys, function(a, b)
        local ga, gb = ControlGroup(settingRows[a]), ControlGroup(settingRows[b])
        if ga ~= gb then return ga < gb end
        return idx[a] < idx[b]
    end)

    local y = -6
    for _, key in ipairs(keys) do
        local row = settingRows[key]
        if row then
            row:ClearAllPoints()
            row:SetPoint("TOPLEFT", setContent, "TOPLEFT", 6, y)
            row:SetPoint("RIGHT", setContent, "RIGHT", -6, 0)
            row:Show()
            -- Sliders carry min/max sublabels under the track, so they need extra
            -- room before the next row's title; other controls don't.
            y = y - (row.isSlider and 66 or 52)
        end
    end
    setContent:SetHeight(math.max(10, -y + 6))
end

-- Build a row's frame + control (control type fixed by the setting's type).
local function CreateSettingRow(key, stype)
    local meta = SettingMeta[key] or {}
    local row = CreateFrame("Frame", nil, setContent)
    row:SetSize(540, 48)
    row.key = key
    row.stype = stype

    row.label = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.label:SetPoint("TOPLEFT", row, "TOPLEFT", 4, -2)
    row.label:SetText(meta.label or key)
    row.label:SetTextColor(0.92, 0.92, 0.92)

    if meta.desc then
        row:EnableMouse(true)
        row:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_TOPLEFT")
            GameTooltip:SetText(meta.label or key, 1, 1, 1)
            GameTooltip:AddLine(meta.desc, 0.8, 0.8, 0.8, true)
            GameTooltip:Show()
        end)
        row:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end

    -- Per-row revert button, shown only while this setting is overridden.
    row.defBtn = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    row.defBtn:SetSize(64, 18)
    row.defBtn:SetPoint("TOPRIGHT", row, "TOPRIGHT", -2, -2)
    row.defBtn:SetText("Default")
    row.defBtn:SetScript("OnClick", function()
        DungeonClearDB.settings[key] = nil
        SendDcCommand("reset", key, true)
    end)
    row.defBtn:Hide()

    if stype == DCT_BOOL then
        local cb = CreateFrame("CheckButton", "DungeonClearCheck_" .. key, row, "UICheckButtonTemplate")
        cb:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 8, 0)
        cb:SetScript("OnClick", function(self)
            if row.updating then return end
            local v = self:GetChecked() and 1 or 0
            DungeonClearDB.settings[key] = v
            row.defBtn:Show()
            SendDcCommand("set", key .. "\t" .. v, true)
        end)
        row.control = cb
    elseif key == "LootMinQuality" then
        -- Loot rarity reads as named tiers, not a number, so a colored dropdown
        -- ("Common", "Rare", "Epic", …) is clearer than a 0-6 slider.
        local dd = CreateFrame("Frame", "DungeonClearDropdown_" .. key, row, "UIDropDownMenuTemplate")
        dd:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", -6, -2)
        UIDropDownMenu_SetWidth(dd, 130)
        local function OnSelect(self)
            local v = self.value
            UIDropDownMenu_SetSelectedValue(dd, v)
            UIDropDownMenu_SetText(dd, QualityText(v))
            if row.updating then return end
            DungeonClearDB.settings[key] = v
            row.defBtn:Show()
            SendDcCommand("set", key .. "\t" .. v, true)
        end
        UIDropDownMenu_Initialize(dd, function(self, level)
            for q = 0, 6 do
                local entry = UIDropDownMenu_CreateInfo()
                entry.text = QualityText(q)
                entry.value = q
                entry.func = OnSelect
                entry.checked = (UIDropDownMenu_GetSelectedValue(dd) == q)
                UIDropDownMenu_AddButton(entry, level)
            end
        end)
        row.control = dd
        row.isQuality = true
    else
        local s = CreateFrame("Slider", "DungeonClearSlider_" .. key, row, "OptionsSliderTemplate")
        s:SetWidth(300)
        s:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 10, 2)
        s:SetOrientation("HORIZONTAL")
        getglobal(s:GetName() .. "Text"):SetText("")  -- use our own label instead
        row.valText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        row.valText:SetPoint("LEFT", s, "RIGHT", 14, 0)
        row.valText:SetTextColor(1, 0.82, 0)
        row.isSlider = true
        s:SetScript("OnValueChanged", function(self, value)
            value = RoundVal(stype, value)
            row.valText:SetText(FmtVal(stype, value))
            if row.updating then return end
            if IsSavedDefault(key, value) then
                DungeonClearDB.settings[key] = nil
                row.defBtn:Hide()
            else
                DungeonClearDB.settings[key] = value
                row.defBtn:Show()
            end
            SendDcCommand("set", key .. "\t" .. value, true)
        end)
        row.control = s
    end

    return row
end

-- Create-or-update the row for one setting from a SETTINGS line / cache entry.
local function UpsertSetting(key, value, minV, maxV, stype, overridden)
    -- Show only the allowlisted player-facing settings, even if the server
    -- streams others in a live sync.
    if not VisibleSettings[key] then return end

    -- Cache the schema so the panel can render before any sync (e.g. at login).
    if not DungeonClearDB.schema[key] then
        DungeonClearDB.schema[key] = {}
        table.insert(DungeonClearDB.schemaOrder, key)
    end
    local sc = DungeonClearDB.schema[key]
    sc.min, sc.max, sc.type = minV, maxV, stype

    -- A sync reports the effective value for the current tank/run.  It is not
    -- safe to delete the local value when overridden=false: a fresh run clears
    -- the tank's in-memory flags before the addon re-pushes this character's
    -- saved settings, and a settings sync while DC is off has no run override
    -- to report.  Explicit Default / Reset controls clear DungeonClearDB.settings
    -- before sending their command, so normal syncs must leave it untouched.
    local saved = DungeonClearDB.settings[key]
    if not overridden and saved ~= nil then
        -- Keep the pending per-character value visible until the next explicit
        -- set is accepted by the server. This also avoids a settings panel
        -- briefly showing defaults while a fresh run is being initialized.
        value = saved
        overridden = true
    end

    local row = settingRows[key]
    if not row then
        row = CreateSettingRow(key, stype)
        settingRows[key] = row
        table.insert(settingOrder, key)
    end

    row.updating = true
    if stype == DCT_BOOL then
        row.control:SetChecked(value ~= 0)
    elseif row.isQuality then
        local v = math.floor(value + 0.5)
        if v < 0 then v = 0 elseif v > 6 then v = 6 end
        UIDropDownMenu_SetSelectedValue(row.control, v)
        UIDropDownMenu_SetText(row.control, QualityText(v))
    else
        row.control:SetMinMaxValues(minV, maxV)
        row.control:SetValueStep(StepFor(stype))
        getglobal(row.control:GetName() .. "Low"):SetText(FmtVal(stype, minV))
        getglobal(row.control:GetName() .. "High"):SetText(FmtVal(stype, maxV))
        row.control:SetValue(value)
        row.valText:SetText(FmtVal(stype, value))
    end
    row.updating = false

    if overridden then row.defBtn:Show() else row.defBtn:Hide() end
end

-- Assign the forward-declared hooks used by OnAddonMessage / ADDON_LOADED.
HandleSettingsLine = function(parts)
    local key = parts[2]
    local value = tonumber(parts[3])
    local minV = tonumber(parts[4])
    local maxV = tonumber(parts[5])
    local stype = tonumber(parts[6])
    local overridden = (parts[7] == "1")
    if not key or value == nil or stype == nil then return end
    UpsertSetting(key, value, minV, maxV, stype, overridden)
    if not inSyncBatch then RelayoutSettings() end
end

OnSettingsSyncBoundary = function(which)
    if which == "start" then
        inSyncBatch = true
    else
        inSyncBatch = false
        RelayoutSettings()
    end
end

PushSettings = function()
    if not DungeonClearDB.settings then return end
    for k, v in pairs(DungeonClearDB.settings) do
        if not IsSavedDefault(k, v) then
            SendDcCommand("set", k .. "\t" .. v, true)
        end
    end
end

BuildSettingsFromCache = function()
    local seen = {}
    local function render(key, stype, minV, maxV, defaultV)
        local v = DungeonClearDB.settings[key]
        if IsSavedDefault(key, v) then
            DungeonClearDB.settings[key] = nil
            v = nil
        end
        local overridden = (v ~= nil)
        if v == nil then v = (defaultV ~= nil) and defaultV or minV end
        UpsertSetting(key, v, minV, maxV, stype, overridden)
        seen[key] = true
    end

    -- Built-in settings first (correct defaults/ranges), preferring any cached
    -- min/max/type from a past sync but always using the built-in default value.
    for _, key in ipairs(DefaultSchemaOrder) do
        local d = DefaultSchema[key]
        local c = DungeonClearDB.schema[key]
        render(key,
            (c and c.type) or d.type,
            (c and c.min) or d.min,
            (c and c.max) or d.max,
            d.default)
    end

    -- Any extra keys a server sync advertised that aren't built in.
    for _, key in ipairs(DungeonClearDB.schemaOrder or {}) do
        local sc = DungeonClearDB.schema[key]
        if sc and sc.type and not seen[key] then
            render(key, sc.type, sc.min, sc.max, nil)
        end
    end

    RelayoutSettings()
end

-- Refresh whenever the panel is shown. First re-render every row from the cached
-- schema NOW that the panel is visible: InputBoxTemplate EditBoxes (the Rest %
-- fields) don't reliably display text set while their parent is hidden, so the
-- ADDON_LOADED population can leave them blank until re-applied on show. Then ask
-- the server for live effective values (silent "addon" param; a missing tank bot
-- no longer reaches chat, so there's nothing to suppress).
local function RefreshSettings()
    if BuildSettingsFromCache then BuildSettingsFromCache() end
    SendDcCommand("sync", "addon")
end
settingsPanel.refresh = RefreshSettings
settingsPanel:SetScript("OnShow", RefreshSettings)

if InterfaceOptions_AddCategory then
    InterfaceOptions_AddCategory(settingsPanel)
end

-- Minimap Button
-- Self-contained (no LibDBIcon dependency): a draggable button pinned to the
-- minimap edge. Left-click toggles the main window exactly like /dc; drag moves
-- it around the dial, with the angle persisted in DungeonClearDB.minimapPos.
local function ToggleMainWindow()
    if frame:IsVisible() then
        frame:Hide()
    else
        -- Always reopen in full (non-tiny) mode, matching the /dc behavior.
        DungeonClearDB.tinyMode = false
        UpdateLayout()
        frame:Show()
    end
end

local minimapButton = CreateFrame("Button", "DungeonClearMinimapButton", Minimap)
minimapButton:SetFrameStrata("MEDIUM")
minimapButton:SetFrameLevel(8)
minimapButton:SetSize(31, 31)
minimapButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
minimapButton:RegisterForDrag("LeftButton")

local mmOverlay = minimapButton:CreateTexture(nil, "OVERLAY")
mmOverlay:SetSize(53, 53)
mmOverlay:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
mmOverlay:SetPoint("TOPLEFT")

local mmIcon = minimapButton:CreateTexture(nil, "BACKGROUND")
mmIcon:SetSize(20, 20)
mmIcon:SetTexture("Interface\\Icons\\inv_misc_enggizmos_17")
mmIcon:SetPoint("CENTER", minimapButton, "CENTER", 0, 1)
mmIcon:SetTexCoord(0.07, 0.93, 0.07, 0.93)

local function UpdateMinimapButtonPosition()
    local angle = math.rad(DungeonClearDB.minimapPos or 200)
    minimapButton:ClearAllPoints()
    minimapButton:SetPoint("CENTER", Minimap, "CENTER",
        80 * math.cos(angle), 80 * math.sin(angle))
end

minimapButton:SetScript("OnDragStart", function(self)
    self:SetScript("OnUpdate", function()
        local mx, my = Minimap:GetCenter()
        local scale = Minimap:GetEffectiveScale()
        local px, py = GetCursorPosition()
        px, py = px / scale, py / scale
        DungeonClearDB.minimapPos = math.deg(math.atan2(py - my, px - mx))
        UpdateMinimapButtonPosition()
    end)
end)
minimapButton:SetScript("OnDragStop", function(self)
    self:SetScript("OnUpdate", nil)
end)

minimapButton:SetScript("OnClick", function(self, button)
    ToggleMainWindow()
end)

minimapButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:AddLine("Dungeon Clear")
    GameTooltip:AddLine("Left-click to toggle the window.", 1, 1, 1)
    GameTooltip:AddLine("Drag to reposition this button.", 1, 1, 1)
    GameTooltip:Show()
end)
minimapButton:SetScript("OnLeave", function() GameTooltip:Hide() end)

UpdateMinimapButtonPosition()

-- SavedVariables aren't restored until ADDON_LOADED fires (which happens after
-- this file has finished executing), so the call above positions the button
-- using the default angle. Re-apply the persisted angle once the DB is actually
-- loaded, otherwise the button snaps back to the default on every login.
local mmLoader = CreateFrame("Frame")
mmLoader:RegisterEvent("ADDON_LOADED")
mmLoader:SetScript("OnEvent", function(self, _, name)
    if name == AddonName then
        UpdateMinimapButtonPosition()
        self:UnregisterEvent("ADDON_LOADED")
    end
end)

-- Slash Command Registration
SLASH_DUNGEONCLEAR1 = "/dc"
SlashCmdList["DUNGEONCLEAR"] = function(msg)
    if msg == "" then
        if frame:IsVisible() then
            frame:Hide()
        else
            -- Always reopen in full (non-tiny) mode
            DungeonClearDB.tinyMode = false
            UpdateLayout()
            frame:Show()
        end
    else
        -- Parse "/dc <sub> [param]" and send via addon message
        local subCmd, param = msg:match("^(%S+)%s*(.*)$")
        if subCmd then
            if subCmd == "reset" and DungeonClearDB.settings then
                -- Keep slash-command reset consistent with the panel's Default
                -- buttons instead of replaying a value on the next run.
                local key = param:match("^(%S+)")
                if key and key ~= "" then
                    DungeonClearDB.settings[key] = nil
                else
                    DungeonClearDB.settings = {}
                end
            end
            SendDcCommand(subCmd, param)
            if (subCmd == "on" or subCmd == "go") and PushSettings then
                -- Both commands can start a fresh run; restore this character's
                -- saved settings after the server resets the tank state.
                PushSettings()
            end
        end
    end
end

-- Print loaded notice
DEFAULT_CHAT_FRAME:AddMessage("|cff3da6ffTortoise DungeonClear loaded.|r Type /dc to toggle window.")

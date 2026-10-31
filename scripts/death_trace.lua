-- KK/YDWE Lua engine module. Load with Cheat("exec-lua:death_trace").
-- This module records observations only; hooked natives always call the original.
local trace = { enabled = true, hooks = {}, death_event = false }
_G.War3DevDeathTrace = trace

local function record(kind, details)
    if not trace.enabled then return end
    local ok, err = pcall(function()
        -- The engine permits writes within the Warcraft installation directory.
        local file = assert(io.open('war3dev_death_trace.log', 'a'))
        file:write(os.date('%Y-%m-%d %H:%M:%S'), '\t', kind, '\t', details, '\n')
        file:close()
    end)
    if not ok then trace.last_error = tostring(err) end
end

local common = require 'jass.common'
local function unit_info(unit)
    if not unit then return 'nil' end
    local id, kind = 0, 0
    pcall(function() id = common.GetHandleId(unit) end)
    pcall(function() kind = common.GetUnitTypeId(unit) end)
    return 'handle=' .. tostring(id) .. ' type=' .. tostring(kind)
end

local hook = require 'jass.hook'
local function watch(name, describe)
    if type(common[name]) ~= 'function' then return end
    local ok, err = pcall(function()
        -- jass.hook passes the original native as the *last* argument.
        hook[name] = function(...)
            local count = select('#', ...)
            local args = {...}
            local original = args[count]
            local result = original(table.unpack(args, 1, count - 1))
            if trace.enabled then
                local details_ok, details = pcall(describe, args, result)
                record(name, details_ok and details or tostring(details))
            end
            return result
        end
    end)
    if ok then trace.hooks[#trace.hooks + 1] = name
    else record('HOOK_ERROR', name .. ': ' .. tostring(err)) end
end

watch('KillUnit', function(args) return unit_info(args[1]) end)
watch('CreateItem', function(args, item)
    return 'type=' .. tostring(args[1]) .. ' x=' .. tostring(args[2]) ..
           ' y=' .. tostring(args[3]) .. ' item=' .. tostring(item)
end)
watch('UnitAddItem', function(args, result)
    return unit_info(args[1]) .. ' item=' .. tostring(args[2]) .. ' result=' .. tostring(result)
end)

local ok_event, event_error = pcall(function()
    -- common.j defines EVENT_PLAYER_UNIT_DEATH as ConvertPlayerUnitEvent(20).
    local death = common.ConvertPlayerUnitEvent(20)
    local trigger = common.CreateTrigger()
    for player_id = 0, 15 do
        common.TriggerRegisterPlayerUnitEvent(trigger, common.Player(player_id), death, nil)
    end
    common.TriggerAddAction(trigger, function()
        local dying = common.GetTriggerUnit()
        local killer = common.GetKillingUnit()
        record('UNIT_DEATH', 'dying={' .. unit_info(dying) .. '} killer={' .. unit_info(killer) .. '}')
    end)
    trace.death_event = true
    trace.trigger = trigger
end)
if not ok_event then record('EVENT_ERROR', tostring(event_error)) end
record('READY', 'death_event=' .. tostring(trace.death_event) .. ' hooks=' .. table.concat(trace.hooks, ','))
return trace

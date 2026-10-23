-- Basarabia RP - Social Club (ca pe RAGE MP)
--   * fiecare jucator raporteaza numele de Social Club din joc
--   * se salveaza langa contul lui (KVP: brp_sc:<identificator discord>)
--   * ban pe Social Club:  brp_scban <NumeSocialClub>   /  brp_scunban <NumeSocialClub>
--   * vezi jucatorii:      brp_sclist
--   * alte resurse:        exports.brp_socialclub:GetSocialClub(source)

local current = {} -- [source] = nume

local function discordId(src)
  for _, id in ipairs(GetPlayerIdentifiers(src)) do
    if id:sub(1, 8) == 'discord:' then return id end
  end
  return GetPlayerIdentifier(src, 0) or ('src:' .. src)
end

local function isBanned(name)
  return name ~= '' and GetResourceKvpString('brp_scban:' .. name:lower()) ~= nil
end

RegisterNetEvent('brp_socialclub:report', function(name)
  local src = source
  if type(name) ~= 'string' then return end
  name = name:gsub('[^%w%._%-]', ''):sub(1, 32)

  current[src] = name
  local who = discordId(src)
  local prev = GetResourceKvpString('brp_sc:' .. who)

  if name ~= '' then
    SetResourceKvp('brp_sc:' .. who, name)
    if prev and prev ~= name then
      print(('[Social Club] ^3%s (%s) a intrat cu alt cont Social Club: %s (inainte: %s)^7'):format(GetPlayerName(src), who, name, prev))
    else
      print(('[Social Club] %s (%s) -> %s'):format(GetPlayerName(src), who, name))
    end
  else
    print(('[Social Club] ^3%s (%s): numele de Social Club nu a putut fi citit^7'):format(GetPlayerName(src), who))
  end

  if isBanned(name) then
    DropPlayer(src, 'Contul tau Social Club este banat pe Basarabia RP.')
  end
end)

AddEventHandler('playerDropped', function() current[source] = nil end)

exports('GetSocialClub', function(src) return current[tonumber(src)] end)

-- comenzi doar din consola serverului / admin (ace)
RegisterCommand('brp_sclist', function(src)
  if src ~= 0 and not IsPlayerAceAllowed(src, 'command') then return end
  for s, n in pairs(current) do print(('[%d] %s -> Social Club: %s'):format(s, GetPlayerName(s) or '?', n ~= '' and n or '(necunoscut)')) end
end, true)

RegisterCommand('brp_scban', function(src, args)
  local n = args[1]; if not n then return print('folosire: brp_scban <NumeSocialClub>') end
  SetResourceKvp('brp_scban:' .. n:lower(), os.date('%Y-%m-%d %H:%M'))
  print('Banat Social Club: ' .. n)
  for s, name in pairs(current) do if name:lower() == n:lower() then DropPlayer(s, 'Contul tau Social Club este banat pe Basarabia RP.') end end
end, true)

RegisterCommand('brp_scunban', function(src, args)
  local n = args[1]; if not n then return print('folosire: brp_scunban <NumeSocialClub>') end
  DeleteResourceKvp('brp_scban:' .. n:lower())
  print('Debanat Social Club: ' .. n)
end, true)

-- Basarabia RP - numele de Social Club (Rockstar) al jucatorului, citit din joc.
-- Ca pe RAGE MP: valoarea vine de la client, deci se foloseste pentru identificare si
-- ban, nu ca dovada de autentificare (aceea e logarea Discord + licenta verificata de joc).

local function socialClubName()
  -- SC_ACCOUNT_INFO_GET_NICKNAME
  local ok, name = pcall(Citizen.InvokeNative, 0x198D161F458ECC7F, Citizen.ResultAsString())
  if ok and type(name) == 'string' and name ~= '' then return name end
  return nil
end

CreateThread(function()
  -- asteptam ca jocul sa fie complet incarcat si logat la Social Club
  while not NetworkIsSessionStarted() do Wait(500) end
  local name, tries = nil, 0
  repeat
    name = socialClubName()
    tries = tries + 1
    if not name then Wait(2000) end
  until name or tries >= 15
  TriggerServerEvent('brp_socialclub:report', name or '')
end)

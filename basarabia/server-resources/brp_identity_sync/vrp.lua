-- ============================================================================
--  brp_identity_sync – rulează în contextul vRP 2
--
--  vRP 2 salvează identificatorii unui jucător (steam:, license:, discord: ...)
--  DOAR când îi creează contul. La login caută după ORICARE identificator.
--
--  Resursa asta adaugă, la fiecare intrare pe server, identificatorii care
--  lipsesc din vrp_user_ids. Astfel, când trecem pe clientul Basarabia RP
--  (cu login propriu prin Discord), jucătorii își găsesc automat contul.
--
--  Nu modifică nimic existent: dacă un identificator e deja legat de alt
--  cont, este ignorat (INSERT IGNORE pe cheia primară).
-- ============================================================================

local BrpIdentitySync = class("BrpIdentitySync", vRP.Extension)

-- Ce tipuri de identificatori sincronizăm. "ip:" NU (se schimbă, ar putea
-- lega conturi greșit).
local SYNC_PREFIXES = { "discord:", "steam:", "license:", "license2:", "fivem:", "xbl:", "live:" }

local function wanted(id)
  for _, prefix in ipairs(SYNC_PREFIXES) do
    if string.sub(id, 1, #prefix) == prefix then return true end
  end
  return false
end

function BrpIdentitySync:__construct()
  vRP.Extension.__construct(self)

  vRP:prepare("BRP/add_identifier_if_free",
    "INSERT IGNORE INTO vrp_user_ids(identifier, user_id) VALUES(@identifier, @user_id)")

  vRP:prepare("BRP/stats_users", "SELECT COUNT(*) AS n FROM vrp_users")
  vRP:prepare("BRP/stats_discord",
    "SELECT COUNT(DISTINCT user_id) AS n FROM vrp_user_ids WHERE identifier LIKE 'discord:%'")

  -- Comandă doar pentru consola serverului / txAdmin:  brp_identity_stats
  RegisterCommand("brp_identity_stats", function(source)
    if source ~= 0 then return end
    async(function()
      local users = tonumber(vRP:query("BRP/stats_users", {})[1].n) or 0
      local withDiscord = tonumber(vRP:query("BRP/stats_discord", {})[1].n) or 0
      local pct = users > 0 and math.floor(withDiscord * 1000 / users) / 10 or 0
      print(("[brp_identity_sync] Conturi: %d | cu Discord legat: %d (%.1f%%)"):format(users, withDiscord, pct))
    end)
  end, true)
end

BrpIdentitySync.event = {}

function BrpIdentitySync.event:playerJoin(user)
  local ids = GetPlayerIdentifiers(user.source) or {}
  local added = 0

  for _, id in ipairs(ids) do
    if wanted(id) then
      local affected = vRP:execute("BRP/add_identifier_if_free", { identifier = id, user_id = user.id })
      added = added + (tonumber(affected) or 0)
    end
  end

  if added > 0 then
    print(("[brp_identity_sync] user_id %d: %d identificator(i) nou(i) legat(i)"):format(user.id, added))
  end
end

vRP:registerExtension(BrpIdentitySync)

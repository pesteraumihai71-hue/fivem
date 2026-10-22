-- Încarcă vrp.lua în contextul vRP 2 (vezi documentația vRP 2, "Script loading")
local Proxy = module("vrp", "lib/Proxy")
local vRP = Proxy.getInterface("vRP")

vRP.loadScript("brp_identity_sync", "vrp")

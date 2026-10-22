fx_version 'cerulean'
game 'gta5'

name 'brp_identity_sync'
author 'Basarabia RP'
description 'Pregateste migrarea pe clientul Basarabia RP: leaga identificatorii lipsa (discord:, steam: ...) de contul vRP existent'
version '1.0.0'

dependency 'vrp'

server_scripts {
  '@vrp/lib/utils.lua',
  'server.lua'
}

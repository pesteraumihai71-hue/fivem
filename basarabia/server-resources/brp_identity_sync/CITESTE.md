# brp_identity_sync (vRP 2)

Pregătește trecerea jucătorilor pe clientul Basarabia RP, **fără să-și piardă conturile**.

## De ce
vRP 2 salvează identificatorii unui jucător (`discord:`, `steam:`, `license:` …) în
`vrp_user_ids` **doar când îi creează contul**, dar la login caută după **oricare** dintre ei.
Resursa adaugă, la fiecare intrare, identificatorii care lipsesc. Când clientul Basarabia RP
va avea login prin Discord, jucătorii își vor găsi automat contul vechi.

Nu șterge și nu schimbă nimic. Dacă un identificator e deja legat de alt cont, e ignorat.

## Instalare
1. Copiați folderul `brp_identity_sync` în `resources/` pe server.
2. În `server.cfg`, **după** `ensure vrp`:
   ```
   ensure brp_identity_sync
   ```
3. Restart la server.

## Verificare
În consola serverului / txAdmin:
```
brp_identity_stats
```
Afișează câte conturi au deja Discord-ul legat. Țintă: cât mai aproape de 100% pentru
jucătorii activi, înainte de trecerea pe clientul nou.

**Sfat:** cereți jucătorilor (anunț pe Discord) să intre pe server cu aplicația Discord
deschisă pe PC, ca FiveM să le transmită ID-ul de Discord.

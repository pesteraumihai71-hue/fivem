# Basarabia RP – server propriu, fără Cfx.re

## De ce
Serverul oficial Cfx (cel de pe ZAP) cere la intrare un bilet semnat de Cfx
(`cfxTicket2`), generat de o componentă **închisă** a FiveM. Clientul Basarabia RP
e construit din codul public, deci nu poate genera acel bilet → serverul îl refuză.

Soluția: server compilat din același cod (fork-ul vostru) + login propriu cu Discord.

| | Înainte (Cfx) | Acum (Basarabia RP) |
|---|---|---|
| Program server | FXServer oficial | FXServer compilat din fork-ul vostru |
| Login jucători | cont / bilet Cfx | Discord (resursa `brp_auth`) |
| Cheie licență (`sv_licenseKey`) | obligatorie | nu mai e folosită |
| Listă servere Cfx | da | nu |
| txAdmin | da | **nu** – serverul pornește direct cu `server.cfg` |
| Resurse, vRP 2, baza de date | – | **neschimbate** |

## 1. Hosting
Pachetul de joc ZAP-Hosting pornește doar versiunile oficiale FiveM.
Pentru serverul propriu e nevoie de un **VPS** (sau server dedicat) cu Linux, de ex.
Ubuntu 22.04/24.04, minim 4 vCPU și 8–16 GB RAM (ZAP-Hosting vinde și VPS).

**Instalare automată (recomandat)** – pe serverul nou, ca root:
```bash
curl -fsSL https://raw.githubusercontent.com/pesteraumihai71-hue/fivem/master/basarabia/vps/install.sh -o install.sh
sudo bash install.sh
```
Instalează serverul, baza de date MariaDB, firewall-ul (portul SSH rămâne deschis),
generează `server.cfg` cu secretele și pornește serverul ca serviciu (`basarabia`).
Rularea din nou actualizează serverul fără să atingă `server.cfg`, resursele sau baza de date.

Manual, serverul se instalează exact ca un artifact Linux oficial:
```bash
mkdir -p ~/basarabia/server && cd ~/basarabia/server
# descărcați basarabia-server-linux.tar.xz din Releases (server-<versiune>)
tar xf basarabia-server-linux.tar.xz
./run.sh +exec server.cfg      # din folderul cu resources/ și server.cfg
```
Deschideți în firewall portul serverului (TCP+UDP, ex. 30120).


## 2. Aplicația Discord (o singură dată)
1. https://discord.com/developers/applications → **New Application** → „Basarabia RP”.
2. **OAuth2** → copiați **Client ID** și **Client Secret** (Reset Secret).
3. **OAuth2 → Redirects** → adăugați exact:
   `https://pesteraumihai71-hue.github.io/fivem/auth/`
4. Salvați.

## 3. server.cfg
```cfg
# --- Basarabia RP: login propriu cu Discord (fara Cfx) ---
set brp_authSecret "PUNETI-AICI-UN-SECRET-LUNG-ALEATOR-DE-MINIM-32-CARACTERE"
set brp_discordClientId "CLIENT_ID_DISCORD"
set brp_discordClientSecret "CLIENT_SECRET_DISCORD"
set brp_auth_redirectUri "https://pesteraumihai71-hue.github.io/fivem/auth/"
set brp_auth_publicUrl "http://IP-UL-SERVERULUI:30120"
set brp_auth_ticketDays 30

# --- verificarea licentei GTA V (fara Cfx) ---
set brp_ownership "steam,rockstar,epic"   # Steam activ; Rockstar/Epic apar „în curând”
set brp_steamApiKey "CHEIE_STEAM_WEB_API"  # https://steamcommunity.com/dev/apikey
set brp_requireOwnership false             # true = intră doar cine are licența verificată

ensure brp_auth
ensure vrp
ensure brp_identity_sync
```
- `brp_authSecret`: generați unul, de ex. pe Linux `openssl rand -hex 32`. **Nu-l publicați.**
  Dacă îl schimbați, toți jucătorii trebuie să se logheze din nou (automat).
- **Nu** mai e nevoie de `sv_licenseKey`.

### Verificarea licenței GTA V
- **Steam**: API-ul oficial Steam. Jucătorul se loghează cu Steam; profilul trebuie să aibă
  „Game details: Public”. Merge pentru GTA V Legacy (271590) și Enhanced (3240220).
- **Rockstar Games Launcher / Epic**: sloturi pregătite în `brp_auth`; se activează când
  Rockstar/Epic oferă acces oficial de verificare a licenței.
- Cu `brp_requireOwnership true`, serverul acceptă doar bilete cu licența verificată.

## 4. Cum intră un jucător
1. Pornește Basarabia RP → se deschide browserul „Login cu Discord”.
2. Acceptă → pagina deschide singură aplicația → intră pe server.
3. Biletul e ținut 30 de zile; după aceea, login din nou (un click).

Contul vRP 2 se găsește după `discord:<id>` – de aceea rulați **acum**, pe serverul
de pe ZAP, resursa `brp_identity_sync`, ca fiecare cont să aibă Discord-ul legat.

## 5. Mutarea de pe ZAP pe VPS
1. Backup din panoul ZAP: folderul `resources` + `server.cfg` (FTP) și baza de date
   (export din phpMyAdmin).
2. Pe VPS: MariaDB/MySQL, importați baza, copiați resursele, actualizați datele de
   conectare la DB în `server.cfg`.
3. Testați cu clientul Basarabia RP; abia apoi anunțați jucătorii.

## Adresa serverului
Clientul și pagina de login conțin adresa serverului:
- `code/client/shared/BasarabiaConfig.h` → `BRP_SERVER_ADDRESS`, `BRP_AUTH_LOGIN_URL_W`
- site, ramura `gh-pages` → `auth/index.html` → `SERVER`
La mutarea pe VPS, adresa nouă se schimbă în aceste locuri și se face un build nou.

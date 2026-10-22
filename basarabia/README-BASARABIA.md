# Basarabia RP – clientul propriu

Fork al clientului Cfx.re (FiveM), rebranduit pentru **Basarabia Roleplay**.
Serverul vostru (FXServer, scripturi, resurse, baza de date) **nu se schimbă deloc**.

---

## 1. Ce face deja clientul

| Ce | Unde e în cod |
|---|---|
| Toată configurarea (nume, IP, protocol, update-uri) într-un singur fișier | `code/client/shared/BasarabiaConfig.h` |
| Se numește **Basarabia RP** (fereastră, meniul de pauză, erori, scurtături) | `LauncherConfig.h`, `RenderHooks.cpp`, `NetLibrary.cpp`, `CustomText.cpp`, `GameInput.cpp`, `GtaGameInterface.cpp`, `MiniDump.cpp`, `UrlConfirmationModal.cpp` |
| Executabilul se numește `BasarabiaRP.exe` | `client/launcher/premake5.lua`, `client/console/premake5.lua` |
| Se instalează separat de FiveM: `%LOCALAPPDATA%\Basarabia RP\` (nu se bate cap în cap cu un FiveM oficial instalat pe același PC) | `Installer.cpp`, `Utils.Win32.cpp` |
| **Se conectează automat** la serverul vostru la pornire | `components/glue/src/ConnectToNative.cpp` |
| Link-uri proprii pentru site/Discord: `basarabiarp://connect/ip:port` | `LauncherConfig.h` |
| **NU** se mai actualizează de pe serverele Cfx.re (altfel și-ar suprascrie modificările cu FiveM-ul oficial) | `Bootstrap.cpp` |
| Iconiță proprie (provizorie, „BRP”) | `client/launcher/fivem.ico` |
| Installer `BasarabiaRP-Setup.exe` care verifică dacă e instalat GTA V | `basarabia/installer.iss` |

---

## 2. Înainte de primul build – OBLIGATORIU

Deschideți `code/client/shared/BasarabiaConfig.h` și puneți adresa serverului:

```cpp
#define BRP_SERVER_ADDRESS  "123.45.67.89:30120"   // sau "cfx.re/join/abc123"
```

Opțional: înlocuiți `code/client/launcher/fivem.ico` cu logo-ul vostru
(fișier `.ico` cu mărimi 16–256 px; numele fișierului rămâne `fivem.ico`).

---

## 3. Build (pe Windows)

**Instalați:** Visual Studio 2022 (Desktop development with C++ + Windows SDK),
Git, Python 3, Node.js LTS + Yarn, .NET SDK. Minim ~100 GB liberi pe SSD.

Dacă aveți codul ca patch (`basarabia-rp.patch`):

```bat
git clone https://github.com/citizenfx/fivem.git -c core.symlinks=true -c core.autocrlf=false basarabia-client
cd basarabia-client
git checkout -b basarabia-rp 4c4fe28
git apply C:\cale\catre\basarabia-rp.patch
git add -A
git commit -m "Basarabia RP"
git submodule update --init
```

- `core.autocrlf=false` e important: fără el, Git pe Windows schimbă capetele de linie și patch-ul nu se aplică.
- `4c4fe28` e versiunea pe care a fost testat patch-ul. După ce merge, puteți aduce codul la zi (secțiunea 6).
- Avertismentele „trailing whitespace” la `git apply` sunt normale.
- Recomandat: urcați rezultatul într-un repo **privat** pe GitHub, ca să lucrați toți pe el.

Apoi urmați `docs/building.md` din repo (pașii de mai jos sunt cei din ghidul oficial;
dacă unul dă eroare de cale, verificați acolo din ce folder se rulează):

```bat
cd code
fxd get-chrome
prebuild
fxd gen -game five
```

Deschideți soluția generată în `code\build\five\` cu Visual Studio,
alegeți configurația **Release** și dați **Build Solution**.

Rezultatul apare în `code\bin\five\release\` → acolo e `BasarabiaRP.exe`.

### Pachetul de UI (meniul principal)
Meniul principal și ecranul de încărcare se construiesc separat:

```bat
cd ext\ui-build
build.cmd five
```

Verificați în `docs/building.md` unde trebuie copiat rezultatul în `bin\five\release`.

---

## 4. Test

1. Rulați `code\bin\five\release\BasarabiaRP.exe`.
2. La prima pornire alegeți folderul GTA V.
3. Trebuie să intre **direct** pe server, fără server browser.
4. Testați și un link: `basarabiarp://connect/IP:PORT` (Win+R).

Trimiteți-mi orice eroare (text sau captură) – o repar.

---

## 5. Installerul pentru jucători

1. Instalați **Inno Setup 6** (gratuit, jrsoftware.org).
2. Deschideți `basarabia/installer.iss` → **Build → Compile**.
3. Rezultat: `basarabia/output/BasarabiaRP-Setup.exe` – asta urcați pe site.

**Recomandat:** semnați `BasarabiaRP.exe` și `BasarabiaRP-Setup.exe` cu un certificat
de code signing, altfel Windows SmartScreen afișează „Windows protected your PC”.

---

## 6. Update-uri

**Acum (simplu):** updater-ul automat e oprit. Pentru un update: recompilați,
faceți un installer nou (creșteți `AppVersion` în `installer.iss`), îl urcați pe site.
Jucătorii îl reinstalează peste versiunea veche.

**Mai târziu (automat):** decomentați `BRP_UPDATER_URL` în `BasarabiaConfig.h`
și găzduiți pe serverul vostru același format ca CDN-ul Cfx
(`heads/<canal>/<branch>` + obiecte pe hash, vezi `client/launcher/Updater.cpp`).
Pot construi eu scriptul care generează aceste fișiere din folderul de build.

### Când Rockstar scoate un patch GTA V
Clientul trebuie actualizat din upstream, altfel poate crăpa:

```bat
git remote add upstream https://github.com/citizenfx/fivem.git
git fetch upstream
git merge upstream/master
```

Rezolvați eventualele conflicte (modificările noastre sunt mici și marcate
cu `Basarabia RP` / `BRP_`), recompilați, testați, scoateți installer nou.

---

## 7. Ce NU e făcut încă (etapa 2)

- **Meniul principal** (server browser, știri, setări) vine dintr-un pachet precompilat
  descărcat de `ext/ui-build/build.cmd` și încă arată brandul FiveM. Îl înlocuim
  cu un meniu propriu Basarabia RP (doar butoane: Joacă, Setări, Discord, Ieșire).
- **Autentificarea** jucătorilor trece în continuare prin infrastructura Cfx.re
  (`client/shared/CnlEndpoint.h`). Acoperită de acordul vostru – nu am atins-o.
- **Scripturile cu Asset Escrow** (Tebex/Keymaster) se decriptează prin Cfx.re –
  testați-le pe acest client înainte de lansare.
- Fișierele interne `CitizenFX.FiveM.dll` și procesele `FiveM_*` păstrează intenționat
  numele, ca să nu se strice scripturile C# și compatibilitatea cu serverul.

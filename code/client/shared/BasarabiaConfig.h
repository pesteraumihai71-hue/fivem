/*
 * Basarabia Roleplay - configurare centrală a clientului.
 *
 * TOT ce ține de brand și de server se schimbă DOAR aici.
 * După orice modificare: recompilați clientul (Release).
 */

#pragma once

// ---------------------------------------------------------------------------
// 1. Numele afișat al produsului (titlul ferestrei, mesaje de eroare, meniu)
// ---------------------------------------------------------------------------
#define BRP_PRODUCT_NAME_A  "Basarabia RP"
#define BRP_PRODUCT_NAME_W  L"Basarabia RP"

// Textul lung din bara de titlu a ferestrei jocului
#define BRP_WINDOW_TITLE_W  L"Basarabia Roleplay"

// Numele INTERN al folderului cu datele clientului (langa BasarabiaRP.exe).
// NU il schimbati: legitimacy.dll (componenta inchisa, precompilata de Cfx.re,
// care verifica licenta GTA V prin Rockstar) are "FiveM.app" scris in cod si
// crapa (legitimacy.dll+2BAAE6) daca folderul are alt nume.
// Jucatorii vad oricum doar numele "Basarabia RP" de mai sus.
#define BRP_APP_DIR_W       L"FiveM.app"

// ---------------------------------------------------------------------------
// 2. Serverul la care clientul se conectează AUTOMAT la pornire.
//    Acceptă "ip:port" (ex. "123.45.67.89:30120") sau "cfx.re/join/abc123".
//    Lăsat gol ("") => clientul pornește în meniu, fără conectare automată.
// ---------------------------------------------------------------------------
#define BRP_SERVER_ADDRESS  "134.255.225.49:30178"

// Pagina de login cu Discord (servita de resursa `brp_auth` de pe serverul vostru).
// Daca schimbati IP-ul/portul serverului, schimbati si aici.
#define BRP_AUTH_LOGIN_URL_W L"http://134.255.225.49:30178/brp_auth/login"

// ---------------------------------------------------------------------------
// 3. Protocolul de link pentru site/Discord: basarabiarp://connect/ip:port
//    (doar litere mici, fără spații)
// ---------------------------------------------------------------------------
#define BRP_LINK_PROTOCOL_W L"basarabiarp"

// ---------------------------------------------------------------------------
// 4. Update-uri automate.
//    IMPORTANT: clientul original se actualizează de pe content.cfx.re și
//    și-ar SUPRASCRIE modificările voastre cu FiveM-ul oficial.
//
//    - Fără BRP_UPDATER_URL definit: updater-ul e DEZACTIVAT. Update-urile
//      se fac reinstalând installerul nou de pe site (recomandat la început).
//    - Cu BRP_UPDATER_URL definit: clientul ia update-urile de pe serverul
//      vostru, în același format ca CDN-ul Cfx (heads/<canal>/<branch> +
//      obiecte pe hash). Vezi README-BASARABIA.md, secțiunea "Update-uri".
// ---------------------------------------------------------------------------
// #define BRP_UPDATER_URL "https://update.basarabiarp.md/updates"

// Numele canalului de update pe serverul vostru (folosit doar cu BRP_UPDATER_URL)
#define BRP_CONTENT_NAME    "basarabiarp"

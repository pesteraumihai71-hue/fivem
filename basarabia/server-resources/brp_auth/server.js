// ============================================================================
//  brp_auth – login Basarabia RP (Discord) + verificare licenta GTA V
//
//  Flux:
//   1. Clientul deschide in browser  http://SERVER:PORT/brp_auth/login
//   2. Login cu Discord (scope "identify"); Discord -> pagina HTTPS de pe site
//      (brp_auth_redirectUri), care trimite code+state inapoi la /callback
//   3. Daca e activata verificarea licentei (brp_ownership), jucatorul isi
//      confirma GTA V pe o platforma (Steam acum; Rockstar/Epic: sloturi gata
//      pentru accesul oficial de la Rockstar/Epic)
//   4. Semnam biletul si deschidem basarabiarp://brp-auth/?t=<bilet>
//
//  Biletul: base64url(json) "." hex(HMAC-SHA256(brp_authSecret, base64url(json)))
//    json: { v:1, d:<discord id>, u:<nume>, e:<expirare>,
//            g:1 daca licenta GTA V e verificata, p:<platforma>, sh:<steam hex> }
//  Il verifica serverul (BasarabiaIdentityProvider).
//
//  server.cfg:
//    set brp_authSecret "<secret lung, aleator>"          (NU il publicati)
//    set brp_discordClientId "<Client ID>"
//    set brp_discordClientSecret "<Client Secret>"
//    set brp_auth_redirectUri "https://pesteraumihai71-hue.github.io/fivem/auth/"
//    set brp_auth_publicUrl "http://IP:PORT"               (adresa serverului)
//    set brp_auth_ticketDays 30
//
//    # verificarea licentei GTA V (fara Cfx):
//    set brp_ownership "steam"                (gol = dezactivat)
//    set brp_steamApiKey "<cheie Steam Web API>"   https://steamcommunity.com/dev/apikey
//    set brp_requireOwnership true            (serverul refuza fara licenta verificata)
//
//    ensure brp_auth
// ============================================================================

const crypto = require('crypto');
const https = require('https');
const querystring = require('querystring');

const RESOURCE = GetCurrentResourceName();

// GTA V pe Steam: Legacy + Enhanced
const STEAM_GTA_APPIDS = [271590, 3240220];

function cfg(name, fallback = '') {
    const v = GetConvar(name, fallback);
    return typeof v === 'string' ? v.trim() : fallback;
}

function publicUrl() {
    return cfg('brp_auth_publicUrl').replace(/\/+$/, '');
}

// ---------------------------------------------------------------------------
// stari temporare (10 minute): OAuth Discord + sesiuni de verificare licenta
// ---------------------------------------------------------------------------
const TTL_MS = 10 * 60 * 1000;
const pendingStates = new Map();   // state Discord -> creat
const sessions = new Map();        // sess -> { discordId, name, created }

setInterval(() => {
    const now = Date.now();
    for (const [k, t] of pendingStates) if (now - t > TTL_MS) pendingStates.delete(k);
    for (const [k, s] of sessions) if (now - s.created > TTL_MS) sessions.delete(k);
}, 60 * 1000);

function newId() {
    return crypto.randomBytes(24).toString('hex');
}

// ---------------------------------------------------------------------------
// utilitare
// ---------------------------------------------------------------------------
function b64url(buf) {
    return Buffer.from(buf).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function signTicket(identity, ownership) {
    const secret = cfg('brp_authSecret');
    const days = Math.max(1, Math.min(90, parseInt(cfg('brp_auth_ticketDays', '30'), 10) || 30));

    const data = {
        v: 1,
        d: String(identity.discordId),
        u: String(identity.name || '').slice(0, 64),
        e: Math.floor(Date.now() / 1000) + days * 24 * 3600,
    };

    if (ownership && ownership.verified) {
        data.g = 1;
        data.p = ownership.platform;
        if (ownership.steamHex) data.sh = ownership.steamHex;
    }

    const payload = b64url(JSON.stringify(data));
    const sig = crypto.createHmac('sha256', secret).update(payload).digest('hex');
    return `${payload}.${sig}`;
}

function request(method, url, headers, body) {
    return new Promise((resolve, reject) => {
        const req = https.request(url, { method, headers, timeout: 15000 }, (res) => {
            let data = '';
            res.on('data', (c) => { data += c; });
            res.on('end', () => {
                let json = null;
                try { json = JSON.parse(data || '{}'); } catch (e) { /* text */ }
                resolve({ status: res.statusCode, json: json || {}, text: data });
            });
        });
        req.on('timeout', () => req.destroy(new Error('Timeout')));
        req.on('error', reject);
        if (body) req.write(body);
        req.end();
    });
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function page(title, message, extraHead = '', buttons = '') {
    return `<!doctype html><html lang="ro"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)} – Basarabia RP</title>${extraHead}
<style>
 body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#0f1219;color:#c8ccd4;font:16px/1.5 "Segoe UI",Arial,sans-serif;padding:16px}
 .box{max-width:480px;text-align:center}
 h1{color:#f3f4f6;font-size:24px;margin:0 0 10px}
 .btns{display:flex;flex-direction:column;gap:10px;margin-top:20px}
 a.btn{display:block;background:#e3192d;color:#fff;font-weight:700;padding:12px 22px;border-radius:6px;text-decoration:none}
 span.btn{display:block;background:#1d212b;color:#7b808b;padding:12px 22px;border-radius:6px}
</style></head><body><div class="box"><h1>${escapeHtml(title)}</h1><p>${message}</p>${buttons ? `<div class="btns">${buttons}</div>` : ''}</div></body></html>`;
}

function send(res, status, html, headers = {}) {
    res.writeHead(status, Object.assign({
        'Content-Type': 'text/html; charset=utf-8',
        'Cache-Control': 'no-store',
    }, headers));
    res.send(html);
}

function redirect(res, url) {
    send(res, 302, '', { Location: url });
}

// ---------------------------------------------------------------------------
// verificarea licentei GTA V – platforme
//   Fiecare platforma: { name, ready(), start(sess, res), callback(query, res) }
// ---------------------------------------------------------------------------
const platforms = {
    // Steam: API oficial Steam (OpenID + IPlayerService/GetOwnedGames)
    steam: {
        name: 'Steam',
        ready: () => !!cfg('brp_steamApiKey') && !!publicUrl(),
        missing: () => [!cfg('brp_steamApiKey') && 'brp_steamApiKey', !publicUrl() && 'brp_auth_publicUrl'].filter(Boolean),

        start(sess, res) {
            const returnTo = `${publicUrl()}/${RESOURCE}/own/steam/callback?sess=${sess}`;
            redirect(res, 'https://steamcommunity.com/openid/login?' + querystring.stringify({
                'openid.ns': 'http://specs.openid.net/auth/2.0',
                'openid.mode': 'checkid_setup',
                'openid.return_to': returnTo,
                'openid.realm': publicUrl(),
                'openid.identity': 'http://specs.openid.net/auth/2.0/identifier_select',
                'openid.claimed_id': 'http://specs.openid.net/auth/2.0/identifier_select',
            }));
        },

        // returneaza { verified, platform, steamHex } sau { error }
        async callback(query) {
            // raspunsul trebuie sa fie pentru sesiunea curenta, pe serverul nostru
            const expectedReturn = `${publicUrl()}/${RESOURCE}/own/steam/callback?sess=${query.sess}`;
            if (String(query['openid.return_to'] || '') !== expectedReturn || query['openid.mode'] !== 'id_res') {
                return { error: 'Răspuns Steam invalid pentru această sesiune.' };
            }

            const claimed = String(query['openid.claimed_id'] || '');
            const m = claimed.match(/^https:\/\/steamcommunity\.com\/openid\/id\/(\d{17})$/);
            if (!m) return { error: 'Răspuns Steam invalid.' };

            // 1) confirmam la Steam ca raspunsul e autentic
            const check = Object.assign({}, query, { 'openid.mode': 'check_authentication' });
            delete check.sess;
            const verify = await request('POST', 'https://steamcommunity.com/openid/login', {
                'Content-Type': 'application/x-www-form-urlencoded',
            }, querystring.stringify(check));

            if (!/is_valid\s*:\s*true/.test(verify.text || '')) {
                return { error: 'Steam nu a confirmat login-ul. Încearcă din nou.' };
            }

            // 2) detine GTA V?
            const steamId = m[1];
            const url = 'https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/?' + querystring.stringify({
                key: cfg('brp_steamApiKey'),
                steamid: steamId,
                include_played_free_games: 1,
                include_appinfo: 0,
            }) + STEAM_GTA_APPIDS.map((id, i) => `&appids_filter[${i}]=${id}`).join('');

            const owned = await request('GET', url, {});
            const resp = owned.json && owned.json.response;

            if (owned.status !== 200 || !resp) {
                return { error: 'Nu am putut verifica biblioteca Steam. Încearcă din nou.' };
            }

            // profil privat: Steam nu trimite lista de jocuri
            if (resp.games === undefined && resp.game_count === undefined) {
                return {
                    error: 'Profilul tău Steam e privat. Setează în Steam: Profil → Edit Profile → Privacy Settings → '
                         + '„Game details: Public”, apoi încearcă din nou.',
                };
            }

            const games = Array.isArray(resp.games) ? resp.games : [];
            const hasGta = games.some((g) => STEAM_GTA_APPIDS.includes(Number(g.appid)));

            if (!hasGta) {
                return { error: 'Contul Steam nu deține GTA V. Folosește contul cu care ai cumpărat jocul.' };
            }

            return { verified: true, platform: 'steam', steamHex: BigInt(steamId).toString(16) };
        },
    },

    // Rockstar Games Launcher / Social Club – slot pregatit.
    // Necesita acces OFICIAL de la Rockstar (API de verificare a licentei sau
    // credentiale de partener). Nu exista API public; nu se implementeaza prin
    // copierea sistemului de login Rockstar.
    rockstar: {
        name: 'Rockstar Games Launcher',
        ready: () => false,
        missing: () => ['acces oficial Rockstar (API de verificare licență)'],
    },

    // Epic Games Store – slot pregatit (Epic Account Services + verificare
    // licenta; necesita aplicatie/acord cu Epic pentru produsul GTA V).
    epic: {
        name: 'Epic Games',
        ready: () => false,
        missing: () => ['acces oficial Epic (verificare licență GTA V)'],
    },
};

function ownershipMode() {
    return cfg('brp_ownership').toLowerCase().split(',').map((s) => s.trim()).filter((s) => platforms[s]);
}

// ---------------------------------------------------------------------------
// finalizare: bilet + link inapoi in aplicatie
// ---------------------------------------------------------------------------
function finish(res, identity, ownership) {
    const ticket = signTicket(identity, ownership);
    const link = `basarabiarp://brp-auth/?t=${encodeURIComponent(ticket)}`;

    console.log(`[brp_auth] Login reusit: discord:${identity.discordId} (${identity.name})`
        + (ownership && ownership.verified ? ` | GTA V verificat (${ownership.platform})` : ''));

    send(res, 200, page(
        'Gata, ești logat',
        `Salut, <b>${escapeHtml(identity.name)}</b>! Te poți întoarce în Basarabia RP – te conectezi automat.`,
        `<meta http-equiv="refresh" content="0;url=${escapeHtml(link)}">`,
        `<a class="btn" href="${escapeHtml(link)}">Deschide Basarabia RP</a>`
    ));
}

function chooseOwnership(res, sess) {
    const enabled = ownershipMode();
    const buttons = enabled.map((key) => {
        const p = platforms[key];
        return p.ready()
            ? `<a class="btn" href="/${RESOURCE}/own/${key}/start?sess=${sess}">Confirm GTA V prin ${escapeHtml(p.name)}</a>`
            : `<span class="btn">${escapeHtml(p.name)} – în curând</span>`;
    }).join('');

    send(res, 200, page(
        'Confirmă că ai GTA V',
        'Basarabia RP verifică licența GTA V direct la platforma de unde ai cumpărat jocul.',
        '',
        buttons
    ));
}

// ---------------------------------------------------------------------------
// Discord
// ---------------------------------------------------------------------------
async function handleDiscordCallback(query, res) {
    const { code, state } = query;

    if (!code || !state || !pendingStates.has(state)) {
        send(res, 400, page('Link expirat', 'Sesiunea de login a expirat sau nu e validă. Închide pagina și pornește din nou Basarabia RP.'));
        return;
    }
    pendingStates.delete(state);

    const token = await request('POST', 'https://discord.com/api/oauth2/token', {
        'Content-Type': 'application/x-www-form-urlencoded',
    }, querystring.stringify({
        client_id: cfg('brp_discordClientId'),
        client_secret: cfg('brp_discordClientSecret'),
        grant_type: 'authorization_code',
        code,
        redirect_uri: cfg('brp_auth_redirectUri'),
    }));

    if (token.status !== 200 || !token.json.access_token) {
        console.log(`[brp_auth] Discord token error ${token.status}: ${token.text}`);
        send(res, 502, page('Eroare Discord', 'Nu am putut confirma contul de Discord. Încearcă din nou.'));
        return;
    }

    const me = await request('GET', 'https://discord.com/api/users/@me', {
        Authorization: `Bearer ${token.json.access_token}`,
    });

    if (me.status !== 200 || !/^\d+$/.test(String(me.json.id || ''))) {
        send(res, 502, page('Eroare Discord', 'Nu am putut citi contul de Discord. Încearcă din nou.'));
        return;
    }

    const identity = { discordId: me.json.id, name: me.json.global_name || me.json.username };

    if (ownershipMode().length === 0) {
        finish(res, identity, null);
        return;
    }

    const sess = newId();
    sessions.set(sess, Object.assign({ created: Date.now() }, identity));
    chooseOwnership(res, sess);
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
function configError() {
    const missing = ['brp_authSecret', 'brp_discordClientId', 'brp_discordClientSecret', 'brp_auth_redirectUri']
        .filter((n) => !cfg(n));
    if (cfg('brp_authSecret') && cfg('brp_authSecret').length < 32) missing.push('brp_authSecret (minim 32 caractere)');
    for (const key of ownershipMode()) {
        if (key === 'steam') missing.push(...platforms.steam.missing());
    }
    return missing.length ? missing : null;
}

function fail(res, e) {
    console.log(`[brp_auth] Eroare: ${e && e.message}`);
    send(res, 500, page('Eroare', 'A apărut o eroare la login. Încearcă din nou.'));
}

SetHttpHandler((req, res) => {
    const [pathname, rawQuery = ''] = (req.path || '/').split('?');
    const query = querystring.parse(rawQuery);

    const missing = configError();
    if (missing) {
        send(res, 500, page('Login neconfigurat', `Lipsește din server.cfg: ${escapeHtml(missing.join(', '))}`));
        return;
    }

    if (pathname === '/login') {
        const state = newId();
        pendingStates.set(state, Date.now());

        redirect(res, 'https://discord.com/oauth2/authorize?' + querystring.stringify({
            client_id: cfg('brp_discordClientId'),
            response_type: 'code',
            redirect_uri: cfg('brp_auth_redirectUri'),
            scope: 'identify',
            state,
        }));
        return;
    }

    if (pathname === '/callback') {
        handleDiscordCallback(query, res).catch((e) => fail(res, e));
        return;
    }

    // /own/<platforma>/start  si  /own/<platforma>/callback
    const m = pathname.match(/^\/own\/([a-z]+)\/(start|callback)$/);
    if (m && platforms[m[1]] && ownershipMode().includes(m[1])) {
        const platform = platforms[m[1]];
        const sess = String(query.sess || '');
        const identity = sessions.get(sess);

        if (!identity) {
            send(res, 400, page('Link expirat', 'Sesiunea a expirat. Pornește din nou Basarabia RP.'));
            return;
        }

        if (!platform.ready()) {
            chooseOwnership(res, sess);
            return;
        }

        if (m[2] === 'start') {
            platform.start(sess, res);
            return;
        }

        platform.callback(query).then((result) => {
            if (result.error) {
                send(res, 403, page('Licența GTA V nu a fost confirmată', escapeHtml(result.error), '',
                    `<a class="btn" href="/${RESOURCE}/own/${m[1]}/start?sess=${sess}">Încearcă din nou</a>`));
                return;
            }
            sessions.delete(sess);
            finish(res, identity, result);
        }).catch((e) => fail(res, e));
        return;
    }

    send(res, 404, page('Basarabia RP', 'Pagină inexistentă.'));
});

const missingAtStart = configError();
if (missingAtStart) {
    console.log(`^1[brp_auth] Neconfigurat. Lipsește din server.cfg: ${missingAtStart.join(', ')}^7`);
} else {
    const mode = ownershipMode();
    console.log(`^2[brp_auth] Pornit. Login: /${RESOURCE}/login | verificare GTA V: ${mode.length ? mode.join(', ') : 'dezactivată'}^7`);
}

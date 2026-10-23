// ============================================================================
//  brp_auth – login cu Discord pentru clientul Basarabia RP
//
//  Flux:
//   1. Clientul deschide in browser  http://SERVER:PORT/brp_auth/login
//   2. Redirect la Discord (scope "identify")
//   3. Discord -> pagina HTTPS de pe site (brp_auth_redirectUri), care trimite
//      code+state inapoi aici:  /brp_auth/callback
//   4. Schimbam codul pe identitatea Discord, semnam biletul si deschidem
//      basarabiarp://brp-auth/?t=<bilet>  -> clientul il salveaza si intra.
//
//  Biletul: base64url(json) "." hex(HMAC-SHA256(brp_authSecret, base64url(json)))
//  Il verifica serverul (BasarabiaIdentityProvider) -> identificator discord:<id>.
//
//  server.cfg:
//    set brp_authSecret "<secret lung, aleator>"          (NU il publicati)
//    set brp_discordClientId "<Client ID>"
//    set brp_discordClientSecret "<Client Secret>"
//    set brp_auth_redirectUri "https://pesteraumihai71-hue.github.io/fivem/auth/"
//    set brp_auth_ticketDays 30
//    ensure brp_auth
// ============================================================================

const crypto = require('crypto');
const https = require('https');
const querystring = require('querystring');

const RESOURCE = GetCurrentResourceName();

function cfg(name, fallback = '') {
    const v = GetConvar(name, fallback);
    return typeof v === 'string' ? v.trim() : fallback;
}

// state-uri OAuth valabile 10 minute (protectie CSRF)
const pendingStates = new Map();
const STATE_TTL_MS = 10 * 60 * 1000;

setInterval(() => {
    const now = Date.now();
    for (const [state, created] of pendingStates) {
        if (now - created > STATE_TTL_MS) pendingStates.delete(state);
    }
}, 60 * 1000);

function b64url(buf) {
    return Buffer.from(buf).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function signTicket(discordId, username) {
    const secret = cfg('brp_authSecret');
    const days = Math.max(1, Math.min(90, parseInt(cfg('brp_auth_ticketDays', '30'), 10) || 30));

    const payload = b64url(JSON.stringify({
        v: 1,
        d: String(discordId),
        u: String(username || '').slice(0, 64),
        e: Math.floor(Date.now() / 1000) + days * 24 * 3600,
    }));

    const sig = crypto.createHmac('sha256', secret).update(payload).digest('hex');
    return `${payload}.${sig}`;
}

function request(method, url, headers, body) {
    return new Promise((resolve, reject) => {
        const req = https.request(url, { method, headers, timeout: 15000 }, (res) => {
            let data = '';
            res.on('data', (c) => { data += c; });
            res.on('end', () => {
                try {
                    resolve({ status: res.statusCode, json: JSON.parse(data || '{}') });
                } catch (e) {
                    reject(new Error(`Raspuns invalid de la Discord (${res.statusCode})`));
                }
            });
        });
        req.on('timeout', () => req.destroy(new Error('Timeout la Discord')));
        req.on('error', reject);
        if (body) req.write(body);
        req.end();
    });
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function page(title, message, extraHead = '', button = '') {
    return `<!doctype html><html lang="ro"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)} – Basarabia RP</title>${extraHead}
<style>
 body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#0f1219;color:#c8ccd4;font:16px/1.5 "Segoe UI",Arial,sans-serif;padding:16px}
 .box{max-width:460px;text-align:center}
 h1{color:#f3f4f6;font-size:24px;margin:0 0 10px}
 a.btn{display:inline-block;margin-top:18px;background:#e3192d;color:#fff;font-weight:700;padding:12px 22px;border-radius:6px;text-decoration:none}
</style></head><body><div class="box"><h1>${escapeHtml(title)}</h1><p>${message}</p>${button}</div></body></html>`;
}

function send(res, status, html, headers = {}) {
    res.writeHead(status, Object.assign({
        'Content-Type': 'text/html; charset=utf-8',
        'Cache-Control': 'no-store',
    }, headers));
    res.send(html);
}

function configError() {
    const missing = ['brp_authSecret', 'brp_discordClientId', 'brp_discordClientSecret', 'brp_auth_redirectUri']
        .filter((n) => !cfg(n));
    if (cfg('brp_authSecret') && cfg('brp_authSecret').length < 32) missing.push('brp_authSecret (minim 32 caractere)');
    return missing.length ? missing : null;
}

async function handleCallback(query, res) {
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
        console.log(`[brp_auth] Discord token error ${token.status}: ${JSON.stringify(token.json)}`);
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

    const ticket = signTicket(me.json.id, me.json.global_name || me.json.username);
    const link = `basarabiarp://brp-auth/?t=${encodeURIComponent(ticket)}`;

    console.log(`[brp_auth] Login reusit: discord:${me.json.id} (${me.json.username})`);

    send(res, 200, page(
        'Gata, ești logat',
        `Salut, <b>${escapeHtml(me.json.global_name || me.json.username)}</b>! Te poți întoarce în Basarabia RP – te conectezi automat.`,
        `<meta http-equiv="refresh" content="0;url=${escapeHtml(link)}">`,
        `<a class="btn" href="${escapeHtml(link)}">Deschide Basarabia RP</a>`
    ));
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
        const state = crypto.randomBytes(24).toString('hex');
        pendingStates.set(state, Date.now());

        const url = 'https://discord.com/oauth2/authorize?' + querystring.stringify({
            client_id: cfg('brp_discordClientId'),
            response_type: 'code',
            redirect_uri: cfg('brp_auth_redirectUri'),
            scope: 'identify',
            state,
        });

        send(res, 302, '', { Location: url });
        return;
    }

    if (pathname === '/callback') {
        handleCallback(query, res).catch((e) => {
            console.log(`[brp_auth] Eroare callback: ${e && e.message}`);
            send(res, 500, page('Eroare', 'A apărut o eroare la login. Încearcă din nou.'));
        });
        return;
    }

    send(res, 404, page('Basarabia RP', 'Pagină inexistentă.'));
});

const missing = configError();
if (missing) {
    console.log(`^1[brp_auth] Neconfigurat. Lipsește din server.cfg: ${missing.join(', ')}^7`);
} else {
    console.log(`^2[brp_auth] Pornit. Login: /${RESOURCE}/login^7`);
}

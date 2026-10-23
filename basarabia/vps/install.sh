#!/usr/bin/env bash
# ============================================================================
#  Basarabia RP - instalare server propriu (Ubuntu 22.04 / 24.04)
#
#  Rulare (ca root, pe serverul dedicat / VPS):
#    curl -fsSL https://raw.githubusercontent.com/pesteraumihai71-hue/fivem/master/basarabia/vps/install.sh -o install.sh
#    sudo bash install.sh
#
#  Optiuni (variabile de mediu):
#    GAME_PORT=30120          portul serverului de joc (TCP+UDP)
#    SERVER_TAG=server-1.0.0  versiunea serverului (Release pe GitHub)
#
#  Se poate rula din nou oricand: actualizeaza serverul, NU atinge server.cfg,
#  resursele sau baza de date.
# ============================================================================
set -euo pipefail

REPO="pesteraumihai71-hue/fivem"
GAME_PORT="${GAME_PORT:-30120}"
SERVER_TAG="${SERVER_TAG:-server-1.0.0}"

BASE=/opt/basarabia
SERVER_DIR="$BASE/server"
DATA_DIR="$BASE/data"
SVC_USER=basarabia
DB_NAME=basarabia
DB_USER=basarabia

RAW="https://raw.githubusercontent.com/$REPO/master/basarabia"
ARCHIVE_URL="https://github.com/$REPO/releases/download/$SERVER_TAG/basarabia-server-linux.tar.xz"

c_ok()   { printf '\033[1;32m%s\033[0m\n' "$*"; }
c_info() { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
c_warn() { printf '\033[1;33m%s\033[0m\n' "$*"; }
die()    { printf '\033[1;31mEROARE: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "Rulați cu sudo: sudo bash install.sh"
command -v apt-get >/dev/null || die "Scriptul e pentru Ubuntu/Debian (apt)."
[[ "$GAME_PORT" =~ ^[0-9]+$ ]] || die "GAME_PORT invalid: $GAME_PORT"

# ---------------------------------------------------------------------------
c_info "1/7 Pachete de sistem"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq curl ca-certificates xz-utils tar openssl ufw mariadb-server >/dev/null
c_ok "OK"

# ---------------------------------------------------------------------------
c_info "2/7 Utilizator de sistem '$SVC_USER'"
if ! id "$SVC_USER" >/dev/null 2>&1; then
    useradd --system --home-dir "$BASE" --shell /usr/sbin/nologin "$SVC_USER"
fi
mkdir -p "$SERVER_DIR" "$DATA_DIR/resources/[basarabia]"
c_ok "OK"

# ---------------------------------------------------------------------------
c_info "3/7 Serverul Basarabia RP ($SERVER_TAG)"
TMP_ARCHIVE="$(mktemp /tmp/basarabia-server.XXXXXX.tar.xz)"
curl -fL --retry 3 -o "$TMP_ARCHIVE" "$ARCHIVE_URL" || die "Nu pot descărca $ARCHIVE_URL"
rm -rf "$SERVER_DIR.new"
mkdir -p "$SERVER_DIR.new"
tar -xJf "$TMP_ARCHIVE" -C "$SERVER_DIR.new"
rm -f "$TMP_ARCHIVE"
[ -x "$SERVER_DIR.new/run.sh" ] || die "Arhiva serverului nu conține run.sh"
rm -rf "$SERVER_DIR.old"
[ -d "$SERVER_DIR" ] && mv "$SERVER_DIR" "$SERVER_DIR.old"
mv "$SERVER_DIR.new" "$SERVER_DIR"
rm -rf "$SERVER_DIR.old"
c_ok "OK"

# ---------------------------------------------------------------------------
c_info "4/7 Resursele Basarabia (brp_auth, brp_identity_sync)"
fetch() { # fetch <cale relativa in repo> <destinatie>
    mkdir -p "$(dirname "$2")"
    curl -fsSL --retry 3 -o "$2" "$RAW/$1" || die "Nu pot descărca $1"
}
RES="$DATA_DIR/resources/[basarabia]"
fetch server-resources/brp_auth/fxmanifest.lua           "$RES/brp_auth/fxmanifest.lua"
fetch server-resources/brp_auth/server.js                "$RES/brp_auth/server.js"
fetch server-resources/brp_identity_sync/fxmanifest.lua  "$RES/brp_identity_sync/fxmanifest.lua"
fetch server-resources/brp_identity_sync/server.lua      "$RES/brp_identity_sync/server.lua"
fetch server-resources/brp_identity_sync/vrp.lua         "$RES/brp_identity_sync/vrp.lua"
c_ok "OK"

# ---------------------------------------------------------------------------
c_info "5/7 Baza de date (MariaDB)"
systemctl enable --now mariadb >/dev/null
DB_PASS_FILE="$BASE/.db_password"
if [ ! -f "$DB_PASS_FILE" ]; then
    openssl rand -hex 24 > "$DB_PASS_FILE"
    chmod 600 "$DB_PASS_FILE"
fi
DB_PASS="$(cat "$DB_PASS_FILE")"
mysql -u root <<SQL
CREATE DATABASE IF NOT EXISTS \`$DB_NAME\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASS';
ALTER USER '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASS';
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'localhost';
FLUSH PRIVILEGES;
SQL
c_ok "OK (baza '$DB_NAME', utilizator '$DB_USER')"

# ---------------------------------------------------------------------------
c_info "6/7 server.cfg"
PUBLIC_IP="$(curl -fsS --max-time 5 https://api.ipify.org 2>/dev/null || hostname -I | awk '{print $1}')"
CFG="$DATA_DIR/server.cfg"
if [ ! -f "$CFG" ]; then
    AUTH_SECRET="$(openssl rand -hex 32)"
    RCON_PASS="$(openssl rand -hex 16)"
    cat > "$CFG" <<CFG
# ============================================================================
#  Basarabia RP - server.cfg  (generat de install.sh; editati liber)
# ============================================================================
endpoint_add_tcp "0.0.0.0:$GAME_PORT"
endpoint_add_udp "0.0.0.0:$GAME_PORT"

sv_hostname "Basarabia Roleplay"
sv_maxclients 32
sv_endpointprivacy true
rcon_password "$RCON_PASS"

# Baza de date (vRP 2 / oxmysql etc.)
set mysql_connection_string "mysql://$DB_USER:$DB_PASS@localhost/$DB_NAME?charset=utf8mb4"

# --- Basarabia RP: login cu Discord, fara Cfx ---
set brp_authSecret "$AUTH_SECRET"
set brp_discordClientId "COMPLETATI_CLIENT_ID_DISCORD"
set brp_discordClientSecret "COMPLETATI_CLIENT_SECRET_DISCORD"
set brp_auth_redirectUri "https://pesteraumihai71-hue.github.io/fivem/auth/"
set brp_auth_publicUrl "http://$PUBLIC_IP:$GAME_PORT"
set brp_auth_ticketDays 30

# --- verificarea licentei GTA V (fara Cfx) ---
set brp_ownership "steam,rockstar,epic"
set brp_steamApiKey "COMPLETATI_CHEIA_STEAM_WEB_API"
set brp_requireOwnership false

ensure brp_auth
# ensure vrp                 <- dupa ce copiati resursele serverului
# ensure brp_identity_sync
CFG
    c_ok "Creat $CFG"
else
    c_warn "Există deja $CFG – nu îl modific."
fi
chown -R "$SVC_USER:$SVC_USER" "$BASE"
chmod 600 "$CFG"

# ---------------------------------------------------------------------------
c_info "7/7 Firewall + serviciu"
# portul SSH curent: NU il inchidem niciodata (serverul nu are consola KVM)
SSH_PORT="$(sshd -T 2>/dev/null | awk '/^port /{print $2; exit}')"
SSH_PORT="${SSH_PORT:-22}"
ufw allow "$SSH_PORT/tcp" >/dev/null
ufw allow OpenSSH >/dev/null 2>&1 || true
ufw allow "$GAME_PORT/tcp" >/dev/null
ufw allow "$GAME_PORT/udp" >/dev/null
ufw --force enable >/dev/null
c_ok "Firewall: SSH ($SSH_PORT) + joc ($GAME_PORT tcp/udp) permise"

cat > /etc/systemd/system/basarabia.service <<UNIT
[Unit]
Description=Basarabia RP server
After=network-online.target mariadb.service
Wants=network-online.target

[Service]
Type=simple
User=$SVC_USER
WorkingDirectory=$DATA_DIR
# FXServer se opreste daca intrarea consolei se inchide (EOF) -> ii dam o intrare care nu se termina
ExecStart=/bin/sh -c 'tail -f /dev/null | exec $SERVER_DIR/run.sh +exec server.cfg'
KillMode=control-group
Restart=always
RestartSec=5
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable basarabia >/dev/null
systemctl restart basarabia
sleep 3
systemctl is-active --quiet basarabia && c_ok "Serviciul basarabia rulează" || c_warn "Serviciul nu a pornit – vedeți: journalctl -u basarabia -n 100"

# ---------------------------------------------------------------------------
cat <<DONE

$(c_ok "Gata!")
  Server:        $PUBLIC_IP:$GAME_PORT
  Configurare:   $CFG
  Resurse:       $DATA_DIR/resources/
  Parola DB:     $DB_PASS_FILE

Pași următori:
  1. În $CFG completați: brp_discordClientId, brp_discordClientSecret, brp_steamApiKey
  2. Copiați resursele serverului (vRP 2 etc.) în $DATA_DIR/resources/
     și importați baza de date:  mysql -u root $DB_NAME < backup.sql
  3. Dați ownership: chown -R $SVC_USER:$SVC_USER $BASE
  4. Restart:  systemctl restart basarabia
     Log:      journalctl -u basarabia -f

DONE

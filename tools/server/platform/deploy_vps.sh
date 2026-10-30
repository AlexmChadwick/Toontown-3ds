#!/bin/bash
# Deploy the platform-badge server pieces to the VPS (systemd Astron + UD + AI + bridge).
#
#   tools/server/platform/deploy_vps.sh plan              read-only: discover units/paths, show the yml diff
#   tools/server/platform/deploy_vps.sh apply             back up, install, patch, restart in order, verify;
#                                                         rolls back by itself if any step fails
#   tools/server/platform/deploy_vps.sh rollback DIR      restore a backup (DIR printed by apply)
#   tools/server/platform/deploy_vps.sh verify            UD shows TTPlatformManager ready, district up
#
# Env: HOST (ssh alias, default vps), REMOTE_DIR (default /opt/tt-platform), FORCE=1 (restart even
# if a client connected in the last 15 minutes), UNIT_ASTROND / UNIT_UD / UNIT_AI / UNIT_BRIDGE
# (default: discovered from `systemctl list-unit-files 'tt-*'`), ASTROND_YML (default: the
# config path in the astrond unit's ExecStart).
#
# What apply changes on the VPS (nothing in the upstream checkout):
#   REMOTE_DIR/            platform.dc, ttplatform/, ud_start.py, patch_astrond.py
#   astrond.yml            platform.dc after toon.dc, uberdog 4721, clientagent manual_dc_hash 547155168
#   UNIT_UD drop-in        /etc/systemd/system/$UNIT_UD.d/platform-badges.conf: ExecStart with
#                          toontown.uberdog.UDStart replaced by REMOTE_DIR/ud_start.py, OTT_DIR set
# Backup (/root/tt-platform-backup-STAMP): astrond.yml, the dc-file dir (etc/), unit files and
# drop-ins, the astron database directory and accounts.json.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="${HOST:-vps}"
REMOTE_DIR="${REMOTE_DIR:-/opt/tt-platform}"
CMD="${1:-plan}"

remote() {  # remote <cmd> [args]: run the embedded remote script on the VPS
    ssh "$HOST" "CMD=$1 ARG='${2:-}' REMOTE_DIR='$REMOTE_DIR' FORCE='${FORCE:-}' \
        UNIT_ASTROND='${UNIT_ASTROND:-}' UNIT_UD='${UNIT_UD:-}' UNIT_AI='${UNIT_AI:-}' \
        UNIT_BRIDGE='${UNIT_BRIDGE:-}' ASTROND_YML='${ASTROND_YML:-}' bash -s" < <(remote_script)
}

upload() {
    tar -C "$HERE" -czf - platform.dc ud_start.py patch_astrond.py ttplatform/__init__.py \
        ttplatform/platforms.py ttplatform/registry.py ttplatform/TTPlatformManager.py \
        ttplatform/TTPlatformManagerAI.py ttplatform/TTPlatformManagerUD.py \
        | ssh "$HOST" "mkdir -p '$REMOTE_DIR.new' && tar -C '$REMOTE_DIR.new' -xzf -"
}

remote_script() { cat <<'REMOTE'
set -euo pipefail
pick() { systemctl list-unit-files 'tt-*' --no-legend 2>/dev/null | awk '{print $1}' | grep -i "$1" | head -1; }
UNIT_ASTROND="${UNIT_ASTROND:-$(pick astron)}"
UNIT_UD="${UNIT_UD:-$(pick -E 'uber|ud')}"
UNIT_AI="${UNIT_AI:-$(pick -E '(^|-)ai')}"
UNIT_BRIDGE="${UNIT_BRIDGE:-$(pick bridge)}"
[ -n "$UNIT_ASTROND" ] && [ -n "$UNIT_UD" ] && [ -n "$UNIT_AI" ] || {
    echo "cannot discover units (astrond=$UNIT_ASTROND ud=$UNIT_UD ai=$UNIT_AI); set UNIT_*"; exit 1; }
exec_of() { systemctl show -p ExecStart --value "$1" | sed -n 's/.*argv\[\]=\([^;]*\);.*/\1/p' | head -1; }
wd_of() { systemctl show -p WorkingDirectory --value "$1"; }
A_EXEC="$(exec_of "$UNIT_ASTROND")"; A_WD="$(wd_of "$UNIT_ASTROND")"
YML="${ASTROND_YML:-$(echo "$A_EXEC" | tr ' ' '\n' | grep -E '\.ya?ml$' | tail -1)}"
case "$YML" in /*) ;; *) YML="${A_WD:-/}/$YML";; esac
UD_EXEC="$(exec_of "$UNIT_UD")"; UD_WD="$(wd_of "$UNIT_UD")"
PY_UD="$(echo "$UD_EXEC" | awk '{print $1}')"
# The upstream checkout: the dir with toontown/ among the UD's cwd, its PYTHONPATH and parents.
UD_ENV="$(systemctl show -p Environment --value "$UNIT_UD")"
OTT=""
for d in $(echo "$UD_ENV" | tr ' ' '\n' | sed -n 's/^\(PYTHONPATH\|OTT_DIR\)=//p' | tr ':' ' ') "$UD_WD" "$UD_WD/.." "$(dirname "$YML")/../.."; do
    [ -d "$d/toontown/uberdog" ] && { OTT="$(cd "$d" && pwd)"; break; }
done
DROPIN="/etc/systemd/system/$UNIT_UD.d/platform-badges.conf"
echo "units: astrond=$UNIT_ASTROND ud=$UNIT_UD ai=$UNIT_AI bridge=${UNIT_BRIDGE:-none}"
echo "astrond.yml: $YML"; echo "ud ExecStart: $UD_EXEC (cwd $UD_WD)"; echo "upstream: ${OTT:-NOT FOUND}"
[ -f "$YML" ] || { echo "astrond.yml not found"; exit 1; }

new_ud_exec() {
    local e="$UD_EXEC"
    if echo "$e" | grep -q 'ud_start.py'; then echo "$e"; return; fi
    if echo "$e" | grep -qE '[^ ]*run_module\.py +toontown\.uberdog\.UDStart'; then
        echo "$e" | sed -E "s#[^ ]*run_module\.py +toontown\.uberdog\.UDStart#$REMOTE_DIR/ud_start.py#"
    elif echo "$e" | grep -qE -- '-m +toontown\.uberdog\.UDStart'; then
        echo "$e" | sed -E "s#-m +toontown\.uberdog\.UDStart#$REMOTE_DIR/ud_start.py#"
    else
        echo "UNSUPPORTED"; fi
}
db_dir() { awk '/type: *yaml/{y=1} y && /directory:/{print $2; exit}' "$YML"; }
recent_clients() {
    journalctl -u "$UNIT_ASTROND" --since "-15 min" --no-pager 2>/dev/null \
        | grep -ciE 'client.*(connect|hello)|accepted|new connection' || true
}
restart_all() {
    systemctl restart "$UNIT_ASTROND"; sleep 3
    systemctl restart "$UNIT_UD"; sleep 8
    systemctl restart "$UNIT_AI"
    [ -n "$UNIT_BRIDGE" ] && { sleep 5; systemctl restart "$UNIT_BRIDGE"; }
    return 0
}
verify() {
    local i
    for i in $(seq 1 60); do
        if journalctl -u "$UNIT_UD" --since "-3 min" --no-pager | grep -q 'TTPlatformManager ready' \
           && journalctl -u "$UNIT_AI" --since "-3 min" --no-pager | grep -q 'ToontownAIRepository: Done'; then
            echo "verify: UD has TTPlatformManager, AI district is up"; return 0; fi
        sleep 3
    done
    echo "verify FAILED"; journalctl -u "$UNIT_UD" -n 30 --no-pager; return 1
}
do_rollback() {
    local b="$1"; [ -d "$b" ] || { echo "no backup $b"; exit 1; }
    echo "rolling back from $b"
    cp -a "$b/astrond.yml" "$YML"
    rm -f "$DROPIN"; [ -f "$b/dropin.conf" ] && cp -a "$b/dropin.conf" "$DROPIN"
    systemctl daemon-reload
    restart_all
    echo "rolled back; services restarted"
}

case "$CMD" in
plan)
    echo "new ud ExecStart: $(new_ud_exec)"
    echo "db dir: $(db_dir)  recent client connects (15 min): $(recent_clients)"
    python3 -I "$REMOTE_DIR.new/patch_astrond.py" "$YML" /tmp/astrond.platform.yml \
        --platform-dc "$REMOTE_DIR/platform.dc" 2>/dev/null && diff -u "$YML" /tmp/astrond.platform.yml || true
    ;;
apply)
    [ "$(new_ud_exec)" = UNSUPPORTED ] && { echo "UD ExecStart format not recognised; edit the drop-in by hand"; exit 1; }
    [ -n "$OTT" ] || { echo "upstream checkout not found"; exit 1; }
    n="$(recent_clients)"
    if [ "$n" != 0 ] && [ -z "$FORCE" ]; then
        echo "REFUSING: $n client connect lines in the last 15 min (a player may be on). Re-run later or FORCE=1."; exit 3; fi
    B="/root/tt-platform-backup-$(date +%Y%m%d_%H%M%S)"; mkdir -p "$B"
    cp -a "$YML" "$B/astrond.yml"
    for f in $(awk '/dc_files:/{d=1;next} d && /^ *- /{print $2; next} d{exit}' "$YML"); do
        case "$f" in /*) ;; *) f="$(dirname "$YML")/$f";; esac; [ -f "$f" ] && cp -a "$(dirname "$f")" "$B/etc" && break
    done
    for u in "$UNIT_ASTROND" "$UNIT_UD" "$UNIT_AI" $UNIT_BRIDGE; do systemctl cat "$u" > "$B/$u.unit" 2>/dev/null || true; done
    [ -f "$DROPIN" ] && cp -a "$DROPIN" "$B/dropin.conf"
    DB="$(db_dir)"; case "$DB" in /*) ;; ?*) DB="$(dirname "$YML")/$DB";; esac
    [ -n "$DB" ] && [ -d "$DB" ] && cp -a "$DB" "$B/astrondb"
    for acc in $(find / -xdev -name accounts.json -path '*databases*' 2>/dev/null | head -3); do
        mkdir -p "$B/accounts$(dirname "$acc")"; cp -a "$acc" "$B/accounts$acc"; done
    echo "backup: $B"
    rm -rf "$REMOTE_DIR.old"; [ -d "$REMOTE_DIR" ] && mv "$REMOTE_DIR" "$REMOTE_DIR.old"; mv "$REMOTE_DIR.new" "$REMOTE_DIR"
    trap 'echo "apply failed"; do_rollback "$B"; exit 1' ERR
    "$PY_UD" -I -c "import sys; sys.path.insert(0, '$REMOTE_DIR'); from ttplatform import registry" \
        || python3 -I -c "import sys; sys.path.insert(0, '$REMOTE_DIR'); from ttplatform import registry"
    python3 -I "$REMOTE_DIR/patch_astrond.py" "$YML" "$YML.platform" --platform-dc "$REMOTE_DIR/platform.dc"
    mv "$YML.platform" "$YML"
    mkdir -p "$(dirname "$DROPIN")"
    printf '[Service]\nEnvironment=OTT_DIR=%s\nExecStart=\nExecStart=%s\n' "$OTT" "$(new_ud_exec)" > "$DROPIN"
    systemctl daemon-reload
    restart_all
    verify
    trap - ERR
    echo "APPLIED. rollback: tools/server/platform/deploy_vps.sh rollback $B"
    ;;
rollback) do_rollback "$ARG" ;;
verify) verify ;;
*) echo "unknown command $CMD"; exit 1 ;;
esac
REMOTE
}

case "$CMD" in
plan|apply) upload; remote "$CMD" ;;
rollback) [ -n "${2:-}" ] || { echo "usage: $0 rollback /root/tt-platform-backup-STAMP"; exit 1; }; remote rollback "$2" ;;
verify) remote verify ;;
*) sed -n '2,12p' "$0"; exit 1 ;;
esac

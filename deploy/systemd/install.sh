#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR="${1:-/usr/local/bin}"
CONFIG_DIR="${2:-/etc/miku}"
UNIT_DIR="/etc/systemd/system"

SERVICES=(
  "miku-api"
  "miku-msggateway"
  "miku-msgtransfer"
  "miku-push"
  "miku-crontask"
  "miku-rpc-auth"
  "miku-rpc-user"
  "miku-rpc-friend"
  "miku-rpc-group"
  "miku-rpc-conversation"
  "miku-rpc-msg"
  "miku-rpc-third"
)

NOFILE_LIMITS="65536"
NOFILE_LIMITS_WS="131072"

create_unit() {
  local svc="$1"
  local desc="$2"
  local nofile="${3:-65536}"

  cat > "${UNIT_DIR}/${svc}.service" <<EOF
[Unit]
Description=Miku IM - ${desc}
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=miku
Group=miku
ExecStart=${INSTALL_DIR}/${svc} -c ${CONFIG_DIR}
Restart=on-failure
RestartSec=5
LimitNOFILE=${nofile}

WorkingDirectory=${CONFIG_DIR}

StandardOutput=journal
StandardError=journal
SyslogIdentifier=${svc}

[Install]
WantedBy=multi-user.target
EOF
}

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run as root (sudo)." >&2
  exit 1
fi

id -u miku &>/dev/null || useradd -r -s /bin/false miku

echo "Installing systemd units..."

create_unit "miku-api"           "API Gateway"        "65536"
create_unit "miku-msggateway"    "WebSocket Gateway"  "131072"
create_unit "miku-msgtransfer"   "Message Transfer"   "65536"
create_unit "miku-push"          "Push Notifications" "65536"
create_unit "miku-crontask"      "Cron Tasks"         "65536"
create_unit "miku-rpc-auth"      "Auth RPC"           "65536"
create_unit "miku-rpc-user"      "User RPC"           "65536"
create_unit "miku-rpc-friend"    "Friend RPC"         "65536"
create_unit "miku-rpc-group"     "Group RPC"          "65536"
create_unit "miku-rpc-conversation" "Conversation RPC" "65536"
create_unit "miku-rpc-msg"       "Message RPC"        "65536"
create_unit "miku-rpc-third"     "Third-Party RPC"    "65536"

systemctl daemon-reload

echo ""
echo "Installed. Enable and start with:"
echo "  systemctl enable --now miku-api miku-msggateway"
echo "  systemctl enable --now miku-rpc-auth miku-rpc-user miku-rpc-friend miku-rpc-group"
echo "  systemctl enable --now miku-rpc-conversation miku-rpc-msg miku-rpc-third"
echo "  systemctl enable --now miku-msgtransfer miku-push miku-crontask"
echo ""
echo "View logs:"
echo "  journalctl -u miku-api -f"

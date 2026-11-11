#!/bin/sh
# run_acp_client.sh — agentty 针对假的 OpenAI 服务器,
# 通过信件对齐 ACP v1 模式定义 (tests/acp/schema/v1).
#   使用:run_acp_client.sh <agentty> [
# schema.json] 模式定义 检查需要python的jsonschema;没有它,情景仍然运行.
set -u
A="$1"
HERE=$(cd "$(dirname "$0")" && pwd)
SCHEMA="${2:-$HERE/schema/v1/schema.json}"
S=$(mktemp -d "${TMPDIR:-/tmp}/agentty_acp_XXXXXX")
export FAKE_TOOLS_LOG="$S/tools.log"
python3 "$HERE/fake_openai_server.py" 0 > "$S/port" 2>/dev/null </dev/null &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -rf "$S"' EXIT INT TERM
# 最多30岁(见 run_cli_test.sh : 空端口读作 "no credentials".
for _ in $(seq 1 150); do [ -s "$S/port" ] && break; sleep 0.2; done
if [ ! -s "$S/port" ]; then echo "FAIL fake server did not start"; exit 1; fi
PORT=$(cat "$S/port")

export AGENTTY_HOME="$S/home"
export AGENTTY_NO_PREWARM=1
mkdir -p "$AGENTTY_HOME"
printf '{"provider":"127.0.0.1:%s","model_id":"o4-mini"}\n' "$PORT" > "$AGENTTY_HOME/settings.json"
unset ANTHROPIC_API_KEY CLAUDE_CODE_OAUTH_TOKEN OPENAI_API_KEY AGENTTY_THREADS_DIR

PY=python3
[ -x "$HERE/../../.venv-acp/bin/python" ] && PY="$HERE/../../.venv-acp/bin/python"
"$PY" "$HERE/acp_client.py" "$A" "$SCHEMA" </dev/null

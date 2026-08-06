#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Uso: $0 <AWS_IOT_ENDPOINT> <THING_NAME>"
  echo "Ejemplo: $0 abc123-ats.iot.us-east-1.amazonaws.com embedded-agent-000001"
  exit 1
fi

ENDPOINT="$1"
THING_NAME="$2"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

aws iot-data publish \
  --region us-east-1 \
  --endpoint-url "https://${ENDPOINT}" \
  --topic "ai/agents/${THING_NAME}/tasks" \
  --cli-binary-format raw-in-base64-out \
  --payload "fileb://${ROOT_DIR}/scripts/test_task.json"

echo "Tarea publicada en ai/agents/${THING_NAME}/tasks"

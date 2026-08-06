param(
    [Parameter(Mandatory = $true)]
    [string]$Endpoint,

    [Parameter(Mandatory = $true)]
    [string]$ThingName
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$TaskFile = Join-Path $Root "scripts\test_task.json"

aws iot-data publish `
    --region us-east-1 `
    --endpoint-url "https://$Endpoint" `
    --topic "ai/agents/$ThingName/tasks" `
    --cli-binary-format raw-in-base64-out `
    --payload "fileb://$TaskFile"

Write-Host "Tarea publicada en ai/agents/$ThingName/tasks"

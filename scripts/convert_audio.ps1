param(
    [Parameter(Mandatory = $true)]
    [string]$StartupAudio,

    [Parameter(Mandatory = $true)]
    [string]$WelcomeAudio
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$AudioDir = Join-Path $Root "data\audio"

New-Item -ItemType Directory -Force -Path $AudioDir | Out-Null

ffmpeg -y -i $StartupAudio -ar 16000 -ac 1 -c:a pcm_s16le `
    (Join-Path $AudioDir "inicio.wav")

ffmpeg -y -i $WelcomeAudio -ar 16000 -ac 1 -c:a pcm_s16le `
    (Join-Path $AudioDir "bienvenida.wav")

python (Join-Path $PSScriptRoot "check_wav.py")

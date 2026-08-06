COLOCA AQUI EXACTAMENTE:

1. inicio.wav
   Se reproduce al encender el ESP32.

2. bienvenida.wav
   Tool MCP:
   self.audio.play_welcome

FORMATO:
- WAV RIFF
- PCM lineal sin compresion
- mono
- 16000 Hz
- signed 16-bit little-endian

Conversion:

Linux:
./scripts/convert_audio.sh audio_inicio.mp3 audio_bienvenida.mp3

PowerShell:
.\scripts\convert_audio.ps1 `
  -StartupAudio .\audio_inicio.mp3 `
  -WelcomeAudio .\audio_bienvenida.mp3

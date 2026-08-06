# Instalación paso a paso

## 1. Requisitos

Instala:

```text
ESP-IDF 5.5.1
Python 3
Git
ffmpeg, solo para convertir audio
AWS CLI, opcional para la prueba de publicación
```

Abre una terminal ESP-IDF y entra a la carpeta del proyecto.

## 2. Copiar los certificados de este único Thing

No copies el archivo público al firmware. Necesitas:

```text
AmazonRootCA1.pem
xxxxxxxx-certificate.pem.crt
xxxxxxxx-private.pem.key
```

Ejecuta:

```bash
python scripts/install_certificates.py \
  --root-ca "/ruta/AmazonRootCA1.pem" \
  --certificate "/ruta/xxxxxxxx-certificate.pem.crt" \
  --private-key "/ruta/xxxxxxxx-private.pem.key"
```

El resultado debe ser:

```text
aws/certs/
├── AmazonRootCA1.pem
├── device-certificate.pem.crt
└── device-private.pem.key
```

## 3. Colocar los audios

Coloca:

```text
data/audio/inicio.wav
data/audio/bienvenida.wav
```

Deben ser PCM16, mono y 16000 Hz.

Conversión Linux:

```bash
./scripts/convert_audio.sh audio1.mp3 audio2.mp3
```

Validación:

```bash
python scripts/check_wav.py
```

## 4. Obtener los dos datos de AWS

Copia desde AWS IoT Core:

```text
Device data endpoint:
xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com

ThingName:
el nombre exacto del Thing asociado al certificado
```

El ThingName debe ser también el MQTT Client ID.

## 5. Configurar el firmware

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

En:

```text
Embedded AI Agent configuration
```

configura:

```text
Wi-Fi
├── SSID
└── password

AWS IoT Core - one Thing
├── endpoint sin mqtts://
└── ThingName exacto
```

Para activar el razonamiento por modelo:

```text
Local AI agent
├── Use OpenAI for local agent decisions = Yes
├── API key
└── model = gpt-4o-mini
```

Deja desactivada la segunda llamada de resumen para minimizar costo.

En producción no es recomendable compilar una API key permanente en el
firmware. En este piloto se permite para demostrar el agente local.

En:

```text
Component config
→ MCP C SDK
→ HTTP authentication
```

cambia:

```text
CAMBIAR_TOKEN_MCP_LOCAL
```

El HTTP MCP es solo para diagnóstico LAN; el MCP Client interno no usa ese
token.

## 6. Revisar archivos

```bash
python scripts/check_project.py
```

El script debe encontrar certificados y audios.

## 7. Compilar

```bash
idf.py fullclean
idf.py build
```

La primera compilación descargará:

```text
espressif/mcp-c-sdk 2.0.1
espressif/esp_codec_dev 1.5.7
espressif/openai 1.1.0
```

## 8. Grabar

Linux:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Windows:

```powershell
idf.py -p COM39 flash monitor
```

`flash` también graba la partición SPIFFS construida desde `data/`.

## 9. Confirmar el arranque

En el monitor debes observar:

```text
MCP Server local listo
MCP initialize OK
MCP Client local conectado al MCP Server local
Agent Runtime listo
Conectado a AWS IoT Core
Suscrito a ai/agents/THING_NAME/tasks
```

El dispositivo debe publicar estado en:

```text
ai/agents/THING_NAME/status
```

## 10. Probar desde AWS Console

En:

```text
AWS IoT Core
→ Test
→ MQTT test client
```

Suscríbete a:

```text
ai/agents/THING_NAME/#
```

Publica en:

```text
ai/agents/THING_NAME/tasks
```

Payload:

```json
{
  "task_id": "task-0001",
  "goal": "Reproduce el audio de bienvenida",
  "priority": "normal",
  "context": {}
}
```

Debes recibir la respuesta en:

```text
ai/agents/THING_NAME/results
```

## 11. Probar con AWS CLI

```bash
./scripts/publish_test_task.sh \
  xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com \
  THING_NAME
```

## 12. Probar el MCP HTTP local

Solo desde la LAN:

```text
http://IP_DEL_ESP32/mcp
```

Este endpoint expone las mismas tools, resource y prompt, pero no participa
en el flujo normal del agente.

## 13. Seguridad posterior al piloto

Antes de producción:

```text
- habilitar Secure Boot;
- habilitar Flash Encryption;
- rotar el certificado si se expone;
- no reutilizar certificados entre Things;
- mover o aprovisionar de forma segura la API key;
- usar Fleet Provisioning para cada unidad nueva;
- mantener un certificado X.509 único por ESP32.
```

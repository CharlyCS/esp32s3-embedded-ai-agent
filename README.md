# ESP32-S3 MCP Server-only

Firmware ESP-IDF para exponer herramientas MCP del ESP32-S3 sin cliente MCP ni
agente de IA embebido.

## Transportes disponibles

1. AWS IoT Core MQTT/TLS: recibe MCP JSON-RPC en
   `ai/agents/{thing}/tasks` y responde en
   `ai/agents/{thing}/results`.
2. MCP HTTP directo en la LAN: endpoint `/mcp`, habilitable mediante
   `CONFIG_APP_ENABLE_HTTP_MCP`.

Los dos transportes llegan al mismo conjunto de herramientas físicas. MQTT no
se convierte en HTTP dentro del ESP32; son entradas independientes.

## Estructura relevante

- `main/`: arranque, Wi-Fi y enlace de AWS con el motor MCP.
- `aws/`: conexión MQTT/TLS, topics y certificados del Thing.
- `mcp-server/`: servidor, herramientas, audio y almacenamiento.
- `data/`: archivos SPIFFS.
- `scripts/`: utilidades de configuración y prueba.

## Configuración

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Configura Wi-Fi, endpoint AWS IoT, ThingName y el token Bearer de `/mcp`. Luego
instala certificados propios en `aws/certs/` usando
`scripts/install_certificates.py`.

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Consulta `ARCHITECTURE_SERVER_ONLY.md` para el flujo completo y usa el cliente
de PC entregado por separado para ejecutar `initialize`, `tools/list` y
`tools/call`.

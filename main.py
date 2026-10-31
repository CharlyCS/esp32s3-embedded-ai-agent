"""Cliente PC multi-ESP32 con una conexion MQTT.

Requiere adaptar AwsMcpSession en mcp_transport.py:
  constructor: thing_names: list[str] en lugar de thing_name
  initialize(*, thing_name), list_tools(*, thing_name)
  call_tool(name, arguments, *, thing_name)
Todos estos metodos comparten la conexion MQTT de la PC.
MCPAgent.invoke conserva su interfaz; su salida tool_call incluye thing_name.
"""

import argparse
import asyncio
import json
from pathlib import Path

import yaml
from dotenv import load_dotenv
from jsonschema import Draft202012Validator

from agent import MCPAgent
from mcp_transport import AwsMcpSession


BASE_DIR = Path(__file__).resolve().parent


def imprimir_bloque(titulo):
    print("\n" + "=" * 70)
    print(titulo)
    print("=" * 70)


def cargar_yaml(path):
    with path.open(encoding="utf-8") as file:
        data = yaml.safe_load(file)
    if not isinstance(data, dict):
        raise ValueError(f"Se esperaba un objeto YAML en {path}")
    return data


def resolver_archivo(value):
    path = Path(value).expanduser()
    return path if path.is_absolute() else BASE_DIR / path


def parse_args():
    parser = argparse.ArgumentParser(description="PC MCP para multiples ESP32")
    parser.add_argument("--config", type=Path, default=BASE_DIR / "config/config.yaml")
    parser.add_argument("--certs", type=Path, default=BASE_DIR / "config/certs.yaml")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--max-tool-calls", type=int, default=5)
    args = parser.parse_args()
    if args.timeout <= 0 or args.max_tool_calls < 1:
        parser.error("timeout debe ser positivo y max-tool-calls >= 1")
    return args


async def descubrir_tools(session, devices):
    catalog = []
    for device in devices:
        thing_name = device["AWS_IOT_THING_NAME"]
        try:
            await session.initialize(thing_name=thing_name)
            result = await session.list_tools(thing_name=thing_name)
            device_tools = []
            for tool in result.get("tools", []):
                name = tool["name"]
                schema = tool.get("inputSchema", {"type": "object"})
                Draft202012Validator.check_schema(schema)
                if any(t["name"] == name for t in device_tools):
                    raise ValueError(f"Tool duplicada: {name}")
                device_tools.append({
                    "thing_name": thing_name,
                    "name": name,
                    "description": tool.get("description", ""),
                    "input_schema": schema
                    # "device_context": {
                    #     key: value for key, value in device.items()
                    #     if key != "AWS_IOT_THING_NAME"
                    # },
                })
            catalog.extend(device_tools)
            print(f"Disponible: {thing_name} ({len(device_tools)} tools)")
        except Exception as exc:
            print(f"No disponible: {thing_name}: {exc}")
    return catalog


async def ejecutar_tool(session, decision, catalog_index):
    thing_name = decision.get("thing_name")
    name = decision.get("tool_name")
    arguments = decision.get("arguments", {})
    try:
        if not isinstance(thing_name, str) or not isinstance(name, str):
            raise ValueError("thing_name y tool_name deben ser cadenas")
        tool = catalog_index.get((thing_name, name))
        if tool is None:
            raise ValueError("La herramienta no esta disponible en ese dispositivo")
        if not isinstance(arguments, dict):
            raise ValueError("arguments debe ser un objeto JSON")
        Draft202012Validator(tool["input_schema"]).validate(arguments)
        result = await session.call_tool(name, arguments, thing_name=thing_name)
        return {"thing_name": thing_name, "tool_name": name, "result": result}
    except Exception as exc:
        # No reintentar automaticamente: una accion pudo ejecutarse antes del timeout.
        return {"thing_name": thing_name, "tool_name": name, "error": str(exc)}


async def ejecutar_agente_con_mcp(session, catalog, max_tool_calls):
    agent = MCPAgent()
    index = {(tool["thing_name"], tool["name"]): tool for tool in catalog}
    while True:
        try:
            pregunta = (await asyncio.to_thread(
                input, "\nIntroduce tu pregunta (o 'salir'): "
            )).strip()
        except EOFError:
            return
        if pregunta.lower() in {"salir", "exit", "quit"}:
            return
        if not pregunta:
            continue
        history = []
        try:
            for step in range(max_tool_calls + 1):
                kwargs = {"question": pregunta, "available_tools": catalog}
                if history:
                    kwargs["mcp_result"] = json.dumps(history, ensure_ascii=False)
                response = await asyncio.to_thread(agent.invoke, **kwargs)
                if not isinstance(response, dict):
                    raise ValueError("MCPAgent.invoke debe devolver un diccionario")
                imprimir_bloque("DECISION DEL AGENTE")
                print(json.dumps(response, ensure_ascii=False, indent=2))
                if response.get("type") == "final":
                    imprimir_bloque("RESPUESTA FINAL")
                    print(response.get("answer", ""))
                    break
                if response.get("type") != "tool_call":
                    raise ValueError("Tipo de respuesta no reconocido")
                if step == max_tool_calls:
                    print("Limite de llamadas alcanzado. Resultados obtenidos:")
                    print(json.dumps(history, ensure_ascii=False, indent=2))
                    break
                result = await ejecutar_tool(session, response, index)
                history.append({"decision": response, "execution": result})
                imprimir_bloque("RESULTADO MCP")
                print(json.dumps(result, ensure_ascii=False, indent=2))
                if "error" in result:
                    print("Ejecucion detenida; revisa el error antes de repetir la accion.")
                    break
        except Exception as exc:
            print(f"No se pudo completar la consulta: {exc}")


async def main():
    load_dotenv(BASE_DIR / ".env")
    args = parse_args()
    config = cargar_yaml(args.config)
    certs = cargar_yaml(args.certs)
    # Compatibilidad con el certs.yaml proporcionado por el usuario.
    client = certs["AWS_IOT_CLIENT_ID"][0]
    devices = config["EMBODIED_AGENTS"]
    if not isinstance(devices, list) or not devices:
        raise ValueError("EMBODIED_AGENTS debe ser una lista no vacia")
    thing_names = [device["AWS_IOT_THING_NAME"] for device in devices]
    if any(not isinstance(name, str) or not name for name in thing_names):
        raise ValueError("Cada dispositivo debe tener AWS_IOT_THING_NAME")
    if len(thing_names) != len(set(thing_names)):
        raise ValueError("Hay dispositivos duplicados en config.yaml")
    for field in ("CERT_CLIENT_NAME", "AWS_IOT_ENDPOINT", "AWS_IOT_CERTIFICATE",
                  "AWS_IOT_PRIVATE_KEY", "AWS_IOT_ROOT_CA"):
        if not isinstance(client.get(field), str) or not client[field].strip():
            raise ValueError(f"Falta {field} en certs.yaml")
    configured_id = config.get("AWS_IOT_CLIENT_ID")
    if configured_id and configured_id != client["CERT_CLIENT_NAME"]:
        raise ValueError("El client ID de config.yaml no coincide con certs.yaml")
    files = {key: resolver_archivo(client[key]) for key in (
        "AWS_IOT_CERTIFICATE", "AWS_IOT_PRIVATE_KEY", "AWS_IOT_ROOT_CA"
    )}
    for path in files.values():
        if not path.is_file():
            raise FileNotFoundError(f"No existe: {path}")

    print(f"Cliente MQTT PC: {client['CERT_CLIENT_NAME']}")
    print(f"Destinos: {', '.join(thing_names)}")
    async with AwsMcpSession(
        endpoint=client["AWS_IOT_ENDPOINT"],
        client_id=client["CERT_CLIENT_NAME"],
        certificate=str(files["AWS_IOT_CERTIFICATE"]),
        private_key=str(files["AWS_IOT_PRIVATE_KEY"]),
        root_ca=str(files["AWS_IOT_ROOT_CA"]),
        thing_names=thing_names,
        timeout=args.timeout,
    ) as session:
        catalog = await descubrir_tools(session, devices)
        if not catalog:
            print("No hay herramientas disponibles. Revisa ESP32, topics y permisos.")
            return
        await ejecutar_agente_con_mcp(session, catalog, args.max_tool_calls)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nCliente detenido.")
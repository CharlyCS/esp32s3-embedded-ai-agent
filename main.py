import argparse
import asyncio
import json
import os
from typing import Any, Dict, List

from dotenv import load_dotenv

from agent import MCPAgent
from mcp_transport import AwsMcpSession


load_dotenv()


def imprimir_bloque(titulo: str):
    print("\n" + "=" * 70)
    print(titulo)
    print("=" * 70)


def require_env(name: str) -> str:
    value = os.getenv(name)
    if not value:
        raise ValueError(f"Falta {name} en el archivo .env")
    return value


def convertir_tools_para_agente(result: Dict[str, Any]) -> List[Dict[str, Any]]:
    tools = []
    for tool in result.get("tools", []):
        tools.append(
            {
                "name": tool.get("name", ""),
                "description": tool.get("description", ""),
                "input_schema": tool.get("inputSchema", {}),
            }
        )
    return tools


async def listar_tools(session: AwsMcpSession) -> List[Dict[str, Any]]:
    imprimir_bloque("TOOLS DISPONIBLES")
    result = await session.list_tools()
    tools = convertir_tools_para_agente(result)
    for tool in tools:
        print(f"- {tool['name']}")
        if tool["description"]:
            print(f"  Descripción: {tool['description']}")
        print(f"  Input schema: {json.dumps(tool['input_schema'], ensure_ascii=False)}")
    return tools


async def llamar_tool(
    session: AwsMcpSession, nombre_tool: str, argumentos: Dict[str, Any]
) -> str:
    imprimir_bloque(f"LLAMANDO TOOL: {nombre_tool}")
    try:
        result = await session.call_tool(nombre_tool, argumentos)
        text = json.dumps(result, ensure_ascii=False, indent=2)
        print(text)
        return text
    except Exception as exc:
        error = f"No se pudo llamar la tool {nombre_tool}: {exc}"
        print(error)
        return error


async def ejecutar_agente_con_mcp(session: AwsMcpSession):
    agent = MCPAgent()
    tools_para_agente = await listar_tools(session)

    while True:
        pregunta = input("\nIntroduce tu pregunta (o 'salir'): ").strip()
        if pregunta.lower() in {"salir", "exit", "quit"}:
            return
        if not pregunta:
            continue

        response = await asyncio.to_thread(
            agent.invoke,
            question=pregunta,
            available_tools=tools_para_agente,
        )
        imprimir_bloque("DECISIÓN DEL AGENTE")
        print(json.dumps(response, ensure_ascii=False, indent=2))

        if response.get("type") == "tool_call":
            result = await llamar_tool(
                session,
                response.get("tool_name", ""),
                response.get("arguments", {}),
            )
            final = await asyncio.to_thread(
                agent.invoke,
                question=pregunta,
                available_tools=tools_para_agente,
                mcp_result=result,
            )
            imprimir_bloque("RESPUESTA FINAL")
            print(final.get("answer", ""))
        elif response.get("type") == "final":
            imprimir_bloque("RESPUESTA FINAL")
            print(response.get("answer", ""))
        else:
            print("El agente devolvió un formato no reconocido.")


def parse_args():
    parser = argparse.ArgumentParser(description="AI Agent + MCP Client + AWS IoT")
    parser.add_argument("--endpoint", default=os.getenv("AWS_IOT_ENDPOINT"))
    parser.add_argument("--thing-name", default=os.getenv("AWS_IOT_THING_NAME"))
    parser.add_argument("--certificate", default=os.getenv("AWS_IOT_CERTIFICATE"))
    parser.add_argument("--private-key", default=os.getenv("AWS_IOT_PRIVATE_KEY"))
    parser.add_argument("--root-ca", default=os.getenv("AWS_IOT_ROOT_CA"))
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--client-id",default=os.getenv("AWS_IOT_CLIENT_ID"))
    return parser.parse_args()


async def main():
    args = parse_args()
    required = {
        "AWS_IOT_ENDPOINT": args.endpoint,
        "AWS_IOT_THING_NAME": args.thing_name,
        "AWS_IOT_CERTIFICATE": args.certificate,
        "AWS_IOT_PRIVATE_KEY": args.private_key,
        "AWS_IOT_ROOT_CA": args.root_ca,
    }
    missing = [name for name, value in required.items() if not value]
    if missing:
        raise ValueError("Faltan valores: " + ", ".join(missing))

    print("Conectando el MCP Client al ESP32 mediante AWS IoT Core...")
    print(f"Endpoint: {args.endpoint}")
    print(f"Client ID: {args.client_id}")
    print(f"Thing destino: {args.thing_name}")
    print(f"Certificado: {args.certificate}")

    async with AwsMcpSession(
        endpoint=args.endpoint,
        thing_name=args.thing_name,
        certificate=args.certificate,
        private_key=args.private_key,
        root_ca=args.root_ca,
        client_id=args.client_id, # ADDED
        timeout=args.timeout,
    ) as session:
        server_info = await session.initialize()
        print("Sesión MCP inicializada correctamente.")
        print(json.dumps(server_info, ensure_ascii=False, indent=2))
        await ejecutar_agente_con_mcp(session)


if __name__ == "__main__":
    asyncio.run(main())

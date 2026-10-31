import json
import os
from typing import Any, Dict, List, Literal, Optional, TypedDict

from dotenv import load_dotenv
from langgraph.graph import END, START, StateGraph
from openai import OpenAI
import yaml

load_dotenv()

with open("config/config.yaml", "r") as f:
    config = yaml.safe_load(f)


embodied_agents = [agent["AWS_IOT_THING_NAME"] for agent in config.get("EMBODIED_AGENTS", []) if agent.get("AWS_IOT_THING_NAME")]
print("Agentes disponibles:", json.dumps(embodied_agents, ensure_ascii=False, indent=2))


DEFAULT_SYSTEM_PROMPT = f"""
Eres un agente supervisor en español conectado a varios ESP32 mediante MCP.
Analiza la solicitud del usuario, elige el ESP32 adecuado a la peticion que se requiere y decide si debes utilizar una herramienta del ESP32 seleccionado.
Los agentes disponibles son:
{embodied_agents}

Reglas:
1. Responde con type="final" si no necesitas una herramienta.
2. Responde con type="tool_call" si necesitas una herramienta MCP.
3. Usa exactamente un nombre incluido en las herramientas disponibles.
4. Usa únicamente argumentos definidos por input_schema.
5. No inventes herramientas, argumentos ni resultados.
6. Si recibes mcp_result, genera una respuesta final basada en ese resultado.
7. Devuelve siempre un objeto JSON válido.
"""


class AgentState(TypedDict, total=False):
    question: str
    available_tools: List[Dict[str, Any]]
    mcp_result: Optional[str]
    decision: Dict[str, Any]
    answer: str


class MCPAgent:
    def __init__(
        self,
        model: Optional[str] = None,
        system_prompt: str = DEFAULT_SYSTEM_PROMPT,
        api_key: Optional[str] = None,
    ):
        self.api_key = api_key or os.getenv("OPENAI_API_KEY")
        if not self.api_key:
            raise ValueError("Falta OPENAI_API_KEY en el archivo .env")

        self.model = model or os.getenv("OPENAI_MODEL", "gpt-4o-mini")
        self.system_prompt = system_prompt
        self.client = OpenAI(api_key=self.api_key)
        self.graph = self._build_graph()

    def _build_graph(self):
        graph = StateGraph(AgentState)
        graph.add_node("decide", self._decide)
        graph.add_node("answer", self._answer)
        graph.add_edge(START, "decide")
        graph.add_conditional_edges(
            "decide",
            self._route_after_decision,
            {"tool_call": END, "final": "answer"},
        )
        graph.add_edge("answer", END)
        return graph.compile()

    def _route_after_decision(
        self, state: AgentState
    ) -> Literal["tool_call", "final"]:
        decision = state.get("decision", {})
        if decision.get("type") == "tool_call" and not state.get("mcp_result"):
            return "tool_call"
        return "final"

    def _decide(self, state: AgentState) -> AgentState:

        print("Analizando la pregunta y el resultado previo para decidir la acción...")
        print(" Agentes disponibles:", json.dumps(embodied_agents, ensure_ascii=False, indent=2))
        print(" Tools MCP disponibles:", json.dumps(state.get('available_tools', []), ensure_ascii=False, indent=2))
        print(" Resultado MCP previo:", state.get('mcp_result') or 'No hay resultado MCP todavía.')


        prompt = f"""
            Eres un coordinador de dispositivos que ejecuta herramientas MCP.
            Pregunta del usuario:
            {state.get('question', '')}

            Agentes disponibles:
            {json.dumps(embodied_agents, ensure_ascii=False, indent=2)}

            Tools MCP disponibles:
            {json.dumps(state.get('available_tools', []), ensure_ascii=False, indent=2)}

            Resultado MCP previo:
            {state.get('mcp_result') or 'No hay resultado MCP todavía.'}

            REGLAS:
            1. Selecciona el agente y la herramienta usando la pregunta del usuario
            y las capacidades descritas.
            2. Cada herramienta se identifica por la combinación de thing_name y name.
            Usa exactamente los valores del listado.
            3. No inventes herramientas ni argumentos.
            4. En esta demostración, la petición "prende la luz", "enciende la luz"
            o equivalente se representa reproduciendo el audio mediante
            self.audio.play_light del agente que tenga esa herramienta.
            Esta acción reproduce audio; no confirma el encendido de una luz física.
            5. Si no hay resultado MCP previo y existe una herramienta adecuada,
            devuelve tool_call.
            6. Si existe un resultado MCP previo que confirma que la solicitud se
            completó, devuelve final. Si indica un error, explica el error sin
            afirmar que la acción se realizó.
            7. Si ninguna herramienta permite atender la petición, devuelve final
            explicando qué capacidad falta.
            8. Devuelve únicamente un objeto JSON válido, sin Markdown.

            Formato para ejecutar una herramienta:
            {{
            "type": "tool_call",
            "thing_name": "identificador_exacto_del_agente",
            "tool_name": "nombre_exacto_de_la_herramienta",
            "arguments": {{}},
            "reason": "explicación breve de por qué se eligió esta herramienta",
            }}

            o:

            {{
            "type": "final",
            "executor agent": "thing_name",
            "answer": "respuesta final"
            
            }}
        """
        content = self._call_openai_json(prompt)
        decision = self._safe_json_loads(content)

        if decision.get("type") == "tool_call":
            allowed = {tool["name"] for tool in state.get("available_tools", [])}
            if decision.get("tool_name") not in allowed:
                decision = {
                    "type": "final",
                    "answer": "No pude seleccionar una herramienta MCP válida.",
                }
            if not isinstance(decision.get("arguments", {}), dict):
                decision["arguments"] = {}

        if decision.get("type") not in {"tool_call", "final"}:
            decision = {"type": "final", "answer": content}

        return {**state, "decision": decision}

    def _answer(self, state: AgentState) -> AgentState:
        decision = state.get("decision", {})
        mcp_result = state.get("mcp_result")

        if decision.get("type") == "final" and decision.get("answer") and not mcp_result:
            return {**state, "answer": decision["answer"]}

        prompt = f"""
            Pregunta original:
            {state.get('question', '')}

            Resultado obtenido mediante MCP:
            {mcp_result}

            Redacta una respuesta final clara en español. No menciones el grafo ni inventes
            información que no esté en el resultado.
            """
        return {**state, "answer": self._call_openai_text(prompt)}

    # generadora de respuesta de texto simple, ejemplo: se esta ejecutando... 
    def _call_openai_text(self, prompt: str) -> str:
        response = self.client.chat.completions.create(
            model=self.model,
            messages=[
                {"role": "system", "content": self.system_prompt},
                {"role": "user", "content": prompt},
            ],
            temperature=0.2,
        )
        return response.choices[0].message.content.strip()

    # funcion ejcutora MCP /  eleccion de herramientas y posible eleccion de agentes
    def _call_openai_json(self, prompt: str) -> str:
        response = self.client.chat.completions.create(
            model=self.model,
            messages=[
                {"role": "system", "content": self.system_prompt},
                {"role": "user", "content": prompt},
            ],
            temperature=0.1,
            response_format={"type": "json_object"},
        )
        return response.choices[0].message.content.strip()

    @staticmethod
    def _safe_json_loads(text: str) -> Dict[str, Any]:
        try:
            value = json.loads(text)
            return value if isinstance(value, dict) else {"type": "final", "answer": text}
        except json.JSONDecodeError:
            return {"type": "final", "answer": text}

    def invoke(
        self,
        question: str,
        available_tools: Optional[List[Dict[str, Any]]] = None,
        mcp_result: Optional[str] = None,
    ) -> Dict[str, Any]:
        result = self.graph.invoke(
            {
                "question": question,
                "available_tools": available_tools or [],
                "mcp_result": mcp_result,
            }
        )
        decision = result.get("decision", {})
        if decision.get("type") == "tool_call" and not mcp_result:
            return {
                "type": "tool_call",
                "thing_name": decision.get("thing_name"),
                "tool_name": decision.get("tool_name"),
                "arguments": decision.get("arguments", {}),
                "reason": decision.get("reason", ""),
            }
        return {
            "type": "final",
            "answer": result.get("answer", decision.get("answer", "")),
        }

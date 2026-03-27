"""miki Demo Control MCP Server — controls running miki demo via TCP JSON-RPC.

Connects to a running miki demo process on localhost:9717 (default).
Provides parameter discovery, get/set, frame stats, screenshot, and exit.

Usage from Windsurf: configured in mcp_config.json, auto-discovered by Cascade.
"""

from __future__ import annotations

import json
import os
import socket
from typing import Any

from mcp.server.fastmcp import FastMCP

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

DEMO_HOST = os.environ.get("MIKI_DEMO_HOST", "127.0.0.1")
DEMO_PORT = int(os.environ.get("MIKI_DEMO_PORT", "9717"))

# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------

mcp = FastMCP(
    "miki-demo-mcp",
    instructions=(
        "MCP server for controlling a running miki renderer demo. "
        "Connect to the demo's built-in TCP control server (localhost:9717). "
        "Use list_parameters to discover all controllable parameters with "
        "their types, ranges, semantics, and current values. "
        "Use set_parameter to change values in real-time. "
        "Use get_frame_stats for FPS/resolution/backend info."
    ),
)


def _send(method: str, params: dict[str, Any] | None = None) -> Any:
    """Send a JSON-RPC request to the demo process and return the result."""
    try:
        sock = socket.create_connection((DEMO_HOST, DEMO_PORT), timeout=3)
    except (ConnectionRefusedError, OSError) as e:
        return {"error": f"Cannot connect to demo at {DEMO_HOST}:{DEMO_PORT}. "
                f"Is the demo running? Error: {e}"}

    msg: dict[str, Any] = {"id": 1, "method": method}
    if params:
        # Flatten params into the top-level message (our protocol is simple)
        msg.update(params)

    try:
        sock.sendall((json.dumps(msg) + "\n").encode("utf-8"))
        data = b""
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
            if b"\n" in data:
                break
        sock.close()
        response = json.loads(data.decode("utf-8").strip())
        return response.get("result", response)
    except Exception as e:
        return {"error": str(e)}


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------


@mcp.tool()
def list_parameters() -> str:
    """List all controllable demo parameters with full schema.

    Returns JSON array of parameter descriptors, each containing:
    - name: parameter identifier (use with set_parameter)
    - category: grouping (ToneMapping, Vignette, ChromaticAberration, Camera)
    - type: int, float, bool, or enum
    - semantic: human-readable description of what the parameter does
    - min/max/step: valid value range
    - default: factory default value
    - current: current live value
    - choices: (enum only) list of {value, label} pairs
    """
    result = _send("list_parameters")
    return json.dumps(result, indent=2)


@mcp.tool()
def get_parameter(name: str) -> str:
    """Get the current value and full schema of a single parameter.

    Args:
        name: Parameter name (e.g. "exposure", "tone_map_mode", "vignette_strength")
    """
    result = _send("get_parameter", {"name": name})
    return json.dumps(result, indent=2)


@mcp.tool()
def set_parameter(name: str, value: str) -> str:
    """Set a demo parameter to a new value in real-time.

    The value is automatically parsed based on the parameter's type.
    For enum parameters, pass the integer index (e.g. "1" for AgX).
    For float parameters, pass a decimal string (e.g. "2.5").
    For bool parameters, pass "true" or "false".

    Args:
        name: Parameter name (from list_parameters)
        value: New value as string (auto-parsed by type)

    Returns:
        Previous and current values confirming the change.
    """
    result = _send("set_parameter", {"name": name, "value": value})
    return json.dumps(result, indent=2)


@mcp.tool()
def reset_all_parameters() -> str:
    """Reset all demo parameters to their default values."""
    result = _send("reset_all")
    return json.dumps(result, indent=2)


@mcp.tool()
def get_frame_stats() -> str:
    """Get current frame statistics from the running demo.

    Returns: FPS, frame time (ms), frame index, resolution, backend name.
    """
    result = _send("get_frame_stats")
    return json.dumps(result, indent=2)


@mcp.tool()
def capture_screenshot(output_path: str = "") -> str:
    """Capture the current frame as a PNG screenshot.

    Args:
        output_path: Optional file path for the screenshot.
                     If empty, uses a default temp location.
    """
    result = _send("capture_screenshot", {"output_path": output_path})
    return json.dumps(result, indent=2)


@mcp.tool()
def request_demo_exit() -> str:
    """Request the demo to exit gracefully."""
    result = _send("request_exit")
    return json.dumps(result, indent=2)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()

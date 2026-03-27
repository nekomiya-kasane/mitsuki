"""RenderDoc CLI MCP Server — wraps renderdoccmd for GPU frame capture and analysis."""

from __future__ import annotations

import asyncio
import difflib
import json
import os
import re
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from mcp.server.fastmcp import FastMCP

# ---------------------------------------------------------------------------
# Locate renderdoccmd
# ---------------------------------------------------------------------------

RENDERDOC_CMD_ENV = os.environ.get("RENDERDOC_CMD")


def _find_renderdoccmd() -> str | None:
    """Find renderdoccmd executable, checking env var first, then PATH."""
    if RENDERDOC_CMD_ENV and Path(RENDERDOC_CMD_ENV).is_file():
        return RENDERDOC_CMD_ENV
    return shutil.which("renderdoccmd") or shutil.which("renderdoccli")


RENDERDOC_CMD = _find_renderdoccmd()

# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------

mcp = FastMCP(
    "renderdoc-cli-mcp",
    instructions=(
        "RenderDoc CLI MCP server. Provides GPU frame capture, replay, "
        "thumbnail extraction, format conversion, process injection, and "
        "section embed/extract via the renderdoccmd command-line tool."
    ),
)


async def _run(args: list[str], timeout: int = 120) -> str:
    """Run renderdoccmd with given arguments and return combined output."""
    if not RENDERDOC_CMD:
        return (
            "ERROR: renderdoccmd not found. "
            "Set RENDERDOC_CMD env var or ensure renderdoccmd is on PATH."
        )
    cmd = [RENDERDOC_CMD] + args
    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=timeout)
        out = stdout.decode("utf-8", errors="replace").strip()
        err = stderr.decode("utf-8", errors="replace").strip()
        parts = []
        if out:
            parts.append(out)
        if err:
            parts.append(f"[stderr] {err}")
        if proc.returncode != 0:
            parts.append(f"[exit code {proc.returncode}]")
        return "\n".join(parts) if parts else "(no output)"
    except asyncio.TimeoutError:
        return f"ERROR: renderdoccmd timed out after {timeout}s"
    except Exception as e:
        return f"ERROR: {e}"


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------


@mcp.tool()
async def renderdoc_version() -> str:
    """Get RenderDoc version information."""
    return await _run(["version"])


@mcp.tool()
async def renderdoc_capture(
    executable: str,
    arguments: str = "",
    working_dir: str = "",
    capture_file: str = "",
    wait_for_exit: bool = True,
    api_validation: bool = True,
    hook_children: bool = False,
) -> str:
    """Launch an executable under RenderDoc to capture GPU frames.

    Args:
        executable: Path to the executable to capture.
        arguments: Command-line arguments for the executable (space-separated).
        working_dir: Working directory for the launched program.
        capture_file: Filename template for captures (frame number auto-appended).
        wait_for_exit: Wait for the target program to exit before returning.
        api_validation: Enable API debugging/validation messages.
        hook_children: Hook child processes created by the application.
    """
    args = ["capture"]
    if working_dir:
        args += ["-d", working_dir]
    if capture_file:
        args += ["-c", capture_file]
    if wait_for_exit:
        args.append("-w")
    if api_validation:
        args.append("--opt-api-validation")
    if hook_children:
        args.append("--opt-hook-children")
    args.append(executable)
    if arguments:
        args += arguments.split()
    return await _run(args, timeout=300)


@mcp.tool()
async def renderdoc_replay(
    capture_file: str,
    width: int = 1280,
    height: int = 720,
    loops: int = 1,
) -> str:
    """Replay a .rdc capture and show the backbuffer on a preview window.

    Args:
        capture_file: Path to the .rdc capture file.
        width: Preview window width.
        height: Preview window height.
        loops: How many times to loop (0 = indefinite).
    """
    args = ["replay", "-w", str(width), "-h", str(height), "-l", str(loops), capture_file]
    return await _run(args, timeout=300)


@mcp.tool()
async def renderdoc_thumbnail(
    capture_file: str,
    output_file: str,
    format: str = "",
    max_size: int = 0,
) -> str:
    """Extract the embedded thumbnail from a .rdc capture and save to disk.

    Args:
        capture_file: Path to the .rdc capture file.
        output_file: Output image file path.
        format: Image format (jpg, png, bmp, tga). Auto-detected from filename if empty.
        max_size: Maximum dimension of thumbnail. 0 = unlimited.
    """
    args = ["thumb", "-o", output_file]
    if format:
        args += ["-f", format]
    if max_size > 0:
        args += ["-s", str(max_size)]
    args.append(capture_file)
    return await _run(args)


@mcp.tool()
async def renderdoc_convert(
    input_file: str,
    output_file: str,
    input_format: str = "rdc",
    convert_format: str = "xml",
) -> str:
    """Convert between capture formats.

    Args:
        input_file: Input capture file path.
        output_file: Output file path.
        input_format: Input format (rdc, zip.xml).
        convert_format: Output format (rdc, chrome.json, xml, zip.xml).
    """
    args = [
        "convert",
        "-f", input_file,
        "-o", output_file,
        "-i", input_format,
        "-c", convert_format,
    ]
    return await _run(args)


@mcp.tool()
async def renderdoc_inject(
    pid: int,
    capture_file: str = "",
    wait_for_exit: bool = False,
    api_validation: bool = True,
) -> str:
    """Inject RenderDoc into a running process by PID.

    Args:
        pid: Process ID of the target process.
        capture_file: Filename template for captures.
        wait_for_exit: Wait for the target to exit before returning.
        api_validation: Enable API debugging/validation messages.
    """
    args = ["inject", f"--PID={pid}"]
    if capture_file:
        args += ["-c", capture_file]
    if wait_for_exit:
        args.append("-w")
    if api_validation:
        args.append("--opt-api-validation")
    return await _run(args, timeout=300)


@mcp.tool()
async def renderdoc_embed(
    capture_file: str,
    section: str,
    data_file: str,
    no_clobber: bool = False,
    compress: str = "",
) -> str:
    """Embed arbitrary data into a capture file as a named section.

    Args:
        capture_file: Path to the .rdc capture file.
        section: Section name to embed.
        data_file: File to read section contents from.
        no_clobber: Don't overwrite if section already exists.
        compress: Compression algorithm (lz4, zstd, or empty for none).
    """
    args = ["embed", "-s", section, "-f", data_file]
    if no_clobber:
        args.append("-n")
    if compress == "lz4":
        args.append("--lz4")
    elif compress == "zstd":
        args.append("--zstd")
    args.append(capture_file)
    return await _run(args)


@mcp.tool()
async def renderdoc_extract(
    capture_file: str,
    section: str,
    output_file: str,
    no_clobber: bool = False,
) -> str:
    """Extract a named section from a capture file.

    Args:
        capture_file: Path to the .rdc capture file.
        section: Section name to extract.
        output_file: File to write section contents to.
        no_clobber: Don't overwrite if output file already exists.
    """
    args = ["extract", "-s", section, "-f", output_file]
    if no_clobber:
        args.append("-n")
    args.append(capture_file)
    return await _run(args)


@mcp.tool()
async def renderdoc_remoteserver() -> str:
    """Start a RenderDoc remote replay server."""
    return await _run(["remoteserver"], timeout=10)


# ---------------------------------------------------------------------------
# XML export helper
# ---------------------------------------------------------------------------


async def _export_xml(capture_file: str) -> str | None:
    """Export a .rdc capture to XML in a temp file, return the XML content."""
    if not Path(capture_file).is_file():
        return None
    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as tmp:
        tmp_path = tmp.name
    try:
        result = await _run([
            "convert", "-f", capture_file, "-o", tmp_path,
            "-i", "rdc", "-c", "xml",
        ], timeout=120)
        if "ERROR" in result or not Path(tmp_path).is_file():
            return None
        return Path(tmp_path).read_text(encoding="utf-8", errors="replace")
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def _parse_chunks(xml_text: str) -> list[ET.Element]:
    """Parse RenderDoc XML export and return chunk elements."""
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError:
        return []
    chunks = root.findall(".//chunk")
    if not chunks:
        chunks = root.findall(".//{*}chunk")
    if not chunks:
        for elem in root.iter():
            if "chunk" in elem.tag.lower():
                chunks.append(elem)
    return chunks


# ---------------------------------------------------------------------------
# Advanced analysis tools
# ---------------------------------------------------------------------------


@mcp.tool()
async def renderdoc_list_drawcalls(
    capture_file: str,
    max_results: int = 200,
) -> str:
    """List all draw calls from a .rdc capture file.

    Exports the capture to XML and extracts draw/dispatch calls with their
    parameters. Returns a structured list of draw calls with event IDs.

    Args:
        capture_file: Path to the .rdc capture file.
        max_results: Maximum number of draw calls to return.
    """
    xml_text = await _export_xml(capture_file)
    if xml_text is None:
        return f"ERROR: Failed to export {capture_file} to XML."

    draw_patterns = re.compile(
        r"(draw|dispatch|clear|resolve|copy|blit|barrier|bind|begin|end)",
        re.IGNORECASE,
    )

    chunks = _parse_chunks(xml_text)
    if not chunks:
        lines = xml_text.split("\n")
        draws = []
        for line in lines:
            if draw_patterns.search(line):
                clean = line.strip()
                if clean:
                    draws.append(clean)
        if not draws:
            return f"No draw calls found. XML has {len(lines)} lines total."
        draws = draws[:max_results]
        return f"Found {len(draws)} draw-related calls:\n" + "\n".join(
            f"  [{i}] {d}" for i, d in enumerate(draws)
        )

    draws = []
    for chunk in chunks:
        name = chunk.get("name", chunk.text or chunk.tag)
        if draw_patterns.search(name):
            eid = chunk.get("eventId", chunk.get("eid", "?"))
            params = []
            for child in chunk:
                val = child.text or child.get("value", "")
                params.append(f"{child.tag}={val}")
            param_str = ", ".join(params) if params else ""
            draws.append(f"  [EID {eid}] {name}({param_str})")

    if not draws:
        return f"No draw calls found in {len(chunks)} API chunks."

    draws = draws[:max_results]
    return f"Found {len(draws)} draw-related calls:\n" + "\n".join(draws)


@mcp.tool()
async def renderdoc_list_api_calls(
    capture_file: str,
    filter_keyword: str = "",
    max_results: int = 500,
) -> str:
    """List API calls from a .rdc capture, optionally filtered by keyword.

    Exports the capture to XML and returns a list of all recorded API calls.
    Use filter_keyword to search for specific calls (e.g. 'vkCmd', 'Draw',
    'Buffer', 'Pipeline', 'Descriptor', 'Bind', 'Barrier').

    Args:
        capture_file: Path to the .rdc capture file.
        filter_keyword: Only show calls containing this substring (case-insensitive).
        max_results: Maximum number of calls to return.
    """
    xml_text = await _export_xml(capture_file)
    if xml_text is None:
        return f"ERROR: Failed to export {capture_file} to XML."

    chunks = _parse_chunks(xml_text)
    results = []

    if chunks:
        for chunk in chunks:
            name = chunk.get("name", chunk.text or chunk.tag)
            if filter_keyword and filter_keyword.lower() not in name.lower():
                continue
            eid = chunk.get("eventId", chunk.get("eid", "?"))
            results.append(f"  [EID {eid}] {name}")
    else:
        for line in xml_text.split("\n"):
            clean = line.strip()
            if not clean:
                continue
            if filter_keyword and filter_keyword.lower() not in clean.lower():
                continue
            results.append(f"  {clean}")

    total = len(results)
    results = results[:max_results]
    truncated = " (truncated)" if total > max_results else ""
    kw_info = f" matching '{filter_keyword}'" if filter_keyword else ""
    return f"Found {total} API calls{kw_info}{truncated}:\n" + "\n".join(results)


@mcp.tool()
async def renderdoc_gpu_timeline(
    capture_file: str,
    output_file: str = "",
) -> str:
    """Export GPU event timeline in Chrome trace format (chrome.json).

    The resulting JSON can be loaded in chrome://tracing or Perfetto UI
    for visual timeline analysis of GPU events.

    Args:
        capture_file: Path to the .rdc capture file.
        output_file: Output .json file path. If empty, uses a temp file and
                     returns a summary of the timeline events.
    """
    use_temp = not output_file
    if use_temp:
        tmp = tempfile.NamedTemporaryFile(suffix=".json", delete=False)
        output_file = tmp.name
        tmp.close()

    result = await _run([
        "convert", "-f", capture_file, "-o", output_file,
        "-i", "rdc", "-c", "chrome.json",
    ], timeout=120)

    if "ERROR" in result or not Path(output_file).is_file():
        return f"ERROR: Failed to export chrome.json. {result}"

    if use_temp:
        try:
            content = Path(output_file).read_text(encoding="utf-8", errors="replace")
            try:
                data = json.loads(content)
                events = data if isinstance(data, list) else data.get("traceEvents", [])
                n = len(events)
                names = {}
                for e in events:
                    nm = e.get("name", "unknown")
                    names[nm] = names.get(nm, 0) + 1
                top = sorted(names.items(), key=lambda x: -x[1])[:20]
                summary = "\n".join(f"  {nm}: {cnt}" for nm, cnt in top)
                return (
                    f"GPU timeline: {n} events exported.\n"
                    f"Top event types:\n{summary}"
                )
            except json.JSONDecodeError:
                return f"Exported chrome.json ({len(content)} bytes) but failed to parse."
        finally:
            try:
                os.unlink(output_file)
            except OSError:
                pass
    else:
        size = Path(output_file).stat().st_size
        return f"GPU timeline exported to {output_file} ({size} bytes). {result}"


@mcp.tool()
async def renderdoc_list_sections(capture_file: str) -> str:
    """List all embedded sections in a .rdc capture file.

    Sections contain metadata, thumbnails, extended data, and custom data.
    Use renderdoc_extract to retrieve a specific section.

    Args:
        capture_file: Path to the .rdc capture file.
    """
    result = await _run(["extract", "--list-sections", "-s", "dummy", "-f", "dummy", capture_file])
    if "ERROR" in result and "list" not in result.lower():
        result2 = await _run(["embed", "--list-sections", "-s", "dummy", "-f", "dummy", capture_file])
        if "section" in result2.lower():
            return result2
    return result


@mcp.tool()
async def renderdoc_analyze_frame(capture_file: str) -> str:
    """Comprehensive frame analysis of a .rdc capture.

    Exports to XML and produces a summary including:
    - Total API call count
    - Draw call count and types
    - Resource operation counts (buffer, texture, pipeline, descriptor)
    - Render pass structure
    - Barrier/sync points

    Args:
        capture_file: Path to the .rdc capture file.
    """
    xml_text = await _export_xml(capture_file)
    if xml_text is None:
        return f"ERROR: Failed to export {capture_file} to XML."

    chunks = _parse_chunks(xml_text)

    categories = {
        "draw": re.compile(r"draw|drawindex|drawindirect|drawinstanced", re.I),
        "dispatch": re.compile(r"dispatch", re.I),
        "clear": re.compile(r"clear", re.I),
        "copy": re.compile(r"copy|blit|resolve", re.I),
        "barrier": re.compile(r"barrier|transition", re.I),
        "bind": re.compile(r"bindpipeline|bindvertex|bindindex|binddescrip", re.I),
        "renderpass": re.compile(r"beginrender|endrender|beginpass|endpass", re.I),
        "buffer_op": re.compile(r"createbuffer|mapbuffer|updatebuffer|buffer", re.I),
        "texture_op": re.compile(r"createimage|createtexture|texture|image", re.I),
        "pipeline": re.compile(r"createpipeline|pipeline|shader", re.I),
        "descriptor": re.compile(r"descriptor|allocatedescriptor|updatedescriptor", re.I),
    }

    counts: dict[str, int] = {k: 0 for k in categories}
    total = 0
    api_names: dict[str, int] = {}

    if chunks:
        total = len(chunks)
        for chunk in chunks:
            name = chunk.get("name", chunk.text or chunk.tag)
            api_names[name] = api_names.get(name, 0) + 1
            for cat, pat in categories.items():
                if pat.search(name):
                    counts[cat] += 1
    else:
        lines = [l.strip() for l in xml_text.split("\n") if l.strip()]
        total = len(lines)
        for line in lines:
            for cat, pat in categories.items():
                if pat.search(line):
                    counts[cat] += 1

    parts = [f"# Frame Analysis: {Path(capture_file).name}", f"Total API calls: {total}", ""]

    parts.append("## Call Categories")
    for cat, cnt in sorted(counts.items(), key=lambda x: -x[1]):
        if cnt > 0:
            parts.append(f"  {cat}: {cnt}")

    if api_names:
        parts.append("")
        parts.append("## Top 25 API Calls by Frequency")
        top = sorted(api_names.items(), key=lambda x: -x[1])[:25]
        for name, cnt in top:
            parts.append(f"  {name}: {cnt}")

    return "\n".join(parts)


@mcp.tool()
async def renderdoc_capture_and_analyze(
    executable: str,
    arguments: str = "",
    working_dir: str = "",
    capture_file: str = "",
    api_validation: bool = True,
) -> str:
    """Capture a GPU frame and immediately run comprehensive analysis.

    Launches the executable under RenderDoc, waits for it to exit, then
    automatically runs renderdoc_analyze_frame on the resulting .rdc file.

    Args:
        executable: Path to the executable to capture.
        arguments: Command-line arguments for the executable.
        working_dir: Working directory for the launched program.
        capture_file: Filename template for captures. If empty, uses a temp path.
        api_validation: Enable API debugging/validation messages.
    """
    if not capture_file:
        capture_file = os.path.join(tempfile.gettempdir(), "renderdoc_auto_capture")

    cap_args = ["capture", "-w"]
    if working_dir:
        cap_args += ["-d", working_dir]
    cap_args += ["-c", capture_file]
    if api_validation:
        cap_args.append("--opt-api-validation")
    cap_args.append(executable)
    if arguments:
        cap_args += arguments.split()

    cap_result = await _run(cap_args, timeout=300)

    # Find the .rdc file produced
    base_dir = os.path.dirname(capture_file) or "."
    base_name = os.path.basename(capture_file)
    rdc_files = sorted(
        Path(base_dir).glob(f"{base_name}*.rdc"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )

    if not rdc_files:
        return f"Capture completed but no .rdc file found.\nCapture output: {cap_result}"

    rdc_path = str(rdc_files[0])
    analysis = await renderdoc_analyze_frame(rdc_path)
    return f"Captured: {rdc_path}\n\n{analysis}\n\nCapture output: {cap_result}"


@mcp.tool()
async def renderdoc_diff_captures(
    capture_file_a: str,
    capture_file_b: str,
    context_lines: int = 0,
) -> str:
    """Diff two .rdc captures by comparing their API call sequences.

    Exports both captures to XML and performs a structural diff, showing
    added/removed/changed API calls. Useful for regression analysis.

    Args:
        capture_file_a: Path to the first (baseline) .rdc capture.
        capture_file_b: Path to the second (new) .rdc capture.
        context_lines: Number of unchanged context lines around each diff hunk.
    """
    xml_a = await _export_xml(capture_file_a)
    xml_b = await _export_xml(capture_file_b)
    if xml_a is None:
        return f"ERROR: Failed to export {capture_file_a} to XML."
    if xml_b is None:
        return f"ERROR: Failed to export {capture_file_b} to XML."

    def _extract_call_names(xml_text: str) -> list[str]:
        chunks = _parse_chunks(xml_text)
        if chunks:
            return [c.get("name", c.text or c.tag) for c in chunks]
        return [l.strip() for l in xml_text.split("\n") if l.strip()]

    calls_a = _extract_call_names(xml_a)
    calls_b = _extract_call_names(xml_b)

    diff = list(difflib.unified_diff(
        calls_a, calls_b,
        fromfile=Path(capture_file_a).name,
        tofile=Path(capture_file_b).name,
        n=context_lines,
        lineterm="",
    ))

    if not diff:
        return (
            f"No differences found between captures.\n"
            f"  A: {len(calls_a)} API calls\n"
            f"  B: {len(calls_b)} API calls"
        )

    added = sum(1 for l in diff if l.startswith("+") and not l.startswith("+++"))
    removed = sum(1 for l in diff if l.startswith("-") and not l.startswith("---"))

    header = (
        f"Diff: {Path(capture_file_a).name} vs {Path(capture_file_b).name}\n"
        f"  A: {len(calls_a)} calls, B: {len(calls_b)} calls\n"
        f"  +{added} added, -{removed} removed\n\n"
    )

    # Limit output size
    diff_text = "\n".join(diff[:500])
    if len(diff) > 500:
        diff_text += f"\n... ({len(diff) - 500} more lines truncated)"

    return header + diff_text


@mcp.tool()
async def renderdoc_search_resource(
    capture_file: str,
    resource_keyword: str,
    max_results: int = 100,
) -> str:
    """Search for all references to a resource in a .rdc capture.

    Finds every API call that mentions the given resource keyword (buffer name,
    image handle, descriptor set, etc.) and returns them with event IDs.
    Useful for tracking resource lifecycle and usage across a frame.

    Args:
        capture_file: Path to the .rdc capture file.
        resource_keyword: Keyword to search for (e.g. handle value, buffer name).
        max_results: Maximum number of matches to return.
    """
    xml_text = await _export_xml(capture_file)
    if xml_text is None:
        return f"ERROR: Failed to export {capture_file} to XML."

    kw_lower = resource_keyword.lower()
    chunks = _parse_chunks(xml_text)
    results = []

    if chunks:
        for chunk in chunks:
            chunk_str = ET.tostring(chunk, encoding="unicode", method="xml")
            if kw_lower in chunk_str.lower():
                name = chunk.get("name", chunk.text or chunk.tag)
                eid = chunk.get("eventId", chunk.get("eid", "?"))
                # Extract matching child elements for context
                details = []
                for child in chunk:
                    child_text = ET.tostring(child, encoding="unicode", method="xml")
                    if kw_lower in child_text.lower():
                        val = child.text or child.get("value", "")
                        details.append(f"{child.tag}={val}")
                detail_str = f" [{', '.join(details)}]" if details else ""
                results.append(f"  [EID {eid}] {name}{detail_str}")
    else:
        for i, line in enumerate(xml_text.split("\n")):
            if kw_lower in line.lower():
                results.append(f"  [line {i+1}] {line.strip()}")

    total = len(results)
    results = results[:max_results]
    truncated = " (truncated)" if total > max_results else ""
    return (
        f"Found {total} references to '{resource_keyword}'{truncated}:\n"
        + "\n".join(results)
    )


@mcp.tool()
async def renderdoc_render_pass_tree(capture_file: str) -> str:
    """Show the render pass structure of a .rdc capture as a tree.

    Parses the API call sequence and builds a hierarchical view of render
    passes, showing nesting and the draw calls within each pass.

    Args:
        capture_file: Path to the .rdc capture file.
    """
    xml_text = await _export_xml(capture_file)
    if xml_text is None:
        return f"ERROR: Failed to export {capture_file} to XML."

    chunks = _parse_chunks(xml_text)
    if not chunks:
        return "ERROR: Could not parse API chunks from XML export."

    begin_re = re.compile(r"begin.*(?:render|pass|query|command|transform)", re.I)
    end_re = re.compile(r"end.*(?:render|pass|query|command|transform)", re.I)
    draw_re = re.compile(r"(draw|dispatch|clear|resolve|copy|blit)", re.I)

    lines = []
    depth = 0

    for chunk in chunks:
        name = chunk.get("name", chunk.text or chunk.tag)
        eid = chunk.get("eventId", chunk.get("eid", "?"))

        if end_re.search(name):
            depth = max(0, depth - 1)
            indent = "  " * depth
            lines.append(f"{indent}[EID {eid}] {name}")
        elif begin_re.search(name):
            indent = "  " * depth
            lines.append(f"{indent}[EID {eid}] {name}")
            depth += 1
        elif draw_re.search(name):
            indent = "  " * depth
            lines.append(f"{indent}[EID {eid}] {name}")

    if not lines:
        return "No render pass structure found."

    # Add summary
    pass_count = sum(1 for l in lines if "begin" in l.lower())
    draw_count = sum(1 for l in lines if draw_re.search(l))

    header = (
        f"# Render Pass Tree: {Path(capture_file).name}\n"
        f"Passes: {pass_count}, Draw/Dispatch/Clear: {draw_count}\n\n"
    )
    return header + "\n".join(lines)


@mcp.tool()
async def renderdoc_pipeline_state_at_eid(
    capture_file: str,
    target_eid: int,
) -> str:
    """Show the accumulated pipeline state at a given event ID.

    Scans all API calls up to the target EID and collects the most recent
    bind/set/push operations to reconstruct the pipeline state at that point.
    Shows bound pipeline, descriptor sets, vertex/index buffers, push constants,
    viewport, scissor, and blend state.

    Args:
        capture_file: Path to the .rdc capture file.
        target_eid: The event ID to inspect state at.
    """
    xml_text = await _export_xml(capture_file)
    if xml_text is None:
        return f"ERROR: Failed to export {capture_file} to XML."

    chunks = _parse_chunks(xml_text)
    if not chunks:
        return "ERROR: Could not parse API chunks from XML export."

    state_patterns = {
        "pipeline": re.compile(r"bind.*pipeline|create.*pipeline", re.I),
        "descriptor": re.compile(r"bind.*descriptor|push.*descriptor", re.I),
        "vertex_buffer": re.compile(r"bind.*vertex", re.I),
        "index_buffer": re.compile(r"bind.*index", re.I),
        "push_constants": re.compile(r"push.*constant", re.I),
        "viewport": re.compile(r"set.*viewport", re.I),
        "scissor": re.compile(r"set.*scissor", re.I),
        "blend": re.compile(r"set.*blend|blend.*factor", re.I),
        "depth_stencil": re.compile(r"set.*depth|depth.*bias|stencil", re.I),
        "render_pass": re.compile(r"begin.*render.*pass|begin.*rendering", re.I),
        "framebuffer": re.compile(r"framebuffer|attachment", re.I),
    }

    # Accumulate state up to target EID
    state: dict[str, list[str]] = {k: [] for k in state_patterns}
    target_call = None

    for chunk in chunks:
        eid_str = chunk.get("eventId", chunk.get("eid", ""))
        try:
            eid = int(eid_str)
        except (ValueError, TypeError):
            continue

        if eid > target_eid:
            break

        name = chunk.get("name", chunk.text or chunk.tag)
        params = []
        for child in chunk:
            val = child.text or child.get("value", "")
            if val:
                params.append(f"{child.tag}={val}")
        call_str = f"{name}({', '.join(params)})" if params else name

        if eid == target_eid:
            target_call = f"[EID {eid}] {call_str}"

        for cat, pat in state_patterns.items():
            if pat.search(name):
                state[cat] = [f"  [EID {eid}] {call_str}"]

    parts = [f"# Pipeline State at EID {target_eid}"]
    if target_call:
        parts.append(f"Target call: {target_call}")
    else:
        parts.append(f"(EID {target_eid} not found in capture)")
    parts.append("")

    any_state = False
    for cat, entries in state.items():
        if entries:
            any_state = True
            parts.append(f"## {cat}")
            parts.extend(entries)
            parts.append("")

    if not any_state:
        parts.append("No pipeline state changes found before this EID.")

    return "\n".join(parts)


@mcp.tool()
async def renderdoc_vulkanlayer(action: str = "check") -> str:
    """Manage RenderDoc Vulkan layer registration.

    RenderDoc needs its Vulkan layer registered to capture Vulkan applications.
    This tool checks or fixes the registration.

    Args:
        action: 'check' to check status, 'register' to register/fix the layer.
    """
    if action == "register":
        return await _run(["vulkanlayer", "--register"])
    return await _run(["vulkanlayer"])


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()

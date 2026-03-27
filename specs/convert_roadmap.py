"""Convert a Markdown file to styled HTML, or rasterize a PDF to a high-res image PDF.

Usage:
    python convert_roadmap.py                              # md -> html (defaults)
    python convert_roadmap.py -i spec.md -o spec.html      # custom md -> html
    python convert_roadmap.py -p input.pdf                  # pdf -> image pdf (600 DPI)
    python convert_roadmap.py -p input.pdf --dpi 300        # pdf -> image pdf (custom DPI)
    python convert_roadmap.py -p input.pdf -o out.pdf       # pdf -> image pdf (custom output)
"""

import argparse
import markdown
import re
import os
import sys

from pygments.lexers.graphics import HLSLShaderLexer
from pygments.token import Keyword, Name, Name as N
from pygments.lexer import words, bygroups
import pygments.lexers._mapping as _mapping


# ---------------------------------------------------------------------------
# SlangLexer: extends HLSL with Slang-specific keywords & syntax
# ---------------------------------------------------------------------------
class SlangLexer(HLSLShaderLexer):
    """Pygments lexer for the Slang shading language (superset of HLSL)."""
    name = 'Slang'
    aliases = ['slang']
    filenames = ['*.slang']
    mimetypes = ['text/x-slang']
    url = 'https://shader-slang.com'
    version_added = ''

    # Slang-specific keywords not in HLSL
    _slang_keywords = (
        'import', '__exported', '__include', 'module', 'implementing',
        'let', 'var', 'func', 'typealias', 'associatedtype', 'typedef',
        'This', 'extension', 'property', 'subscript',
        '__init', '__subscript', '__generic',
        'is', 'as',
        'public', 'internal', 'private', 'fileprivate', 'open',
        'mutating', 'nonmutating',
        '__target_switch', '__intrinsic_asm',
        'no_diff', 'detach',
        'each', 'expand',
        'throws', 'try', 'throw',
        'where', 'satisfies',
    )

    _slang_types = (
        'Optional', 'Result', 'Array', 'Ptr', 'NativeRef',
        'ParameterBlock', 'ConstantBuffer',
        'DifferentialPair', 'InOut',
        'RaytracingAccelerationStructure', 'RayDesc', 'RayQuery',
        'BuiltInTriangleIntersectionAttributes',
        'SubpassInput', 'SubpassInputMS',
    )

    _slang_builtins = (
        'fwd_diff', 'bwd_diff', 'diffPair', 'getDifferential',
        'getPrimal', 'bit_cast', 'reinterpret', 'sizeof', 'alignof',
        'createDynamicObject', 'makeArray',
        'WaveActiveSum', 'WaveActiveProduct', 'WaveActiveMax', 'WaveActiveMin',
        'WaveActiveBitAnd', 'WaveActiveBitOr', 'WaveActiveBitXor',
        'WaveActiveAllEqual', 'WaveActiveCountBits',
        'WavePrefixCountBits', 'WavePrefixProduct', 'WavePrefixSum',
        'WaveMultiPrefixSum', 'WaveMultiPrefixProduct',
        'WaveReadLaneFirst',
        'InterlockedAddF32',
    )

    _slang_decorators = (
        'Differentiable', 'PreferRecompute', 'ForwardDerivative',
        'BackwardDerivative', 'TreatAsDifferentiable',
        'shader', 'numthreads', 'outputtopology', 'maxvertexcount',
        'domain', 'partitioning', 'outputcontrolpoints', 'patchconstantfunc',
        'maxtessfactor',
        'vk_binding', 'vk_location', 'vk_push_constant',
        'allow_uav_condition', 'unroll', 'loop', 'branch', 'flatten',
        'earlydepthstencil', 'forcecase', 'call',
        'payload', 'intersection', 'anyhit', 'closesthit', 'miss',
        'callable', 'raygeneration',
        'UserDefinedAttribute',
    )

    def __init__(self, **options):
        super().__init__(**options)
        # Prepend Slang-specific rules before the catch-all Name rule
        import pygments.token as tok
        slang_rules = [
            (words(self._slang_keywords, prefix=r'\b', suffix=r'\b'), tok.Keyword),
            (words(self._slang_types, prefix=r'\b', suffix=r'\b'), tok.Keyword.Type),
            (words(self._slang_builtins, prefix=r'\b', suffix=r'\b'), tok.Name.Builtin),
            (words(self._slang_decorators, prefix=r'\b', suffix=r'\b'), tok.Name.Decorator),
            # [attr(...)] style attributes
            (r'\[\w+(?:\([^)]*\))?\]', tok.Name.Decorator),
        ]
        # Insert before the last two rules (Name catch-all and whitespace)
        root = list(self._tokens['root'])
        # Find the index of the generic Name catch-all: (r'[a-zA-Z_]\w*', Name)
        insert_idx = None
        for i, rule in enumerate(root):
            if len(rule) >= 2 and rule[0] == r'[a-zA-Z_]\w*':
                insert_idx = i
                break
        if insert_idx is not None:
            for j, sr in enumerate(slang_rules):
                root.insert(insert_idx + j, sr)
        self._tokens = dict(self._tokens)
        self._tokens['root'] = root


# Register SlangLexer so Pygments and codehilite can find it by alias 'slang'
def _register_slang_lexer():
    """Register the SlangLexer into Pygments' runtime lexer cache.

    We monkey-patch get_lexer_by_name so that 'slang' resolves to our
    custom SlangLexer without touching _mapping (which would try to
    import __main__ and fail).
    """
    import pygments.lexers as _lex
    _original = _lex.get_lexer_by_name

    def _patched(alias, **options):
        if alias == 'slang':
            return SlangLexer(**options)
        return _original(alias, **options)

    _lex.get_lexer_by_name = _patched
    # Also patch the module-level import that codehilite uses
    import pygments.lexers
    pygments.lexers.get_lexer_by_name = _patched


def _default_path(name: str) -> str:
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), name)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Convert Markdown to styled HTML, or rasterize PDF.")
    p.add_argument("-i", "--input", default=_default_path("roadmap.md"),
                   help="Input Markdown file (default: roadmap.md)")
    p.add_argument("-o", "--output", default=None,
                   help="Output file (default: <input-stem>.html or *.image.pdf)")
    p.add_argument("-p", "--pdf-image", default=None, metavar="PDF",
                   help="Rasterize a PDF to high-res image PDF instead of md->html")
    p.add_argument("--dpi", type=int, default=600,
                   help="DPI for PDF rasterization (default: 600)")
    args = p.parse_args(argv)
    if args.pdf_image:
        if args.output is None:
            stem, _ = os.path.splitext(args.pdf_image)
            args.output = stem + " (image).pdf"
    else:
        if args.output is None:
            stem, _ = os.path.splitext(args.input)
            args.output = stem + ".html"
    return args


def convert_mermaid(text: str) -> str:
    """Convert ```mermaid code blocks to <div class="mermaid"> for client-side rendering.

    Handles both top-level and blockquote-embedded (> ```mermaid) code blocks.
    """
    # --- Pass 1: blockquote-embedded mermaid blocks ---
    # Match lines like:  > ```mermaid ... > ```
    bq_pattern = r'(?m)(^>[ ]?```mermaid[ ]*\n)((?:^>.*\n)*?)(^>[ ]?```[ ]*$)'
    def bq_replacer(m):
        # Strip leading "> " or ">" from each content line
        raw_lines = m.group(2).splitlines(True)
        cleaned = []
        for line in raw_lines:
            stripped = re.sub(r'^>[ ]?', '', line)
            cleaned.append(stripped)
        content = ''.join(cleaned)
        return f'<div class="mermaid">\n{content}</div>'
    text = re.sub(bq_pattern, bq_replacer, text)

    # --- Pass 2: top-level mermaid blocks ---
    pattern = r'```mermaid\s*\n(.*?)```'
    def replacer(m):
        return f'<div class="mermaid">\n{m.group(1)}</div>'
    return re.sub(pattern, replacer, text, flags=re.DOTALL)


def convert_math(text: str) -> str:
    """Convert LaTeX math delimiters to KaTeX-compatible <span>/<div> elements.

    - Display math: $$...$$ -> <div class="katex-display">...</div>
    - Inline math:  $...$  -> <span class="katex-inline">...</span>
    Avoids matching inside code blocks or already-converted HTML.
    """
    # Display math ($$...$$) — must come first
    text = re.sub(
        r'\$\$([^$]+?)\$\$',
        r'<div class="katex-display" data-katex="\1"></div>',
        text, flags=re.DOTALL
    )
    # Inline math ($...$) — negative lookbehind/ahead to avoid $$
    text = re.sub(
        r'(?<!\$)\$(?!\$)([^$\n]+?)(?<!\$)\$(?!\$)',
        r'<span class="katex-inline" data-katex="\1"></span>',
        text
    )
    return text


MD_EXTENSIONS = [
    'markdown.extensions.tables',
    'markdown.extensions.fenced_code',
    'markdown.extensions.toc',
    'markdown.extensions.codehilite',
    'markdown.extensions.attr_list',
    'markdown.extensions.def_list',
    'markdown.extensions.md_in_html',
]

MD_EXTENSION_CONFIGS = {
    'markdown.extensions.codehilite': {
        'css_class': 'codehilite',
        'guess_lang': False,
        'use_pygments': True,
    },
}


def convert(input_path: str, output_path: str) -> None:
    _register_slang_lexer()

    with open(input_path, "r", encoding="utf-8") as f:
        md_text = f.read()

    md_text = convert_mermaid(md_text)
    md_text = convert_math(md_text)
    html_body = markdown.markdown(md_text, extensions=MD_EXTENSIONS,
                                  extension_configs=MD_EXTENSION_CONFIGS)
    title = os.path.splitext(os.path.basename(input_path))[0].replace("_", " ").title()

    html_full = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{title}</title>
<script src="https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js"></script>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css">
<script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js"></script>
<script>
mermaid.initialize({{
  startOnLoad: true,
  theme: 'base',
  themeVariables: {{
    // Pastel Rainbow palette
    primaryColor:       '#bdb2ff',  // Periwinkle — default node fill
    primaryTextColor:   '#2b2b3a',  // dark text
    primaryBorderColor: '#9b8ecf',  // darker periwinkle border
    secondaryColor:     '#caffbf',  // Tea Green
    secondaryTextColor: '#2b2b3a',
    secondaryBorderColor:'#8ad4a0',
    tertiaryColor:      '#ffd6a5',  // Apricot Cream
    tertiaryTextColor:  '#2b2b3a',
    tertiaryBorderColor:'#e0b080',
    lineColor:          '#9b8ecf',  // edge lines — darker periwinkle
    textColor:          '#2b2b3a',
    mainBkg:            '#bdb2ff',  // flowchart node bg
    nodeBorder:         '#9b8ecf',
    clusterBkg:         '#f4f0ff',  // subgraph bg
    clusterBorder:      '#bdb2ff',
    titleColor:         '#5040a0',
    edgeLabelBackground:'#ffffff',
    // Sequence diagram
    actorBkg:           '#a0c4ff',  // Baby Blue Ice
    actorBorder:        '#7098d0',
    actorTextColor:     '#2b2b3a',
    activationBorderColor:'#9b8ecf',
    activationBkg:      '#e8e4ff',
    signalColor:        '#5040a0',
    signalTextColor:    '#2b2b3a',
    labelBoxBkgColor:   '#ffd6a5',
    labelBoxBorderColor:'#e0b080',
    labelTextColor:     '#2b2b3a',
    loopTextColor:      '#5040a0',
    noteBkgColor:       '#fdffb6',  // Lemon Chiffon
    noteBorderColor:    '#e0e0a0',
    noteTextColor:      '#2b2b3a',
    // Gantt
    sectionBkgColor:    '#caffbf',
    altSectionBkgColor: '#9bf6ff',
    gridColor:          '#e8e4f0',
    todayLineColor:     '#ffadad',
    // Pie
    pie1: '#ffadad', pie2: '#ffd6a5', pie3: '#fdffb6', pie4: '#caffbf',
    pie5: '#9bf6ff', pie6: '#a0c4ff', pie7: '#bdb2ff', pie8: '#ffc6ff',
    // General
    fontSize: '14px',
    fontFamily: 'Inter, sans-serif'
  }},
  flowchart: {{ useMaxWidth: true, htmlLabels: true, curve: 'basis' }},
  sequence: {{ useMaxWidth: true }},
  gantt: {{ useMaxWidth: true }}
}});
</script>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap');

  /* ===== Pastel Rainbow palette =====
     Powder Blush, Apricot Cream, Lemon Chiffon, Tea Green,
     Soft Cyan, Baby Blue Ice, Periwinkle, Mauve, Porcelain.
     White background, white code blocks. */

  :root {{
    --bg: #ffffff;
    --fg: #2b2b3a;
    --c-blush: #ffadad;        /* Powder Blush */
    --c-apricot: #ffd6a5;      /* Apricot Cream */
    --c-lemon: #fdffb6;        /* Lemon Chiffon */
    --c-tea: #caffbf;          /* Tea Green */
    --c-cyan: #9bf6ff;         /* Soft Cyan */
    --c-baby: #a0c4ff;         /* Baby Blue Ice */
    --c-peri: #bdb2ff;         /* Periwinkle */
    --c-mauve: #ffc6ff;        /* Mauve */
    --c-porcelain: #fffffc;    /* Porcelain */
    /* darker tints for text (pastel bg needs readable text) */
    --t-blush: #b04050;
    --t-apricot: #9a6020;
    --t-tea: #2a7a3a;
    --t-cyan: #0a7a8a;
    --t-baby: #2a4a8a;
    --t-peri: #5040a0;
    --t-mauve: #8a2a8a;
    --border: #e8e4f0;
    --code-bg: #ffffff;
    --code-fg: #5040a0;
    --pre-bg: #ffffff;
    --pre-fg: #2b2b3a;
    --table-header-fg: #2b2b3a;
    --table-stripe: #fafaff;
    --table-hover: #f4f0ff;
    --blockquote-bg: #fff8f0;
    --blockquote-border: #ffd6a5;
    --shadow: 0 1px 4px rgba(0,0,0,0.05);
    --rainbow: linear-gradient(90deg,
      #ffadad, #ffd6a5, #fdffb6, #caffbf, #9bf6ff, #a0c4ff, #bdb2ff, #ffc6ff);
  }}

  /* ===== PRINT: force all colors ===== */
  *, *::before, *::after {{
    -webkit-print-color-adjust: exact !important;
    print-color-adjust: exact !important;
    color-adjust: exact !important;
  }}

  @media print {{
    @page {{ size: A4; margin: 10mm; }}
    body {{
      font-size: 8.5pt;
      line-height: 1.35;
      padding: 0 !important;
      max-width: none !important;
    }}
    h1 {{ font-size: 16pt; margin-top: 0.6em !important; color: #2a4a8a !important; }}
    h2 {{ font-size: 13pt; color: #5040a0 !important; }}
    h3 {{ font-size: 11pt; break-after: avoid; color: #b04050 !important; }}
    h4 {{ font-size: 9.5pt; break-after: avoid; color: #8a2a8a !important; }}
    table {{
      font-size: 7.5pt;
      break-inside: auto;
      box-shadow: none !important;
      border: 1px solid #ddd;
    }}
    thead {{ display: table-header-group; }}
    tr {{ break-inside: avoid; }}
    thead th {{ background: #ffadad !important; color: #2b2b3a !important; }}
    thead th:nth-child(8n+2) {{ background: #ffd6a5 !important; }}
    thead th:nth-child(8n+3) {{ background: #fdffb6 !important; }}
    thead th:nth-child(8n+4) {{ background: #caffbf !important; }}
    thead th:nth-child(8n+5) {{ background: #9bf6ff !important; }}
    thead th:nth-child(8n+6) {{ background: #a0c4ff !important; }}
    thead th:nth-child(8n+7) {{ background: #bdb2ff !important; }}
    thead th:nth-child(8n+8) {{ background: #ffc6ff !important; }}
    tbody tr:nth-child(even) {{ background: #fafaff !important; }}
    pre {{
      font-size: 7pt;
      break-inside: avoid;
      background: #ffffff !important;
      color: #2b2b3a !important;
      box-shadow: none !important;
      border: 1px solid #e8e4f0 !important;
    }}
    code {{ background: #ffffff !important; color: #5040a0 !important; }}
    blockquote {{ background: #fff8f0 !important; border-left-color: #ffd6a5 !important; }}
    .mermaid {{
      break-inside: avoid;
      border: 1px solid #e8e4f0 !important;
    }}
    .mermaid svg {{ max-width: 100% !important; }}
    hr {{
      height: 2px !important;
      background: linear-gradient(90deg, #ffadad, #ffd6a5, #fdffb6, #caffbf, #9bf6ff, #a0c4ff, #bdb2ff, #ffc6ff) !important;
    }}
    a {{ color: #2a4a8a !important; }}
    strong {{ color: #0a7a8a !important; }}
    tbody td:first-child {{ color: #2a7a3a !important; }}
  }}

  * {{ margin: 0; padding: 0; box-sizing: border-box; }}

  body {{
    font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    color: var(--fg);
    background: var(--bg);
    line-height: 1.65;
    padding: 12mm;
    max-width: 1100px;
    margin: 0 auto;
  }}

  /* === Headings === */
  h1 {{
    font-size: 2em;
    font-weight: 700;
    color: var(--t-baby);
    padding-bottom: 0.3em;
    margin: 1.2em 0 0.5em;
    border-bottom: 3px solid var(--c-baby);
  }}
  h1:first-child {{ margin-top: 0; }}

  h2 {{
    font-size: 1.45em;
    font-weight: 600;
    color: var(--t-peri);
    border-bottom: 2px solid var(--c-peri);
    padding-bottom: 0.2em;
    margin: 1.4em 0 0.5em;
  }}

  h3 {{
    font-size: 1.18em;
    font-weight: 600;
    color: var(--t-blush);
    margin: 1.1em 0 0.35em;
    padding-left: 0.6em;
    border-left: 3px solid var(--c-blush);
  }}

  h4 {{
    font-size: 1.02em;
    font-weight: 600;
    color: var(--t-mauve);
    margin: 0.9em 0 0.3em;
  }}

  p {{ margin: 0.45em 0; }}

  strong {{
    font-weight: 600;
    color: var(--t-cyan);
  }}

  a {{
    color: var(--t-baby);
    text-decoration: none;
    border-bottom: 1px dotted var(--c-baby);
    transition: color 0.2s;
  }}
  a:hover {{
    color: var(--t-blush);
    border-bottom-color: var(--c-blush);
  }}

  /* === Blockquotes: apricot warmth === */
  blockquote {{
    background: var(--blockquote-bg);
    border-left: 4px solid var(--blockquote-border);
    padding: 0.8em 1em;
    margin: 0.8em 0;
    border-radius: 0 6px 6px 0;
    font-style: italic;
    color: #4a4040;
  }}
  blockquote p {{ margin: 0.2em 0; }}
  blockquote strong {{ color: var(--t-apricot); }}

  /* === Inline code: white bg, periwinkle text === */
  code {{
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    background: var(--code-bg);
    color: var(--code-fg);
    padding: 0.15em 0.4em;
    border-radius: 4px;
    font-size: 0.87em;
    border: 1px solid var(--border);
  }}

  /* === Code blocks: white bg === */
  pre {{
    background: var(--pre-bg);
    color: var(--pre-fg);
    padding: 1em 1.2em;
    border-radius: 8px;
    overflow-x: auto;
    margin: 0.8em 0;
    line-height: 1.5;
    border: 1px solid var(--border);
  }}
  pre code {{
    background: none;
    border: none;
    padding: 0;
    color: inherit;
    font-size: 0.84em;
  }}

  /* === Pygments syntax highlighting (pastel-rainbow theme) === */
  .codehilite {{ background: var(--pre-bg); }}
  .codehilite .hll {{ background-color: #f4f0ff; }}
  .codehilite .c,
  .codehilite .ch,
  .codehilite .cm,
  .codehilite .c1,
  .codehilite .cs,
  .codehilite .cpf {{ color: #7a8a7a; font-style: italic; }} /* Comment */
  .codehilite .cp  {{ color: #9a6020; font-weight: 500; }}   /* Preprocessor */
  .codehilite .k,
  .codehilite .kd,
  .codehilite .kn,
  .codehilite .kr  {{ color: #5040a0; font-weight: 600; }}   /* Keyword */
  .codehilite .kc  {{ color: #0a7a8a; font-weight: 600; }}   /* Keyword.Constant */
  .codehilite .kt  {{ color: #b04050; font-weight: 500; }}   /* Keyword.Type */
  .codehilite .kp  {{ color: #5040a0; }}                     /* Keyword.Pseudo */
  .codehilite .o,
  .codehilite .ow  {{ color: #666; }}                        /* Operator */
  .codehilite .p   {{ color: #555; }}                        /* Punctuation */
  .codehilite .m,
  .codehilite .mb,
  .codehilite .mf,
  .codehilite .mh,
  .codehilite .mi,
  .codehilite .mo,
  .codehilite .il  {{ color: #0a7a8a; }}                     /* Number */
  .codehilite .s,
  .codehilite .sa,
  .codehilite .sb,
  .codehilite .sc,
  .codehilite .dl,
  .codehilite .s2,
  .codehilite .sh,
  .codehilite .s1,
  .codehilite .ss  {{ color: #9a3030; }}                     /* String */
  .codehilite .se  {{ color: #aa5d1f; font-weight: 600; }}   /* String.Escape */
  .codehilite .sd  {{ color: #7a8a7a; font-style: italic; }} /* String.Doc */
  .codehilite .nb,
  .codehilite .bp  {{ color: #2a7a3a; }}                     /* Name.Builtin */
  .codehilite .nf,
  .codehilite .fm  {{ color: #2a4a8a; }}                     /* Name.Function */
  .codehilite .nc  {{ color: #b04050; font-weight: 600; }}   /* Name.Class */
  .codehilite .nd  {{ color: #8a2a8a; }}                     /* Name.Decorator */
  .codehilite .nn  {{ color: #2a4a8a; font-weight: 600; }}   /* Name.Namespace */
  .codehilite .no  {{ color: #800; }}                        /* Name.Constant */
  .codehilite .ni  {{ color: #717171; font-weight: 600; }}   /* Name.Entity */
  .codehilite .ne  {{ color: #cb3f38; font-weight: 600; }}   /* Name.Exception */
  .codehilite .nv,
  .codehilite .vc,
  .codehilite .vg,
  .codehilite .vi,
  .codehilite .vm  {{ color: #19177c; }}                     /* Name.Variable */
  .codehilite .nl  {{ color: #767600; }}                     /* Name.Label */
  .codehilite .nt  {{ color: #5040a0; font-weight: 600; }}   /* Name.Tag */
  .codehilite .na  {{ color: #687822; }}                     /* Name.Attribute */
  .codehilite .ge  {{ font-style: italic; }}                 /* Generic.Emph */
  .codehilite .gs  {{ font-weight: bold; }}                  /* Generic.Strong */
  .codehilite .gd  {{ color: #a00000; }}                     /* Generic.Deleted */
  .codehilite .gi  {{ color: #008400; }}                     /* Generic.Inserted */
  .codehilite .w   {{ color: #bbb; }}                        /* Whitespace */
  .codehilite .err {{ border: none; color: inherit; }}       /* Error: suppress red box */

  /* === Tables: pastel rainbow cycling header === */
  table {{
    width: 100%;
    border-collapse: separate;
    border-spacing: 0;
    margin: 0.8em 0;
    border-radius: 8px;
    overflow: hidden;
    box-shadow: var(--shadow);
    font-size: 0.91em;
    border: 1px solid var(--border);
  }}

  thead th {{
    background: var(--c-blush);
    color: var(--table-header-fg);
    font-weight: 600;
    text-align: left;
    padding: 0.55em 0.75em;
    white-space: nowrap;
    letter-spacing: 0.02em;
  }}
  /* 8-color pastel rainbow cycle */
  thead th:nth-child(8n+2) {{ background: var(--c-apricot); }}
  thead th:nth-child(8n+3) {{ background: var(--c-lemon); }}
  thead th:nth-child(8n+4) {{ background: var(--c-tea); }}
  thead th:nth-child(8n+5) {{ background: var(--c-cyan); }}
  thead th:nth-child(8n+6) {{ background: var(--c-baby); }}
  thead th:nth-child(8n+7) {{ background: var(--c-peri); }}
  thead th:nth-child(8n+8) {{ background: var(--c-mauve); }}

  tbody td {{
    padding: 0.45em 0.75em;
    border-bottom: 1px solid var(--border);
    vertical-align: top;
  }}
  tbody td:first-child {{
    color: var(--t-tea);
    font-weight: 500;
  }}

  tbody tr:nth-child(even) {{
    background: var(--table-stripe);
  }}

  tbody tr:hover {{
    background: var(--table-hover);
    transition: background 0.15s;
  }}

  tbody tr:last-child td {{
    border-bottom: none;
  }}

  /* === Lists === */
  ul, ol {{
    padding-left: 1.5em;
    margin: 0.4em 0;
  }}
  li {{ margin: 0.2em 0; }}
  li > ul, li > ol {{ margin: 0.1em 0; }}
  ul > li::marker {{ color: var(--t-blush); }}
  ol > li::marker {{ color: var(--t-baby); font-weight: 600; }}

  /* === HR: pastel rainbow (only gradient) === */
  hr {{
    border: none;
    height: 3px;
    background: var(--rainbow);
    margin: 2em 0;
    border-radius: 2px;
  }}

  /* === Mermaid: no fill container === */
  .mermaid {{
    text-align: center;
    margin: 1em 0;
    padding: 0.8em;
    background: transparent;
    border-radius: 8px;
    border: 1px solid var(--border);
  }}
  /* Override mermaid SVG internals for palette consistency */
  .mermaid .label {{ font-family: 'Inter', sans-serif !important; }}
  .mermaid .edgeLabel {{ font-family: 'Inter', sans-serif !important; font-size: 12px !important; }}
  .mermaid .cluster rect {{ rx: 6; ry: 6; }}
  .mermaid text {{ fill: #2b2b3a !important; }}

  /* === Responsive === */
  @media (max-width: 800px) {{
    body {{ padding: 6mm; }}
    table {{ font-size: 0.82em; }}
    thead th, tbody td {{ padding: 0.35em 0.45em; }}
  }}
</style>
</head>
<body>
{html_body}
<script>
// Post-render: (1) cycle node fills through pastel rainbow, (2) scale tall SVGs.
document.addEventListener('DOMContentLoaded', function() {{
  var PASTELS = [
    '#ffadad', '#ffd6a5', '#fdffb6', '#caffbf',
    '#9bf6ff', '#a0c4ff', '#bdb2ff', '#ffc6ff'
  ];
  var BORDERS = [
    '#e08090', '#e0b080', '#e0e0a0', '#8ad4a0',
    '#70d4e0', '#7098d0', '#9b8ecf', '#d090d0'
  ];

  setTimeout(function() {{
    // --- 1. Cycle node fills per diagram ---
    document.querySelectorAll('.mermaid svg').forEach(function(svg) {{
      // Flowchart nodes: .node rect, .node polygon, .node circle
      var nodes = svg.querySelectorAll('.node rect, .node polygon, .node circle, .node .label-container');
      nodes.forEach(function(node, i) {{
        var ci = i % PASTELS.length;
        node.style.fill = PASTELS[ci];
        node.style.stroke = BORDERS[ci];
      }});

      // Cluster/subgraph backgrounds
      var clusters = svg.querySelectorAll('.cluster rect');
      clusters.forEach(function(rect, i) {{
        rect.style.fill = '#f8f6ff';
        rect.style.stroke = '#bdb2ff';
        rect.style.rx = '8';
        rect.style.ry = '8';
      }});

      // Edge labels background
      svg.querySelectorAll('.edgeLabel rect, .edgeLabel polygon').forEach(function(el) {{
        el.style.fill = '#ffffff';
        el.style.stroke = 'none';
      }});

      // --- 2. Scale tall/thin SVGs ---
      var vb = svg.getAttribute('viewBox');
      if (!vb) return;
      var parts = vb.split(/[\\s,]+/).map(Number);
      var vbW = parts[2], vbH = parts[3];
      if (!vbW || !vbH) return;

      var aspectRatio = vbH / vbW;
      var container = svg.parentElement;
      var containerW = container.offsetWidth || 700;

      if (aspectRatio > 1.5) {{
        var naturalH = containerW * aspectRatio;
        var maxH = 900;
        if (naturalH > maxH) {{
          var scale = maxH / naturalH;
          svg.style.width = (containerW * scale) + 'px';
          svg.style.height = maxH + 'px';
          svg.style.maxWidth = '100%';
        }}
      }}

      svg.style.maxWidth = '100%';
      svg.removeAttribute('height');
    }});
  }}, 2000);

  // --- KaTeX auto-render ---
  function renderKatex() {{
    document.querySelectorAll('[data-katex]').forEach(function(el) {{
      var tex = el.getAttribute('data-katex');
      var displayMode = el.classList.contains('katex-display');
      try {{
        katex.render(tex, el, {{ displayMode: displayMode, throwOnError: false }});
      }} catch (e) {{
        el.textContent = tex;
        el.style.color = 'red';
      }}
    }});
  }}
  // katex.js is defer-loaded; wait for it
  if (typeof katex !== 'undefined') {{
    renderKatex();
  }} else {{
    document.addEventListener('DOMContentLoaded', function() {{
      var interval = setInterval(function() {{
        if (typeof katex !== 'undefined') {{
          clearInterval(interval);
          renderKatex();
        }}
      }}, 100);
    }});
  }}
}});
</script>
</body>
</html>
"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html_full)

    print(f"HTML written to {output_path}")
    print(f"Size: {os.path.getsize(output_path) / 1024:.0f} KB")


def pdf_to_images(input_path: str, output_path: str, dpi: int = 600) -> None:
    """Rasterize every page of a PDF at the given DPI and reassemble into a new image-only PDF."""
    try:
        import fitz  # PyMuPDF
    except ImportError:
        print("Error: PyMuPDF is required for PDF rasterization. Install with: pip install PyMuPDF",
              file=sys.stderr)
        sys.exit(1)

    zoom = dpi / 72.0
    mat = fitz.Matrix(zoom, zoom)

    src = fitz.open(input_path)
    total = len(src)
    dst = fitz.open()

    print(f"Rasterizing {total} pages at {dpi} DPI ...")
    for i, page in enumerate(src):
        pix = page.get_pixmap(matrix=mat, alpha=False)
        img_page = dst.new_page(width=page.rect.width, height=page.rect.height)
        img_page.insert_image(img_page.rect, pixmap=pix)
        if (i + 1) % 10 == 0 or i == 0 or i == total - 1:
            print(f"  Page {i+1}/{total} ({pix.width}x{pix.height} px)")

    dst.save(output_path, deflate=True, garbage=4)
    dst.close()
    src.close()

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(f"Done! {total} pages -> {output_path} ({size_mb:.1f} MB)")


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    if args.pdf_image:
        if not os.path.isfile(args.pdf_image):
            print(f"Error: PDF not found: {args.pdf_image}", file=sys.stderr)
            sys.exit(1)
        pdf_to_images(args.pdf_image, args.output, args.dpi)
    else:
        if not os.path.isfile(args.input):
            print(f"Error: input file not found: {args.input}", file=sys.stderr)
            sys.exit(1)
        convert(args.input, args.output)


if __name__ == "__main__":
    main()

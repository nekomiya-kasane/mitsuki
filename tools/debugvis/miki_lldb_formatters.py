"""
miki_lldb_formatters.py — LLDB type summaries and synthetics for miki renderer.

Load via:
  command script import D:/repos/miki/tools/debugvis/miki_lldb_formatters.py

Or add to launch.json initCommands:
  "initCommands": ["command script import ${workspaceFolder}/tools/debugvis/miki_lldb_formatters.py"]

Covers miki::core::Result<T> (libc++ std::expected): one-line ok:/err: summary plus
flattened has_value + value/error children (see ResultSyntheticProvider).
"""

import lldb


# ============================================================================
# Helpers
# ============================================================================

def _float_val(sbval):
    """Extract float from SBValue."""
    return sbval.GetValueAsFloat() if sbval.IsValid() else 0.0


def _uint_val(sbval):
    """Extract unsigned from SBValue."""
    return sbval.GetValueAsUnsigned(0) if sbval.IsValid() else 0


# ============================================================================
# miki::core math types
# ============================================================================

def float2_summary(valobj, internal_dict):
    x = _float_val(valobj.GetChildMemberWithName("x"))
    y = _float_val(valobj.GetChildMemberWithName("y"))
    return f"({x:.4g}, {y:.4g})"


def float3_summary(valobj, internal_dict):
    x = _float_val(valobj.GetChildMemberWithName("x"))
    y = _float_val(valobj.GetChildMemberWithName("y"))
    z = _float_val(valobj.GetChildMemberWithName("z"))
    return f"({x:.4g}, {y:.4g}, {z:.4g})"


def float4_summary(valobj, internal_dict):
    x = _float_val(valobj.GetChildMemberWithName("x"))
    y = _float_val(valobj.GetChildMemberWithName("y"))
    z = _float_val(valobj.GetChildMemberWithName("z"))
    w = _float_val(valobj.GetChildMemberWithName("w"))
    return f"({x:.4g}, {y:.4g}, {z:.4g}, {w:.4g})"


def float4x4_summary(valobj, internal_dict):
    cols = valobj.GetChildMemberWithName("columns")
    if not cols.IsValid():
        return "float4x4 {?}"
    diag = []
    for i in range(4):
        col = cols.GetChildAtIndex(i)
        # diagonal element: col[i].{x,y,z,w}
        names = ["x", "y", "z", "w"]
        elem = col.GetChildMemberWithName(names[i])
        diag.append(_float_val(elem))
    return f"float4x4 diag=({diag[0]:.3g}, {diag[1]:.3g}, {diag[2]:.3g}, {diag[3]:.3g})"


def int2_summary(valobj, internal_dict):
    x = valobj.GetChildMemberWithName("x").GetValueAsSigned(0)
    y = valobj.GetChildMemberWithName("y").GetValueAsSigned(0)
    return f"({x}, {y})"


def uint2_summary(valobj, internal_dict):
    x = _uint_val(valobj.GetChildMemberWithName("x"))
    y = _uint_val(valobj.GetChildMemberWithName("y"))
    return f"({x}, {y})"


def uint3_summary(valobj, internal_dict):
    x = _uint_val(valobj.GetChildMemberWithName("x"))
    y = _uint_val(valobj.GetChildMemberWithName("y"))
    z = _uint_val(valobj.GetChildMemberWithName("z"))
    return f"({x}, {y}, {z})"


def uint4_summary(valobj, internal_dict):
    x = _uint_val(valobj.GetChildMemberWithName("x"))
    y = _uint_val(valobj.GetChildMemberWithName("y"))
    z = _uint_val(valobj.GetChildMemberWithName("z"))
    w = _uint_val(valobj.GetChildMemberWithName("w"))
    return f"({x}, {y}, {z}, {w})"


# ============================================================================
# miki::core geometry types
# ============================================================================

def aabb_summary(valobj, internal_dict):
    mn = valobj.GetChildMemberWithName("min")
    mx = valobj.GetChildMemberWithName("max")
    mnx = _float_val(mn.GetChildMemberWithName("x"))
    mny = _float_val(mn.GetChildMemberWithName("y"))
    mnz = _float_val(mn.GetChildMemberWithName("z"))
    mxx = _float_val(mx.GetChildMemberWithName("x"))
    mxy = _float_val(mx.GetChildMemberWithName("y"))
    mxz = _float_val(mx.GetChildMemberWithName("z"))
    return f"AABB [({mnx:.3g},{mny:.3g},{mnz:.3g})..({mxx:.3g},{mxy:.3g},{mxz:.3g})]"


def bounding_sphere_summary(valobj, internal_dict):
    c = valobj.GetChildMemberWithName("center")
    x = _float_val(c.GetChildMemberWithName("x"))
    y = _float_val(c.GetChildMemberWithName("y"))
    z = _float_val(c.GetChildMemberWithName("z"))
    r = _float_val(c.GetChildMemberWithName("_pad"))
    return f"Sphere c=({x:.3g},{y:.3g},{z:.3g}) r={r:.3g}"


def ray_summary(valobj, internal_dict):
    o = valobj.GetChildMemberWithName("origin")
    d = valobj.GetChildMemberWithName("direction")
    return (f"Ray o=({_float_val(o.GetChildMemberWithName('x')):.3g},"
            f"{_float_val(o.GetChildMemberWithName('y')):.3g},"
            f"{_float_val(o.GetChildMemberWithName('z')):.3g}) "
            f"d=({_float_val(d.GetChildMemberWithName('x')):.3g},"
            f"{_float_val(d.GetChildMemberWithName('y')):.3g},"
            f"{_float_val(d.GetChildMemberWithName('z')):.3g})")


def plane_summary(valobj, internal_dict):
    n = valobj.GetChildMemberWithName("normal")
    nx = _float_val(n.GetChildMemberWithName("x"))
    ny = _float_val(n.GetChildMemberWithName("y"))
    nz = _float_val(n.GetChildMemberWithName("z"))
    dist = _float_val(n.GetChildMemberWithName("_pad"))
    return f"Plane n=({nx:.3g},{ny:.3g},{nz:.3g}) d={dist:.3g}"


# ============================================================================
# miki::core — ErrorCode
# ============================================================================

_ERROR_NAMES = {
    0x0000: "Ok",
    0x0001: "InvalidArgument",
    0x0002: "OutOfMemory",
    0x0003: "NotSupported",
    0x0004: "NotImplemented",
    0x0005: "InvalidState",
    0x0006: "IoError",
    0x0007: "Timeout",
    0x0008: "ResourceExhausted",
    0x1000: "PipelineCreationFailed",
    0x1001: "ShaderCompilationFailed",
    0x1002: "RenderPassInvalid",
    0x1003: "GraphCycleDetected",
    0x1004: "UnresolvedResource",
    0x2000: "MeshletOverflow",
    0x3000: "HlrBufferOverflow",
    0x4000: "RayMiss",
    0x5000: "ResourceNotFound",
    0x5001: "ResourceCorrupted",
    0x6000: "TaskCancelled",
    0x7000: "ImportFailed",
    0x7001: "TessellationFailed",
    0x7002: "ExportFailed",
    0xF000: "DeviceLost",
    0xF001: "DeviceNotReady",
    0xF002: "SwapchainOutOfDate",
    0xF003: "SurfaceLost",
}


def error_code_summary(valobj, internal_dict):
    v = _uint_val(valobj)
    name = _ERROR_NAMES.get(v, f"0x{v:04X}")
    return name


# ============================================================================
# miki::core — Result<T> (std::expected<T, ErrorCode>, libc++ layout)
# ============================================================================

def _find_member_dfs(valobj, member_name, max_depth=18):
    """Find first descendant SBValue with the given member name (libc++ internals)."""
    if max_depth <= 0:
        return lldb.SBValue()
    direct = valobj.GetChildMemberWithName(member_name)
    if direct.IsValid():
        return direct
    n = valobj.GetNumChildren()
    for i in range(n):
        ch = valobj.GetChildAtIndex(i)
        if not ch.IsValid():
            continue
        found = _find_member_dfs(ch, member_name, max_depth - 1)
        if found.IsValid():
            return found
    return lldb.SBValue()


def _libcxx_expected_val_unex(union_or_v):
    """Resolve __val_ / __unex_ from __union_.__v or raw __union_t."""
    if not union_or_v.IsValid():
        return lldb.SBValue(), lldb.SBValue()
    v = union_or_v.GetChildMemberWithName("__v")
    if v.IsValid():
        union_or_v = v
    val_ch = union_or_v.GetChildMemberWithName("__val_")
    unex_ch = union_or_v.GetChildMemberWithName("__unex_")
    return val_ch, unex_ch


def _libcxx_extract_expected(valobj):
    """Parse libc++ std::expected layout: __has_val_ + __union_[.__v].(__val_|__unex_).

    Returns (has_val: bool, val_ch, unex_ch) or None if layout is unrecognized.
    """
    raw = valobj.GetNonSyntheticValue()
    if not raw.IsValid():
        return None
    has_m = _find_member_dfs(raw, "__has_val_")
    if not has_m.IsValid():
        return None
    has_val = has_m.GetValueAsUnsigned(0) != 0
    union_m = _find_member_dfs(raw, "__union_")
    if not union_m.IsValid():
        return None
    val_ch, unex_ch = _libcxx_expected_val_unex(union_m)
    return (has_val, val_ch, unex_ch)


def _reparent_as_child(parent_valobj, child_sbval, new_name):
    """Expose a nested SBValue as a direct synthetic child with a readable name."""
    if not child_sbval.IsValid():
        return lldb.SBValue()
    addr = child_sbval.GetLoadAddress()
    if addr == lldb.LLDB_INVALID_ADDRESS:
        return child_sbval
    t = child_sbval.GetType()
    if not t.IsValid():
        return child_sbval
    rep = parent_valobj.CreateValueFromAddress(new_name, addr, t)
    return rep if rep.IsValid() else child_sbval


def _result_format_error(unex_ch):
    if not unex_ch.IsValid():
        return "?"
    s = unex_ch.GetSummary()
    if s:
        return s
    v = _uint_val(unex_ch)
    return _ERROR_NAMES.get(v, f"0x{v:04X}")


def _result_format_value(val_ch):
    if not val_ch.IsValid():
        return "?"
    tname = val_ch.GetType().GetName()
    if tname and ("__empty_byte" in tname or tname.endswith("void")):
        return "void"
    s = val_ch.GetSummary()
    if s:
        return s
    v = val_ch.GetValue()
    if v:
        return v
    return "?"


def result_summary(valobj, internal_dict):
    ext = _libcxx_extract_expected(valobj)
    if not ext:
        return "{Result ?}"
    has_val, val_ch, unex_ch = ext
    if has_val:
        return f"ok: {_result_format_value(val_ch)}"
    return f"err: {_result_format_error(unex_ch)}"


class ResultSyntheticProvider:
    """Flattens libc++ std::expected to has_value + value/error (no __repr_ / __union_ maze)."""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.has_val = False
        self.val_ch = lldb.SBValue()
        self.unex_ch = lldb.SBValue()

    def update(self):
        self.has_val = False
        self.val_ch = lldb.SBValue()
        self.unex_ch = lldb.SBValue()
        ext = _libcxx_extract_expected(self.valobj)
        if ext:
            self.has_val, self.val_ch, self.unex_ch = ext
        return True

    def num_children(self):
        # has_value + active payload only (value xor error)
        return 2

    def get_child_index(self, name):
        if name == "has_value":
            return 0
        if name == "value":
            return 1 if self.has_val else -1
        if name == "error":
            return 1 if not self.has_val else -1
        return -1

    def get_child_at_index(self, index):
        target = self.valobj.GetTarget()
        bool_t = target.FindFirstType("bool")
        if not bool_t.IsValid():
            bool_t = target.GetBasicType(lldb.eBasicTypeBool)

        if index == 0:
            data = lldb.SBData.CreateDataFromUInt32Array(
                lldb.eByteOrderLittle, 4, [1 if self.has_val else 0]
            )
            return self.valobj.CreateValueFromData("has_value", data, bool_t)

        if index != 1:
            return lldb.SBValue()

        if self.has_val and self.val_ch.IsValid():
            return _reparent_as_child(self.valobj, self.val_ch, "value")
        if not self.has_val and self.unex_ch.IsValid():
            return _reparent_as_child(self.valobj, self.unex_ch, "error")

        # inactive branch: placeholder
        data = lldb.SBData.CreateDataFromUInt32Array(lldb.eByteOrderLittle, 4, [0])
        return self.valobj.CreateValueFromData("(inactive)", data, bool_t)


# ============================================================================
# miki::rhi — Handle<Tag>
# Layout: [generation:16 | index:32 | type:16]
# ============================================================================

def handle_summary(valobj, internal_dict):
    raw = _uint_val(valobj.GetChildMemberWithName("_value"))
    if raw == 0:
        return "null"
    gen = (raw >> 48) & 0xFFFF
    idx = (raw >> 16) & 0xFFFFFFFF
    ty  = raw & 0xFFFF
    return f"Handle(gen={gen}, idx={idx}, ty={ty})"


# ============================================================================
# miki::rhi — BackendType
# ============================================================================

_BACKEND_NAMES = {0: "Vulkan", 1: "D3D12", 2: "OpenGL", 3: "WebGPU", 4: "Mock"}

# DeviceFeature enum values — must match DeviceFeature.h exactly.
# Order = declaration order (0-based), stopping before Count_.
_DEVICE_FEATURE_NAMES = [
    "Present",
    "DynamicRendering",
    "Synchronization2",
    "TimelineSemaphore",
    "MeshShader",
    "GeometryShader",
    "RayTracingPipeline",
    "RayQuery",
    "AccelerationStructure",
    "VariableRateShading",
    "DescriptorBuffer",
    "BufferDeviceAddress",
    "PushDescriptors",
    "DescriptorIndexing",
    "CooperativeMatrix",
    "Int64Atomics",
    "SubgroupOps",
    "TextureCompressionBC",
    "TextureCompressionASTC",
    "TextureCompressionETC2",
    "DebugMarkers",
    "PipelineStatistics",
    "CalibratedTimestamps",
]
_DEVICE_FEATURE_COUNT = len(_DEVICE_FEATURE_NAMES)  # == DeviceFeature::Count_


def _read_bitset_bits(valobj, num_bits):
    """Read set bit indices from a std::bitset by reading raw memory.

    Works with both libc++ and MSVC STL layouts by reading the underlying
    storage array directly from process memory.
    """
    bits_member = valobj.GetChildMemberWithName("bits_")
    if not bits_member.IsValid():
        bits_member = valobj  # caller may pass bits_ directly

    # libc++ stores in __first_ (array of unsigned long / uint64_t chunks)
    # MSVC stores in _Array (array of _Ty chunks)
    storage = bits_member.GetChildMemberWithName("__first_")
    if not storage.IsValid():
        storage = bits_member.GetChildMemberWithName("_Array")
    if not storage.IsValid():
        # Fallback: try to read raw bytes from the bitset object
        byte_size = bits_member.GetByteSize()
        if byte_size == 0:
            return []
        process = bits_member.GetProcess()
        addr = bits_member.GetLoadAddress()
        if addr == lldb.LLDB_INVALID_ADDRESS:
            return []
        err = lldb.SBError()
        data = process.ReadMemory(addr, byte_size, err)
        if err.Fail() or not data:
            return []
        result = []
        for bit_idx in range(min(num_bits, byte_size * 8)):
            byte_pos = bit_idx // 8
            bit_pos = bit_idx % 8
            if data[byte_pos] & (1 << bit_pos):
                result.append(bit_idx)
        return result

    # Read from the chunk array
    result = []
    num_chunks = storage.GetNumChildren()
    for chunk_idx in range(num_chunks):
        chunk = storage.GetChildAtIndex(chunk_idx)
        chunk_val = chunk.GetValueAsUnsigned(0)
        chunk_bits = chunk.GetByteSize() * 8
        for bit_pos in range(chunk_bits):
            global_bit = chunk_idx * chunk_bits + bit_pos
            if global_bit >= num_bits:
                return result
            if chunk_val & (1 << bit_pos):
                result.append(global_bit)
    return result


def _feature_bit_name(bit_idx):
    """Map bit index to DeviceFeature name."""
    if 0 <= bit_idx < len(_DEVICE_FEATURE_NAMES):
        return _DEVICE_FEATURE_NAMES[bit_idx]
    return f"Unknown({bit_idx})"


def backend_type_summary(valobj, internal_dict):
    v = _uint_val(valobj)
    return _BACKEND_NAMES.get(v, f"BackendType({v})")


# ============================================================================
# miki::rhi — DeviceFeature (enum)
# ============================================================================

def device_feature_summary(valobj, internal_dict):
    v = _uint_val(valobj)
    return _feature_bit_name(v)


# ============================================================================
# miki::rhi — DeviceFeatureSet (bitset wrapper)
# ============================================================================

def device_feature_set_summary(valobj, internal_dict):
    set_bits = _read_bitset_bits(valobj, _DEVICE_FEATURE_COUNT)
    if not set_bits:
        return "FeatureSet {empty}"
    names = [_feature_bit_name(b) for b in set_bits]
    return f"FeatureSet({len(names)}) {{{', '.join(names)}}}"


class DeviceFeatureSetSyntheticProvider:
    """Synthetic children provider: shows ALL feature bits with semantic names.

    Expanded view example:
      Present              = true
      DynamicRendering     = false
      Synchronization2     = false
      ...
    """

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.bit_values = []  # list of bool, length == _DEVICE_FEATURE_COUNT

    def update(self):
        # Synthetic provider's valobj has its children replaced, so
        # GetChildMemberWithName("bits_") would return our own synthetic
        # children instead of the real struct member. Read raw memory directly.
        self.bit_values = [False] * _DEVICE_FEATURE_COUNT
        raw = self.valobj.GetNonSyntheticValue()
        process = raw.GetProcess()
        addr = raw.GetLoadAddress()
        byte_size = raw.GetByteSize()
        if addr == lldb.LLDB_INVALID_ADDRESS or not process or byte_size == 0:
            return True
        err = lldb.SBError()
        data = process.ReadMemory(addr, byte_size, err)
        if err.Fail() or not data:
            return True
        for bit_idx in range(_DEVICE_FEATURE_COUNT):
            byte_pos = bit_idx // 8
            bit_pos = bit_idx % 8
            if byte_pos < len(data) and (data[byte_pos] & (1 << bit_pos)):
                self.bit_values[bit_idx] = True
        return True

    def num_children(self):
        return _DEVICE_FEATURE_COUNT

    def get_child_index(self, name):
        for i in range(_DEVICE_FEATURE_COUNT):
            if _DEVICE_FEATURE_NAMES[i] == name:
                return i
        return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= _DEVICE_FEATURE_COUNT:
            return None
        name = _DEVICE_FEATURE_NAMES[index]
        val = 1 if self.bit_values[index] else 0
        data = lldb.SBData.CreateDataFromUInt32Array(
            lldb.eByteOrderLittle, 1, [val]
        )
        return self.valobj.CreateValueFromData(
            name,
            data,
            self.valobj.GetTarget().FindFirstType("bool")
        )


# ============================================================================
# miki::render — CameraUBO
# ============================================================================

def camera_ubo_summary(valobj, internal_dict):
    cp = valobj.GetChildMemberWithName("cameraPos")
    x = _float_val(cp.GetChildMemberWithName("x"))
    y = _float_val(cp.GetChildMemberWithName("y"))
    z = _float_val(cp.GetChildMemberWithName("z"))
    near = _float_val(valobj.GetChildMemberWithName("nearPlane"))
    far  = _float_val(valobj.GetChildMemberWithName("farPlane"))
    return f"Camera pos=({x:.3g},{y:.3g},{z:.3g}) near={near:.3g} far={far:.3g}"


# ============================================================================
# miki::render — DirectionalLight
# ============================================================================

def directional_light_summary(valobj, internal_dict):
    dx = _float_val(valobj.GetChildMemberWithName("dirX"))
    dy = _float_val(valobj.GetChildMemberWithName("dirY"))
    dz = _float_val(valobj.GetChildMemberWithName("dirZ"))
    cr = _float_val(valobj.GetChildMemberWithName("colorR"))
    cg = _float_val(valobj.GetChildMemberWithName("colorG"))
    cb = _float_val(valobj.GetChildMemberWithName("colorB"))
    i  = _float_val(valobj.GetChildMemberWithName("intensity"))
    return f"Light dir=({dx:.3g},{dy:.3g},{dz:.3g}) col=({cr:.2g},{cg:.2g},{cb:.2g}) I={i:.3g}"


# ============================================================================
# miki::render — LightBuffer
# ============================================================================

def light_buffer_summary(valobj, internal_dict):
    count = _uint_val(valobj.GetChildMemberWithName("lightCount"))
    return f"LightBuffer count={count}"


# ============================================================================
# miki::render — StandardPBR
# ============================================================================

def standard_pbr_summary(valobj, internal_dict):
    albedo = valobj.GetChildMemberWithName("albedo")
    ar = _float_val(albedo.GetChildMemberWithName("x"))
    ag = _float_val(albedo.GetChildMemberWithName("y"))
    ab = _float_val(albedo.GetChildMemberWithName("z"))
    metal = _float_val(valobj.GetChildMemberWithName("metallic"))
    rough = _float_val(valobj.GetChildMemberWithName("roughness"))
    return f"PBR albedo=({ar:.2g},{ag:.2g},{ab:.2g}) metal={metal:.2g} rough={rough:.2g}"


# ============================================================================
# miki::render — DrawCall
# ============================================================================

def draw_call_summary(valobj, internal_dict):
    mat = _uint_val(valobj.GetChildMemberWithName("material"))
    mesh = valobj.GetChildMemberWithName("mesh")
    vc = _uint_val(mesh.GetChildMemberWithName("vertexCount"))
    ic = _uint_val(mesh.GetChildMemberWithName("indexCount"))
    mat_str = "INVALID" if mat == 0xFFFFFFFF else str(mat)
    return f"Draw mat={mat_str} verts={vc} idx={ic}"


# ============================================================================
# miki::gfx — MeshBuffers
# ============================================================================

def mesh_buffers_summary(valobj, internal_dict):
    vc = _uint_val(valobj.GetChildMemberWithName("vertexCount"))
    ic = _uint_val(valobj.GetChildMemberWithName("indexCount"))
    return f"Mesh verts={vc} idx={ic}"


# ============================================================================
# miki::rhi — DeviceConfig
# ============================================================================

def device_config_summary(valobj, internal_dict):
    backend = valobj.GetChildMemberWithName("preferredBackend")
    b = _uint_val(backend)
    bname = _BACKEND_NAMES.get(b, str(b))
    val = valobj.GetChildMemberWithName("enableValidation")
    validation = "on" if val.GetValueAsUnsigned(0) else "off"
    return f"DeviceConfig backend={bname} validation={validation}"


# ============================================================================
# miki::rhi — NativeImageHandle
# ============================================================================

def native_image_handle_summary(valobj, internal_dict):
    w = _uint_val(valobj.GetChildMemberWithName("width"))
    h = _uint_val(valobj.GetChildMemberWithName("height"))
    b = _uint_val(valobj.GetChildMemberWithName("type"))
    bname = _BACKEND_NAMES.get(b, str(b))
    return f"{w}x{h} backend={bname}"


# ============================================================================
# std::bitset<23> summary (must be defined before __lldb_init_module)
# ============================================================================

def _bitset23_as_features_summary(valobj, internal_dict):
    """Summary for std::bitset<23> when used as DeviceFeatureSet::bits_."""
    storage = valobj.GetChildMemberWithName("__first_")
    if not storage.IsValid():
        storage = valobj.GetChildMemberWithName("_Array")

    set_names = []
    if storage.IsValid():
        num_chunks = storage.GetNumChildren()
        for chunk_idx in range(num_chunks):
            chunk = storage.GetChildAtIndex(chunk_idx)
            chunk_val = chunk.GetValueAsUnsigned(0)
            chunk_bits = chunk.GetByteSize() * 8
            for bit_pos in range(chunk_bits):
                global_bit = chunk_idx * chunk_bits + bit_pos
                if global_bit >= _DEVICE_FEATURE_COUNT:
                    break
                if chunk_val & (1 << bit_pos):
                    set_names.append(_feature_bit_name(global_bit))
    else:
        byte_size = valobj.GetByteSize()
        if byte_size > 0:
            process = valobj.GetProcess()
            addr = valobj.GetLoadAddress()
            if addr != lldb.LLDB_INVALID_ADDRESS:
                err = lldb.SBError()
                data = process.ReadMemory(addr, byte_size, err)
                if not err.Fail() and data:
                    for bit_idx in range(min(_DEVICE_FEATURE_COUNT, byte_size * 8)):
                        if data[bit_idx // 8] & (1 << (bit_idx % 8)):
                            set_names.append(_feature_bit_name(bit_idx))

    if not set_names:
        return "{empty}"
    return f"{{{', '.join(set_names)}}}"


# ============================================================================
# Registration
# ============================================================================

_reg_results = []  # collected during init for final report


def _safe_add(cat, type_name, fn, **kwargs):
    """Register summary with error reporting."""
    try:
        _add(cat, type_name, fn, **kwargs)
        _reg_results.append(("summary", type_name, fn.__name__, True, None))
    except Exception as e:
        _reg_results.append(("summary", type_name, fn.__name__, False, str(e)))


def _safe_add_regex(cat, regex, fn):
    """Register regex summary with error reporting."""
    try:
        _add_regex(cat, regex, fn)
        _reg_results.append(("summary(regex)", regex, fn.__name__, True, None))
    except Exception as e:
        _reg_results.append(("summary(regex)", regex, fn.__name__, False, str(e)))


def _safe_add_synthetic(cat, type_name, class_name, **kwargs):
    """Register synthetic with error reporting."""
    try:
        _add_synthetic(cat, type_name, class_name, **kwargs)
        _reg_results.append(("synthetic", type_name, class_name.split('.')[-1], True, None))
    except Exception as e:
        _reg_results.append(("synthetic", type_name, class_name.split('.')[-1], False, str(e)))


def _safe_add_synthetic_regex(cat, regex, class_name, options=lldb.eTypeOptionNone):
    """Register synthetic with regex type name."""
    try:
        synth = lldb.SBTypeSynthetic.CreateWithClassName(
            class_name,
            options,
        )
        cat.AddTypeSynthetic(
            lldb.SBTypeNameSpecifier(regex, True),
            synth,
        )
        _reg_results.append(("synthetic(regex)", regex, class_name.split(".")[-1], True, None))
    except Exception as e:
        _reg_results.append(("synthetic(regex)", regex, class_name.split(".")[-1], False, str(e)))


def __lldb_init_module(debugger, internal_dict):
    # Create category and ensure it has higher priority than built-in 'cplusplus'
    cat = debugger.CreateCategory("miki")
    cat.SetEnabled(True)
    cat.AddLanguage(lldb.eLanguageTypeC_plus_plus)

    # Disable built-in std::bitset synthetic for our specific bitset size
    # so DeviceFeatureSet's own synthetic takes effect.
    debugger.HandleCommand(
        'type synthetic delete -w cplusplus "std::bitset<23>"'
    )
    debugger.HandleCommand(
        'type synthetic delete -w cplusplus "std::__1::bitset<23>"'
    )

    # --- Core math ---
    _safe_add(cat, "miki::core::float2", float2_summary)
    _safe_add(cat, "miki::core::float3", float3_summary)
    _safe_add(cat, "miki::core::float4", float4_summary)
    _safe_add(cat, "miki::core::float4x4", float4x4_summary)
    _safe_add(cat, "miki::core::int2", int2_summary)
    _safe_add(cat, "miki::core::uint2", uint2_summary)
    _safe_add(cat, "miki::core::uint3", uint3_summary)
    _safe_add(cat, "miki::core::uint4", uint4_summary)

    # --- Core geometry ---
    _safe_add(cat, "miki::core::AABB", aabb_summary)
    _safe_add(cat, "miki::core::BoundingSphere", bounding_sphere_summary)
    _safe_add(cat, "miki::core::Ray", ray_summary)
    _safe_add(cat, "miki::core::Plane", plane_summary)

    # --- Core error ---
    _safe_add(cat, "miki::core::ErrorCode", error_code_summary)

    # --- Core Result<T> / VoidResult = std::expected<..., ErrorCode> (libc++) ---
    _safe_add_regex(cat, r"^miki::core::Result<.*>$", result_summary)
    _safe_add(cat, "miki::core::VoidResult", result_summary)
    _safe_add_regex(cat, r"^std::expected<void, miki::core::ErrorCode>$", result_summary)
    _safe_add_regex(
        cat, r"^std::__[0-9]+::expected<void, miki::core::ErrorCode>$", result_summary
    )
    _safe_add_regex(
        cat, r"^std::__[0-9]+::expected<.+,\s*miki::core::ErrorCode>$", result_summary
    )
    synth_opts = lldb.eTypeOptionCascade
    _safe_add_synthetic_regex(
        cat,
        r"^miki::core::Result<.*>$",
        "miki_lldb_formatters.ResultSyntheticProvider",
        synth_opts,
    )
    _safe_add_synthetic(
        cat,
        "miki::core::VoidResult",
        "miki_lldb_formatters.ResultSyntheticProvider",
        options=synth_opts,
    )
    _safe_add_synthetic_regex(
        cat,
        r"^std::expected<void, miki::core::ErrorCode>$",
        "miki_lldb_formatters.ResultSyntheticProvider",
        synth_opts,
    )
    _safe_add_synthetic_regex(
        cat,
        r"^std::__[0-9]+::expected<void, miki::core::ErrorCode>$",
        "miki_lldb_formatters.ResultSyntheticProvider",
        synth_opts,
    )
    _safe_add_synthetic_regex(
        cat,
        r"^std::__[0-9]+::expected<.+,\s*miki::core::ErrorCode>$",
        "miki_lldb_formatters.ResultSyntheticProvider",
        synth_opts,
    )

    # --- RHI handles (regex for all Handle<Tag> specializations) ---
    _safe_add_regex(cat, r"^miki::rhi::Handle<.*>$", handle_summary)

    # --- RHI enums/structs ---
    _safe_add(cat, "miki::rhi::BackendType", backend_type_summary)
    _safe_add(cat, "miki::rhi::DeviceFeature", device_feature_summary)
    _safe_add(cat, "miki::rhi::DeviceConfig", device_config_summary)
    _safe_add(cat, "miki::rhi::NativeImageHandle", native_image_handle_summary)

    # --- DeviceFeatureSet: summary + synthetic children ---
    _safe_add(cat, "miki::rhi::DeviceFeatureSet", device_feature_set_summary,
              options=lldb.eTypeOptionCascade)
    _safe_add_synthetic(cat, "miki::rhi::DeviceFeatureSet",
                        "miki_lldb_formatters.DeviceFeatureSetSyntheticProvider",
                        options=lldb.eTypeOptionCascade)

    # Override std::bitset<23> summary for feature-name display
    _safe_add(cat, "std::bitset<23>", _bitset23_as_features_summary)
    _safe_add_regex(cat, r"^std::__[0-9]+::bitset<23>$", _bitset23_as_features_summary)

    # --- Render ---
    _safe_add(cat, "miki::render::CameraUBO", camera_ubo_summary)
    _safe_add(cat, "miki::render::DirectionalLight", directional_light_summary)
    _safe_add(cat, "miki::render::LightBuffer", light_buffer_summary)
    _safe_add(cat, "miki::render::StandardPBR", standard_pbr_summary)
    _safe_add(cat, "miki::render::DrawCall", draw_call_summary)

    # --- Gfx ---
    _safe_add(cat, "miki::gfx::MeshBuffers", mesh_buffers_summary)

    # --- Diagnostic report ---
    ok_count = sum(1 for r in _reg_results if r[3])
    fail_count = sum(1 for r in _reg_results if not r[3])
    print(f"[miki] LLDB formatters: {ok_count} registered, {fail_count} failed.")
    print(f"[miki] {'Kind':<18} {'Type':<45} {'Handler':<40} Status")
    print(f"[miki] {'-'*18} {'-'*45} {'-'*40} ------")
    for kind, tname, handler, ok, err in _reg_results:
        status = "OK" if ok else f"FAIL: {err}"
        print(f"[miki] {kind:<18} {tname:<45} {handler:<40} {status}")
    print(f"[miki] -------")
    _reg_results.clear()


def _add(category, type_name, fn, options=lldb.eTypeOptionNone):
    summary = lldb.SBTypeSummary.CreateWithFunctionName(
        f"miki_lldb_formatters.{fn.__name__}",
        options
    )
    category.AddTypeSummary(
        lldb.SBTypeNameSpecifier(type_name, False),
        summary
    )


def _add_regex(category, regex, fn):
    summary = lldb.SBTypeSummary.CreateWithFunctionName(
        f"miki_lldb_formatters.{fn.__name__}",
        lldb.eTypeOptionNone
    )
    category.AddTypeSummary(
        lldb.SBTypeNameSpecifier(regex, True),  # True = regex
        summary
    )


def _add_synthetic(category, type_name, class_name, options=lldb.eTypeOptionNone):
    synth = lldb.SBTypeSynthetic.CreateWithClassName(
        class_name,
        options
    )
    category.AddTypeSynthetic(
        lldb.SBTypeNameSpecifier(type_name, False),
        synth
    )

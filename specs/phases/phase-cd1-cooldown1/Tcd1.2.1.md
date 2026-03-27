# Tcd1.2.1 — RHI Test Gap-Fill

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Test Gap
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Scope

- **Problem**: Missing edge-case tests in RHI backends: error paths (invalid handles, double-destroy, OOM simulation), resource leak detection, format coverage gaps, backend-specific quirks.
- **Affected files**: `tests/unit/test_vulkan_device.cpp`, `test_vulkan_cmdbuf.cpp`, `test_opengl_device.cpp`, `test_webgpu_device.cpp`, `test_mock_device.cpp`
- **Affected phases**: 1a, 1b, 6a (mesh shader pipeline)

### Acceptance Criteria

- [ ] ≥80% line coverage on `src/miki/rhi/vulkan/VulkanDevice.cpp`
- [ ] ≥80% line coverage on `src/miki/rhi/vulkan/VulkanCommandBuffer.cpp`
- [ ] Error path tests: invalid handle destroy, double-destroy, buffer overrun
- [ ] Mesh shader pipeline creation tests cover all validation rules
- [ ] All new tests pass on debug-vulkan build

## Steps

- [ ] **Step 1**: Coverage audit — identify untested code paths via gcov/llvm-cov
      `[verify: compile]`
- [ ] **Step 2**: Write missing RHI tests (target +15-20 new tests)
      `[verify: test]`

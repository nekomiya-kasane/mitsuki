// postfx/dof.slang — Depth of Field (implementing miki_postfx).
//
// Circular bokeh DoF using gather-based approach.
// CoC (Circle of Confusion) computed from thin-lens model.

implementing miki_postfx;

import miki_core;

/// Compute Circle of Confusion radius from thin-lens model.
/// focalLength in mm, aperture = f-number, focusDistance in world units.
[ForceInline]
public float computeCoC(float depth, float focusDistance, float focalLength, float aperture, float sensorHeight) {
    if (depth <= 0.0f) return 0.0f;
    float fLenMeters = focalLength * 0.001f;
    float coc = abs(fLenMeters * (depth - focusDistance)) / (depth * (focusDistance - fLenMeters) * aperture);
    return coc * sensorHeight;  // Convert to screen-space pixels
}

/// Near/far field classification from signed CoC.
/// Positive CoC = far field (behind focus), negative = near field (in front).
[ForceInline]
public float computeSignedCoC(float depth, float focusDistance, float focalLength, float aperture, float sensorHeight) {
    float fLenMeters = focalLength * 0.001f;
    float sign = (depth > focusDistance) ? 1.0f : -1.0f;
    return sign * computeCoC(depth, focusDistance, focalLength, aperture, sensorHeight);
}

/// Clamp CoC to max bokeh radius (in pixels) to bound scatter cost.
[ForceInline]
public float clampCoC(float coc, float maxRadius) {
    return clamp(coc, -maxRadius, maxRadius);
}

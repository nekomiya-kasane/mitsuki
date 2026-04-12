// cad/draft_angle.slang — Draft angle analysis for injection molding / casting.
//
// Computes the angle between each face normal and a pull direction (mold open direction).
// Faces are colored by draft angle: sufficient draft (green), insufficient (yellow),
// undercut (red), and zero draft (blue).
//
// This is a core feature in CAD/CAM for manufacturability analysis (DFM).
//
// Reference: CATIA Draft Analysis, Siemens NX Draft Body, HOOPS draft angle.

implementing miki_cad;

import miki_core;

/// Draft angle analysis parameters.
public struct DraftAnalysisParams {
    public float3 pullDirection;  // Mold pull direction (normalized)
    public float  minDraftAngle;  // Minimum required draft angle in degrees (e.g. 1.0)
    public float  maxDraftAngle;  // Maximum visualization range in degrees (e.g. 10.0)
};

/// Compute the draft angle between a surface normal and the pull direction.
/// Returns angle in degrees [0, 90].
public float computeDraftAngle(float3 faceNormal, float3 pullDirection) {
    float cosAngle = abs(dot(normalize(faceNormal), normalize(pullDirection)));
    float angleRad = acos(clamp(cosAngle, 0.0, 1.0));
    // Draft angle is the complement: 90 - angle between normal and pull direction
    return 90.0 - degrees(angleRad);
}

/// Classify draft angle into color zones.
public float3 draftAngleColor(float draftAngleDeg, DraftAnalysisParams params) {
    if (draftAngleDeg < 0.0) {
        // Undercut: face is angled away from pull direction
        return float3(1.0, 0.0, 0.0); // Red
    }
    if (draftAngleDeg < 0.1) {
        // Zero draft: face is perpendicular to pull direction
        return float3(0.0, 0.0, 1.0); // Blue
    }
    if (draftAngleDeg < params.minDraftAngle) {
        // Insufficient draft
        return float3(1.0, 1.0, 0.0); // Yellow
    }
    // Sufficient draft: interpolate green intensity
    float t = saturate((draftAngleDeg - params.minDraftAngle)
                       / max(params.maxDraftAngle - params.minDraftAngle, 0.01));
    return float3(0.0, 0.5 + 0.5 * t, 0.0); // Green (darker for minimal, brighter for generous)
}

/// Check if a face is an undercut (cannot be extracted in pull direction).
/// An undercut occurs when the face normal opposes the pull direction AND
/// the draft angle is negative.
public bool isUndercut(float3 faceNormal, float3 pullDirection) {
    float cosAngle = dot(normalize(faceNormal), normalize(pullDirection));
    return cosAngle > 0.0; // Normal pointing in same direction as pull = undercut
}

/// Degrees to radians.
float degrees(float radians_) {
    return radians_ * (180.0 / 3.14159265358979);
}

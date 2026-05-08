#version 460
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform CameraInfo { mat4 viewProj; } pc;

layout(location = 0) out vec3 vColor;

void main() {
    vec4 clipPos = pc.viewProj * vec4(inPosition.xyz, 1.0);
    gl_Position = clipPos;

    // VIBEMATH: Volumetric Spatial Color Hashing
    // The color is derived from the particle's physical location in the universe
    vec3 pos = inPosition.xyz * 0.0001; // Scale down so the gradients are wide and smooth
    float r = sin(pos.x + pos.y) * 0.5 + 0.5;
    float g = cos(pos.y + pos.z) * 0.5 + 0.5;
    float b = sin(pos.z - pos.x) * 0.5 + 0.5;

    // Boost the vibrance for that demoscene look
    vColor = vec3(r, g, b) * 1.5;

    // Perspective point sizing (Bigger up close, tiny far away)
    float pointSize = 6000.0 / clipPos.w;
    // Allow slightly bigger points (up to 8.0) so the alpha blending overlaps beautifully
    gl_PointSize = clamp(pointSize, 1.0, 8.0); 
}

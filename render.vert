// render.vert
#version 460

layout(location = 0) in vec4 inPosition;

layout(push_constant) uniform CameraInfo {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vEnergy;

void main() {
    vec3 pos = inPosition.xyz;

    vec4 clipPos = pc.viewProj * vec4(pos, 1.0);

    gl_Position = clipPos;

    float dist = length(pos);

    // energy from compute shader pad channel
    vEnergy = inPosition.w;

    // ========================================================
    // COSMIC VOLUME PALETTE
    // ========================================================

    float band0 =
        sin(pos.x * 0.00012 + vEnergy * 8.0);

    float band1 =
        sin(pos.y * 0.00008 + pos.z * 0.00015);

    float band2 =
        cos(dist * 0.00005 - vEnergy * 5.0);

    vec3 deepSpace = vec3(
        0.15 + band0 * 0.8,
        0.20 + band1 * 0.7,
        0.35 + band2 * 1.2
    );

    vec3 plasma = vec3(
        sin(vEnergy * 10.0),
        sin(vEnergy * 7.0 + 2.0),
        sin(vEnergy * 13.0 + 4.0)
    ) * 0.5 + 0.5;

    vColor = mix(deepSpace, plasma * 2.0, vEnergy);

    // ========================================================
    // VOLUMETRIC STAR BLOOM
    // ========================================================

    float size =
        14000.0 / max(clipPos.w, 1.0);

    size *= (0.6 + vEnergy * 1.8);

    gl_PointSize = clamp(size, 1.0, 14.0);
}

#version 460
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform CameraInfo { mat4 viewProj; } pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vGlow;

void main() {
    vec4 clipPos = pc.viewProj * vec4(inPosition.xyz, 1.0);
    gl_Position = clipPos;

    // Decode Material ID and Energy
    int sliceID = int(inPosition.w);
    float energy = fract(inPosition.w);

    // Default values
    vColor = vec3(1.0);
    float size = 1.0;
    vGlow = 0.5;

    // --- PALETTE DEFINITIONS ---
    // Slice 0: The Core (Deep Bio-Luminescent Purple)
    if (sliceID == 0) {
        vColor = mix(vec3(0.2, 0.0, 0.5), vec3(0.0, 1.0, 1.0), energy);
        size = 2.0;
        vGlow = 0.3;
    }
    // Slice 1: Hopf Hunters (Solar Flare Orange)
    else if (sliceID == 1) {
        vColor = vec3(1.0, 0.4, 0.1);
        size = 3.5;
        vGlow = 0.8;
    }
    // Slice 2: Boids (Nebula Violet)
    else if (sliceID == 2) {
        vColor = vec3(0.6, 0.2, 1.0);
        size = 2.0;
    }
    // Slice 4: Fusion/Impact (Blinding White-Blue)
    else if (sliceID == 4) {
        vColor = vec3(0.8, 0.9, 1.0);
        size = 5.0;
        vGlow = 1.0;
    }

    // Pseudo-volumetric sizing: things further away get smaller/dimmer
    // 4000 is an arbitrary factor based on your world scale
    gl_PointSize = clamp(size * (4000.0 / clipPos.w), 1.0, 8.0);
}

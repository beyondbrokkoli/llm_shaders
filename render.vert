#version 460
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform CameraInfo { mat4 viewProj; } pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vAlphaBase;

void main() {
    vec4 clipPos = pc.viewProj * vec4(inPosition.xyz, 1.0);
    gl_Position = clipPos;

    // Decode the Material ID and Energy from the pad channel
    int sliceID = int(inPosition.w);
    float energy = fract(inPosition.w);
    
    float pointSize = 1.0;

    // SLICE 0: CPU CORE (Deep Plasma + Cyan Highlights)
    if (sliceID == 0) {
        vec3 deep = vec3(0.1, 0.0, 0.3);
        vec3 hot = vec3(0.0, 0.8, 1.0);
        vColor = mix(deep, hot, energy);
        vAlphaBase = 0.05;
        pointSize = 4000.0 / clipPos.w; // Volumetric bloom
    }
    // SLICE 1: CONTAINMENT CAGE (Ghostly Orange Wireframe)
    else if (sliceID == 1) {
        vColor = vec3(1.0, 0.4, 0.0);
        vAlphaBase = 0.03; // Very faint
        pointSize = 1500.0 / clipPos.w; // Sharp, thin lines
    }
    // SLICE 2: ACCELERATOR BOIDS (Searing Pink Sparks)
    else if (sliceID == 2) {
        vColor = vec3(1.0, 0.0, 0.6);
        vAlphaBase = 0.15; // Brighter
        pointSize = 6000.0 / clipPos.w; // Large glowing orbs
    }
    // SLICE 3: METEORS (Blinding White/Blue)
    else if (sliceID == 3) {
        vColor = vec3(0.8, 0.9, 1.0);
        vAlphaBase = 0.2; // Brightest
        pointSize = 8000.0 / clipPos.w; // Massive streaks
    }

    gl_PointSize = clamp(pointSize, 1.0, 1.0); 
}

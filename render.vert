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
        pointSize = 1.0; // Keep the dense core at 1px to save fill-rate
    }
    // SLICE 1: CONTAINMENT CAGE (Ghostly Orange Wireframe)
    else if (sliceID == 1) {
        vColor = vec3(1.0, 0.4, 0.0);
        vAlphaBase = 0.03; // Very faint
        pointSize = 1.0;
    }
    // SLICE 2: ACCELERATOR BOIDS (Standard Traffic)
    else if (sliceID == 2) {
        vColor = vec3(0.5, 0.0, 1.0); // Calmer purple
        vAlphaBase = 0.1;
        pointSize = 1.0;
    }
    // SLICE 3: FUSION IMPACTS / METEORS / SPARKS (Violent Red/White)
    else if (sliceID == 3) {
        vColor = vec3(1.0, 0.2, 0.1); // Searing hot core-breach red
        vAlphaBase = 0.9; // Opaque and bright!
        
        // This is a collision! Make the point physically larger on screen
        pointSize = 2.5; 
    }
    // SLICE 4: PLASMA WEB (The Cage Snapping to Intruders)
    else if (sliceID == 4) {
        vColor = vec3(0.0, 1.0, 0.8); // Intense Cyan flash
        vAlphaBase = 0.8; 
        
        // Make the snapping web slightly thicker
        pointSize = 2.0; 
    }

    // THE IGPU SAVER: Allow sizes up to 2.5 pixels for impacts, 
    // but keep standard particles clamped to 1.0.
    gl_PointSize = clamp(pointSize, 1.0, 2.5);
}

#version 460
layout(location = 0) in vec3 vColor;
layout(location = 1) in float vAlphaBase;

layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = gl_PointCoord - vec2(0.5);
    float dist = length(uv);

    if (dist > 0.5) discard;

    // Soft Gaussian-style fade from center to edge
    float glow = smoothstep(0.5, 0.0, dist);
    
    // Make the exact center pixel burning hot white
    float core = smoothstep(0.1, 0.0, dist);

    vec3 finalColor = (vColor * glow) + (vec3(1.0) * core * 0.5);

    // Apply the specific alpha base we set in the vertex shader
    fragColor = vec4(finalColor * 2.0, glow * vAlphaBase);
}

#version 460
layout(location = 0) in vec3 vColor;
layout(location = 1) in float vAlphaBase;

layout(location = 0) out vec4 fragColor;

// render.frag
void main() {
    vec2 uv = gl_PointCoord - vec2(0.5);
    float dist = length(uv);

    if (dist > 0.5) discard;

    // Cheaper, linear glow instead of smoothstep
    float glow = max(0.0, 1.0 - (dist * 2.0));
    float core = max(0.0, 1.0 - (dist * 10.0));

    vec3 finalColor = (vColor * glow) + (vec3(1.0) * core * 0.5);
    fragColor = vec4(finalColor * 2.0, glow * vAlphaBase);
}

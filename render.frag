// render.frag
#version 460

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vEnergy;

layout(location = 0) out vec4 fragColor;

void main() {

    vec2 uv = gl_PointCoord - vec2(0.5);

    float dist = length(uv);

    if (dist > 0.5) {
        discard;
    }

    // ========================================================
    // MULTI-LAYER STAR CORE
    // ========================================================

    float core =
        smoothstep(0.22, 0.0, dist);

    float glow =
        smoothstep(0.5, 0.0, dist);

    float ring =
        smoothstep(0.38, 0.34, abs(dist - 0.22));

    vec3 color =
        vColor * glow +
        vec3(1.0, 0.9, 0.7) * core * (0.3 + vEnergy);

    color += ring * vColor * 0.8;

    // IMPORTANT:
    // millions of additive particles
    float alpha =
        glow * (0.012 + vEnergy * 0.05);

    fragColor = vec4(color * alpha * 8.0, alpha);
}

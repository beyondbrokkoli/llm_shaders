#version 460
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    // 1. Find where we are inside the single point (0.0 to 1.0)
    vec2 uv = gl_PointCoord - vec2(0.5);
    float dist = length(uv);

    // 2. Round the square into a perfect circle
    if (dist > 0.5) discard;

    // 3. Create a soft, fuzzy Gaussian glow (1.0 at center, 0.0 at edges)
    float edgeFade = smoothstep(0.5, 0.0, dist);

    // 4. THE 7-MILLION TRICK: Extremely low base alpha!
    // Because we have so many particles, if alpha is 1.0 it just turns pure white.
    // At 0.08, individual particles are ghosts, but swarms burn like stars.
    float baseAlpha = 0.08; 

    fragColor = vec4(vColor * edgeFade, edgeFade * baseAlpha);
}

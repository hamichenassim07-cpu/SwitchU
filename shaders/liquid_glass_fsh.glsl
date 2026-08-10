// Switch U HOME V10.4 - Liquid Glass Engine C2
// deko3d / uam fragment shader
//
// Goals of this pass:
// - unmistakable edge refraction / lensing
// - soft + heavy frost sampling from the captured HOME backdrop
// - subtle RGB dispersion only around the bevel
// - Fresnel rim, directional specular and inner shadow
// - very slow liquid micro-warp
// - deliberate diagnostic mode when refractionIntensity >= 0.95
#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;

layout (binding = 0) uniform sampler2D tex;

// FsUniforms layout (matches nxui::FsUniforms):
//   int useTexture; float param1..param3; float extra[48]
layout (std140, binding = 1) uniform FsUniforms {
    int   useTexture;
    float lg_refractionIntensity;
    float lg_blurIntensity;
    float lg_noiseIntensity;

    vec4  lg_pack0;      // x=glow, y=saturation, z=body/reflect strength, w=roughness
    vec4  lg_pack1;      // x=animSpeed, y=time, z=powerFactor, w=fPower
    vec4  lg_pack2;      // x=refA, y=refB, z=refC, w=refD
    vec4  lg_pack3;      // x=glowWeight, y=glowBias, z=glowEdge0, w=glowEdge1
    vec4  lg_tintColor;
    vec4  lg_panelRect;  // x,y,w,h in 1280x720 screen pixels
    vec4  lg_screenSize; // x=screenW, y=screenH, z=shade, w=reserved
};

layout (location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

float saturate1(float v) { return clamp(v, 0.0, 1.0); }

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float sdRoundedRectPx(vec2 p, vec2 halfSize, float radius) {
    radius = clamp(radius, 1.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

vec3 applySaturation(vec3 c, float amount) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(l), c, amount);
}

vec3 screenBlend(vec3 base, vec3 blend) {
    return 1.0 - (1.0 - base) * (1.0 - blend);
}

vec4 sampleSoft(vec2 uv, vec2 texel, float radiusPx) {
    vec2 o = texel * max(radiusPx, 0.01);
    vec4 c = texture(tex, uv) * 0.40;
    c += texture(tex, uv + vec2( o.x, 0.0)) * 0.15;
    c += texture(tex, uv + vec2(-o.x, 0.0)) * 0.15;
    c += texture(tex, uv + vec2(0.0,  o.y)) * 0.15;
    c += texture(tex, uv + vec2(0.0, -o.y)) * 0.15;
    return c;
}

vec4 sampleHeavy(vec2 uv, vec2 texel, float radiusPx) {
    vec2 o = texel * max(radiusPx, 0.01);
    vec2 d = o * 0.72;
    vec4 c = texture(tex, uv) * 0.22;
    c += texture(tex, uv + vec2( o.x, 0.0)) * 0.10;
    c += texture(tex, uv + vec2(-o.x, 0.0)) * 0.10;
    c += texture(tex, uv + vec2(0.0,  o.y)) * 0.10;
    c += texture(tex, uv + vec2(0.0, -o.y)) * 0.10;
    c += texture(tex, uv + vec2( d.x,  d.y)) * 0.095;
    c += texture(tex, uv + vec2(-d.x,  d.y)) * 0.095;
    c += texture(tex, uv + vec2( d.x, -d.y)) * 0.095;
    c += texture(tex, uv + vec2(-d.x, -d.y)) * 0.095;
    return c;
}

void main() {
    float refrIntensity = clamp(lg_refractionIntensity, 0.0, 1.25);
    float blurControl   = clamp(lg_blurIntensity / 8.0, 0.0, 1.0);
    float noiseAmount   = clamp(lg_noiseIntensity, 0.0, 0.10);
    float glowAmount    = max(lg_pack0.x, 0.0);
    float saturation    = clamp(lg_pack0.y, 0.0, 1.6);
    float bodyStrength  = clamp(lg_pack0.z, 0.0, 1.0);
    float roughness     = clamp(lg_pack0.w, 0.0, 0.08);
    float animSpeed     = max(lg_pack1.x, 0.0);
    float time          = lg_pack1.y;
    float powerFactor   = clamp(lg_pack1.z, 2.0, 12.0);
    float fPower        = clamp(lg_pack1.w, 0.5, 2.5);
    float shadeAmount   = clamp(lg_screenSize.z, 0.0, 1.0);

    // The debug flag is intentionally encoded by HomeLiquidGlassStyle using
    // refraction >= .95, so no renderer ABI change is needed.
    float debugMode = step(0.95, refrIntensity);

    vec2 panelSize = max(lg_panelRect.zw, vec2(2.0));
    vec2 halfSize  = panelSize * 0.5;
    vec2 localPx   = (fragUV - 0.5) * panelSize;

    // Rounded-rect SDF in PIXELS. The powerFactor influences the apparent
    // corner radius a little so existing tuning still has a visible role.
    float cornerFactor = clamp(0.54 + powerFactor * 0.040, 0.62, 0.93);
    float cornerRadius = min(halfSize.x, halfSize.y) * cornerFactor;
    float d = sdRoundedRectPx(localPx, halfSize, cornerRadius);
    float aa = max(fwidth(d), 0.75);
    float shape = 1.0 - smoothstep(-aa, aa, d);
    if (shape <= 0.001)
        discard;

    float insidePx = max(-d, 0.0);
    float bevelPx = clamp(min(halfSize.x, halfSize.y) * (0.34 + 0.04 * fPower), 7.0, 20.0);
    float edge = 1.0 - smoothstep(0.0, bevelPx, insidePx);
    float edge2 = edge * edge;

    // True SDF normal around the panel contour.
    const float gradStep = 1.35;
    float dx = sdRoundedRectPx(localPx + vec2(gradStep, 0.0), halfSize, cornerRadius)
             - sdRoundedRectPx(localPx - vec2(gradStep, 0.0), halfSize, cornerRadius);
    float dy = sdRoundedRectPx(localPx + vec2(0.0, gradStep), halfSize, cornerRadius)
             - sdRoundedRectPx(localPx - vec2(0.0, gradStep), halfSize, cornerRadius);
    vec2 normal2 = normalize(vec2(dx, dy) + vec2(1e-5));

    // Slow microscopic liquid motion. It moves only the sampled backdrop, not
    // the widget geometry, so the HUD remains perfectly stable.
    float phase = time * animSpeed;
    vec2 wave = vec2(
        sin(localPx.y * 0.045 + phase * 0.73) + 0.46 * sin(localPx.y * 0.091 - phase * 0.41),
        cos(localPx.x * 0.041 - phase * 0.67) + 0.42 * cos(localPx.x * 0.083 + phase * 0.38)
    );
    wave *= (roughness * 24.0 + debugMode * 0.85);

    // Edge refraction is deliberately much stronger than centre distortion.
    // This produces the visible "bending line" test expected from real glass.
    float edgeRefPx = (2.0 + 13.5 * refrIntensity) * pow(edge2, 0.82);
    edgeRefPx *= mix(1.0, 1.70, debugMode);
    vec2 edgeRefraction = normal2 * edgeRefPx;

    // Convex lens: gently pull inner pixels toward the panel centre.
    vec2 centreVector = -localPx / max(min(halfSize.x, halfSize.y), 1.0);
    float centreLens = (0.55 + 2.35 * refrIntensity) * (1.0 - edge) * (1.0 - edge);
    vec2 lensOffset = centreVector * centreLens;

    vec2 screenPx = lg_panelRect.xy + fragUV * panelSize;
    vec2 sampledPx = screenPx + edgeRefraction + lensOffset + wave;
    vec2 screenUV = sampledPx / max(lg_screenSize.xy, vec2(1.0));
    screenUV = clamp(screenUV, vec2(0.001), vec2(0.999));

    // Captured backdrop is half-resolution (640x360), so one source texel
    // corresponds to two full-screen pixels.
    vec2 texel = 2.0 / max(lg_screenSize.xy, vec2(1.0));

    // Two frost bands from the same captured backdrop. Heavy frost dominates
    // the bevel, while the centre remains clearer and more lens-like.
    float softRadius  = mix(0.75, 2.65, blurControl);
    float heavyRadius = mix(2.8, 7.8, blurControl);
    vec4 soft  = sampleSoft(screenUV, texel, softRadius);
    vec4 heavy = sampleHeavy(screenUV, texel, heavyRadius);
    float frostMix = clamp(edge * (0.58 + 0.34 * blurControl) + blurControl * 0.16, 0.0, 0.92);
    vec3 glass = mix(soft.rgb, heavy.rgb, frostMix);

    // Chromatic dispersion only in the refractive rim. In diagnostic mode it
    // becomes intentionally exaggerated, making shader activation obvious.
    float chromaPx = (0.35 + 1.85 * refrIntensity) * pow(edge, 1.45);
    chromaPx *= mix(1.0, 3.6, debugMode);
    vec2 chromaUV = normal2 * chromaPx / max(lg_screenSize.xy, vec2(1.0));
    vec3 split;
    split.r = texture(tex, clamp(screenUV + chromaUV, vec2(0.001), vec2(0.999))).r;
    split.g = texture(tex, screenUV).g;
    split.b = texture(tex, clamp(screenUV - chromaUV, vec2(0.001), vec2(0.999))).b;
    glass = mix(glass, split, edge * mix(0.20, 0.78, debugMode));

    glass = applySaturation(glass, saturation);

    // Very light cool body tint. The backdrop must remain recognisable through
    // the panel; this is glass, not an opaque blue rectangle.
    vec3 tint = clamp(lg_tintColor.rgb, 0.0, 1.25);
    vec3 neutralTint = mix(vec3(0.985, 0.995, 1.015), tint, 0.18);
    float tintMix = clamp(0.025 + bodyStrength * 0.10, 0.0, 0.16);
    glass = mix(glass, glass * neutralTint, tintMix);

    // Bevel normal + Fresnel / specular model.
    float bevelSlope = edge * edge * 1.28;
    vec3 N = normalize(vec3(normal2 * bevelSlope, 1.0));
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(vec3(-0.68, -0.74, 0.88));
    vec3 H = normalize(L + V);

    float fresnel = pow(clamp(1.0 - N.z, 0.0, 1.0), 2.15);
    float specular = pow(max(dot(N, H), 0.0), mix(34.0, 22.0, debugMode));
    float frontRim = pow(max(dot(normal2, normalize(vec2(-0.72, -0.69))), 0.0), 2.0);
    float backRim  = pow(max(dot(normal2, normalize(vec2( 0.72,  0.69))), 0.0), 2.4);

    vec3 coolWhite = vec3(0.93, 0.975, 1.06);
    float rimEnergy = edge * (0.10 + glowAmount * 0.13)
                    + fresnel * (0.30 + glowAmount * 0.38)
                    + specular * (0.18 + glowAmount * 0.34)
                    + frontRim * edge * 0.10;
    glass = screenBlend(glass, coolWhite * clamp(rimEnergy, 0.0, 0.72));

    // Opposite side receives a soft inner shadow, which creates thickness.
    float innerShadow = backRim * edge * (0.085 + 0.075 * bodyStrength);
    glass *= (1.0 - innerShadow);

    // Directional moving sheen. Very subtle in normal mode.
    float angle = atan(normal2.y, normal2.x);
    float sheenPhase = angle - 0.62 + phase * 0.055;
    float sheen = (0.5 + 0.5 * sin(sheenPhase)) * edge;
    float sheenWeight = clamp(lg_pack3.x, 0.0, 1.0);
    float sheenBias = clamp(lg_pack3.y, -0.2, 0.2);
    glass *= 1.0 + sheen * sheenWeight * glowAmount * 0.075 + sheenBias * 0.10;

    // Tiny material grain prevents sterile plastic-looking gradients.
    float grain = hash21(gl_FragCoord.xy + vec2(phase * 7.0, phase * 3.0)) - 0.5;
    glass += grain * noiseAmount * 0.42;

    if (shadeAmount > 0.001) {
        float lum = dot(glass, vec3(0.2126, 0.7152, 0.0722));
        vec3 muted = mix(glass, vec3(lum), shadeAmount * 0.18);
        glass = mix(glass, muted * vec3(0.84, 0.86, 0.90), shadeAmount * 0.30);
    }

    // Diagnostic mode also leaves a very thin cyan/magenta rim. If this is
    // visible, the V10.4 shader is unquestionably the code being executed.
    if (debugMode > 0.5) {
        vec3 diagTint = mix(vec3(1.0, 0.18, 0.82), vec3(0.10, 0.92, 1.0), step(0.0, normal2.x));
        glass = mix(glass, diagTint, edge * 0.18);
    }

    glass = clamp(glass, 0.0, 1.0);
    outColor = vec4(glass, shape) * fragColor;
}

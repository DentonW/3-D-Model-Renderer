// The four programs, carried over from rockgen.glview unchanged except where
// a general model loader needs more than a single vertex-coloured mesh did:
// the mesh shader gained a material colour for meshes without vertex
// colours, a diffuse texture and an alpha cut-off. The lighting itself is
// untouched.
#pragma once

namespace shaders {

inline constexpr const char *kHeader = "#version 330 core\n";

// Shared lighting, identical to the software renderer's _shade so both
// backends agree.
inline constexpr const char *kLight = R"GLSL(
uniform vec3 uEye, uKeyDir, uKeyCol, uFillDir, uFillCol, uSky, uBounce;
uniform float uRim, uExposure;

vec3 shade(vec3 pos, vec3 n, vec3 albedo) {
    float ndl = max(dot(n, uKeyDir), 0.0);
    float ndf = max(dot(n, uFillDir), 0.0);
    float t = clamp(0.5 + 0.5 * n.y, 0.0, 1.0);
    vec3 ambient = mix(uBounce, uSky, t);
    vec3 view = normalize(uEye - pos);
    float rim = pow(clamp(1.0 - abs(dot(view, n)), 0.0, 1.0), 3.0) * uRim;
    vec3 lit = albedo * (uKeyCol * ndl + uFillCol * ndf + ambient) + vec3(rim);
    return clamp(lit * uExposure, 0.0, 1.0);
}
)GLSL";

inline constexpr const char *kMeshVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec2 aUV;
uniform mat4 uMVP;
out vec3 vPos;
out vec3 vNrm;
out vec3 vCol;
out vec2 vUV;
void main() {
    vPos = aPos;
    vNrm = aNrm;
    vCol = aCol;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char *kMeshFS = R"GLSL(
in vec3 vPos;
in vec3 vNrm;
in vec3 vCol;
in vec2 vUV;
uniform bool uFlat;
uniform bool uHasTex;
uniform bool uTwoSided;
uniform bool uVertexColor;
uniform sampler2D uTex;
uniform vec3 uBaseColor;
uniform float uAlphaCutoff;
out vec4 FragColor;
void main() {
    vec4 tex = uHasTex ? texture(uTex, vUV) : vec4(1.0);
    // Cut-out materials -- foliage, fences, decals -- carry their silhouette
    // in the alpha channel and nothing else.
    if (uHasTex && tex.a < uAlphaCutoff) discard;

    vec3 n = normalize(vNrm);
    if (uFlat) {
        // Face normal straight from the screen-space derivatives, so flat
        // shading needs no second copy of the geometry.
        vec3 fn = normalize(cross(dFdx(vPos), dFdy(vPos)));
        n = dot(fn, n) < 0.0 ? -fn : fn;
    }
    // With culling off, a back face arrives with its normal pointing away
    // from the eye, which would shade it as though it were in shadow.
    if (uTwoSided && !gl_FrontFacing) n = -n;

    vec3 albedo = (uVertexColor ? vCol : uBaseColor) * tex.rgb;
    FragColor = vec4(pow(shade(vPos, n, albedo), vec3(1.0 / 1.05)), 1.0);
}
)GLSL";

inline constexpr const char *kGroundVS = R"GLSL(
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
out vec3 vPos;
void main() {
    vPos = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char *kGroundFS = R"GLSL(
in vec3 vPos;
uniform vec3 uGround, uHorizon, uGridCol, uLight;
uniform vec2 uCentre;
uniform float uSpan, uCell, uGridStrength;
uniform bool uGrid;
out vec4 FragColor;
void main() {
    float d = length(vPos.xz - uCentre);
    float shadow = pow(clamp(1.0 - d / max(uSpan * 0.62, 1e-6), 0.0, 1.0), 1.6);
    vec3 col = uGround * uLight * (1.0 - 0.72 * shadow);
    if (uGrid) {
        // fwidth gives the exact on-screen footprint, so the lines stay one
        // pixel wide however oblique the view is.
        vec2 g = abs(fract(vPos.xz / uCell + 0.5) - 0.5) * uCell;
        vec2 w = fwidth(vPos.xz) * 1.1 + 1e-9;
        float line = 1.0 - min(min(g.x / w.x, g.y / w.y), 1.0);
        col += uGridCol * (line * line * uGridStrength);
    }
    float f = clamp(1.0 - d / max(uSpan * 14.0, 1e-6), 0.0, 1.0);
    f = f * f * (3.0 - 2.0 * f);
    col = mix(uHorizon, col, f);
    FragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 1.05)), 1.0);
}
)GLSL";

inline constexpr const char *kLineVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform mat4 uMVP;
out vec3 vCol;
void main() {
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

inline constexpr const char *kLineFS = R"GLSL(
in vec3 vCol;
out vec4 FragColor;
void main() { FragColor = vec4(vCol, 1.0); }
)GLSL";

inline constexpr const char *kBackgroundVS = R"GLSL(
layout(location = 0) in vec2 aPos;
out float vY;
void main() {
    vY = aPos.y * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

inline constexpr const char *kBackgroundFS = R"GLSL(
in float vY;
uniform vec3 uTop, uBottom;
out vec4 FragColor;
void main() { FragColor = vec4(mix(uBottom, uTop, vY), 1.0); }
)GLSL";

}  // namespace shaders

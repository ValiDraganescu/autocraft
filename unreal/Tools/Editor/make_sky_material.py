"""M_AcSky: the sky dome's material (GAME-LAYER.md §2.8, chunk B9).

Rerunnable: rebuilds the material's graph from nothing each time.

Output: /Game/Sky/M_AcSky, an unlit "Is Sky" material that AAcDaylight
(Source/Autocraft/AcDaylight.cpp) puts on a large sphere round the map:

- the SkyAtmosphere toward the pixel (`SkyAtmosphereViewLuminance`),
- the sun's and the moon's discs (`SkyAtmosphereLightDiskLuminance`, atmosphere
  lights 0 and 1),
- the Swift star field (`SkyShader.sky_color`, Sources/Autocraft/SkyShader.swift):
  at most one star in each cell of a 150 x 150 grid on each face of a cube
  round the eye, round and pixel-sized, fading in from 3° to 18° above the
  horizon. Parameter `Stars` (0-1) is the Swift `stars` value: 0 by day,
  1 from 14° of sun below the horizon. `StarGain` scales them for the
  exposure.

- the Swift haze rising from the horizon (`sky_color`'s `rise`): toward
  `Haze` (linear, the height fog's colour, set by AAcDaylight), all of it
  at and below the horizon, none from `HazeRise` degrees up (Swift's
  `top.w`): `pow(1 - smoothstep(0, rise, e), 1.6)`. The sun's disc shows
  through at 40 % in full haze, as Swift's. The height fog stops short of
  the dome (`FogCutoffDistance`), and the pilot's veil (M_AcPilotVeil)
  paints the far ground with the same haze, so ground and sky meet without
  a seam.

The real-time sky light capture sees the dome too, so the stars and the
moonlit sky tint the night's ambient a little.

Run it in the editor (`py <path>` in the console), or headless while the live
editor does not have the asset open:
  UnrealEditor-Cmd Autocraft.uproject -run=pythonscript -script=<abs path>
"""
import os

import unreal

MAT_DIR = "/Game/Sky"
MAT_NAME = "M_AcSky"

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary

# The Swift star field. `D` is the world direction from the eye (Unreal, Z
# up); Swift's is Y up: (x, z, y). The helper functions sit in a local
# struct, the usual way to have functions in a Custom node.
STARS_HLSL = r"""
struct AcStarFns {
    float hash(float3 p) {
        p = frac(p * float3(0.1031, 0.1030, 0.0973));
        p += dot(p, p.yzx + 33.33);
        return frac((p.x + p.y) * p.z);
    }
};
AcStarFns F;
float3 d = normalize(float3(D.x, D.z, D.y));
float e = asin(clamp(d.y, -1.0, 1.0)) * 57.29578;
float3 a = abs(d);
float3 q = a.x >= a.y && a.x >= a.z ? float3(d.zy / a.x, d.x > 0.0 ? 0.0 : 1.0)
    : (a.y >= a.z ? float3(d.xz / a.y, d.y > 0.0 ? 2.0 : 3.0) : float3(d.xy / a.z, d.z > 0.0 ? 4.0 : 5.0));
float2 grid = q.xy * 150.0;
float px = clamp(length(fwidth(grid)), 0.08, 0.6);
float2 cell = floor(grid);
float star = 0.0;
if (F.hash(float3(cell, q.z)) > 0.978) {
    float2 at = cell + 0.2 + 0.6 * float2(F.hash(float3(cell, q.z + 7.0)), F.hash(float3(cell, q.z + 13.0)));
    float r = length(grid - at) / px;
    star = exp(-r * r * 1.6) * (0.12 + 0.88 * pow(F.hash(float3(cell, q.z + 19.0)), 5.0));
}
star *= Stars * smoothstep(3.0, 18.0, e);
return float3(0.9, 0.93, 1.0) * star * StarGain;
"""

HAZE_HLSL = r"""
float3 d = normalize(D);
float e = asin(clamp(d.z, -1.0, 1.0)) * 57.29578;
float rise = pow(1.0 - smoothstep(0.0, max(Haze.a, 1.0), e), 1.6);
// The gas giant eclipses the sun's disc.
float wP = dot(d, PD.xyz);
float occ = 0.0;
if (wP > 0.0 && PD.w > 1.0) {
    float rr = PD.w * sqrt(max(1.0 - wP * wP, 0.0));
    occ = smoothstep(1.0, 0.985, rr);
}
return lerp(C, Haze.rgb, rise) + Sun * (1.0 - occ) * (1.0 - 0.6 * rise);
"""



# The gas giant (Source/Autocraft/AcGasGiant.cpp feeds the parameters):
# drawn from the pixel's direction alone, so it is "at infinity". Everything
# on the disc is in view-plane units: x right, y up, z toward the eye,
# lengths in planet radii; the planet's frame is P (toward it) plus
# E1 = up x P and E2 = P x E1, as the C++.
PLANET_HLSL = r"""
struct GG {
    float hash(float3 p) {
        p = frac(p * float3(0.1031, 0.1030, 0.0973));
        p += dot(p, p.yzx + 33.33);
        return frac((p.x + p.y) * p.z);
    }
    float noise(float3 x) {
        float3 i = floor(x); float3 f = frac(x); f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(hash(i), hash(i + float3(1,0,0)), f.x), lerp(hash(i + float3(0,1,0)), hash(i + float3(1,1,0)), f.x), f.y),
                    lerp(lerp(hash(i + float3(0,0,1)), hash(i + float3(1,0,1)), f.x), lerp(hash(i + float3(0,1,1)), hash(i + float3(1,1,1)), f.x), f.y), f.z);
    }
    float fbm(float3 p, int oct) {
        float s = 0.0, a = 0.5, nrm = 0.0;
        for (int i = 0; i < oct; i++) { s += a * noise(p); nrm += a; p = p * 2.03 + float3(17.1, 5.3, 9.7); a *= 0.5; }
        return s / nrm;
    }
    // The winds: radians a second, relative to the body (keep as AcGasGiant.cpp).
    float zonal(float lat) { return 0.0018 * (0.6 * cos(lat * 4.0) + 0.4 * cos(lat * 9.0 + 1.0)); }
    float wrap(float a) { return a - 6.2831853 * floor(a / 6.2831853 + 0.5); }
    float3 bandCol(float lat, float tu) {
        float b = 0.5 + 0.28 * sin(lat * 11.0 + 1.2 * sin(lat * 3.3)) + 0.17 * sin(lat * 23.0 + 0.7) + 0.09 * sin(lat * 41.0 + 2.0);
        b = saturate(b + tu);
        float3 dk = float3(0.30, 0.22, 0.17), md = float3(0.68, 0.53, 0.37), lt = float3(0.96, 0.90, 0.76);
        float3 c = b < 0.5 ? lerp(dk, md, b * 2.0) : lerp(md, lt, (b - 0.5) * 2.0);
        float pole = smoothstep(0.95, 1.4, abs(lat));
        return lerp(c, float3(0.50, 0.56, 0.64) * (0.75 + 0.35 * b), pole * 0.75);
    }
    float3 layer(float lat, float lon, float lag, float fine) {
        float l = lon - zonal(lat) * lag;
        float cl = cos(lat);
        float3 q = float3(cl * cos(l) * 3.0, sin(lat) * 15.0, cl * sin(l) * 3.0);
        // The big planet: warp the bands with a first turbulence, then read
        // the flow-stretched detail through it (billows along the band edges).
        float t = fbm(q, 5) - 0.5;
        float3 qw = q + float3(t * 0.9, t * 0.15, -t * 0.9);
        float t2 = fbm(qw * float3(2.3, 1.5, 2.3) + 7.3, 4) - 0.5;
        float t3 = fine > 0.05 ? (fbm(qw * float3(7.0, 3.2, 7.0) + 3.1, 3) - 0.5) : 0.0;
        float3 c = bandCol(lat + 0.055 * t + 0.012 * t3 * fine, 0.3 * t2 + 0.16 * t3 * fine);
        float fil = 0.5 + 0.5 * sin(lat * 130.0 + 9.0 * t2 + 4.0 * t3);
        return c * (0.93 + 0.14 * sin(lat * 70.0 + 5.0 * t)) * (1.0 - 0.1 * fine * fil * smoothstep(0.1, 0.4, abs(t2)));
    }
    float ringDensity(float rho, float detail) {
        float d = smoothstep(1.26, 1.34, rho) * smoothstep(2.46, 2.38, rho);
        d *= lerp(0.4, 1.0, smoothstep(1.52, 1.58, rho));
        d *= 1.0 - 0.93 * smoothstep(1.96, 2.0, rho) * smoothstep(2.1, 2.05, rho);
        d *= 1.0 - 0.75 * exp(-pow((rho - 2.29) * 55.0, 2.0));
        d *= 1.0 - detail * 0.28 * (0.5 - 0.5 * sin(rho * 97.0 + 2.0 * sin(rho * 23.0))) * 2.0 * 0.5 - (1.0 - detail) * 0.1;
        return saturate(d);
    }
    float streak(float2 p, float2 head, float2 tail, float w0, float w1) {
        float2 ab = head - tail; float L2 = max(dot(ab, ab), 1e-6);
        float h = saturate(dot(p - tail, ab) / L2);
        float2 e = p - tail - ab * h;
        float w = lerp(w0, w1, h);
        return exp(-dot(e, e) / (w * w)) * h * h;
    }
};
GG F;
float3 d = normalize(D);
float3 P = PD.xyz;
float Dc = PD.w;                 // the planet's centre, in radii from the eye
float W0 = sqrt(max(1.0 - 1.0 / (Dc * Dc), 0.0));
float cz = dot(d, P);
float3 E1 = normalize(cross(float3(0, 0, 1), P));
float3 E2 = cross(P, E1);
float2 uv = float2(dot(d, E1), dot(d, E2));
if (PM.w < 0.5) return C;

float G = PS.w;
float Ev = 2.0 + G * 1.5;
float T = PM.x;
float3 Av = float3(dot(PA.xyz, E1), dot(PA.xyz, E2), dot(PA.xyz, -P));
float3 Sv = float3(dot(PS.xyz, E1), dot(PS.xyz, E2), dot(PS.xyz, -P));
float3 Bv = normalize(cross(Av, float3(0, 0, 1)));
float3 Cv = cross(Bv, Av);
float spin = PA.w;

// The ray from the eye in the eye frame (x right, y up, z toward the eye); the
// planet's centre at z = -Dc. A real perspective sphere: it is big.
float3 dd = float3(uv, -cz);
float3 Cc = float3(0, 0, -Dc);
float b0 = dot(dd, Cc);
float disc0 = b0 * b0 - (Dc * Dc - 1.0);
float rr0 = Dc * length(uv);                       // closest approach, in radii
float fwr = max(Dc * (fwidth(uv.x) + fwidth(uv.y)), 1e-5);
bool front = cz > 0.0;
float edge = front ? saturate((1.0 - rr0) / fwr + 0.5) : 0.0;
bool hit = front && disc0 > 0.0;
float ts = hit ? (b0 - sqrt(disc0)) : 1e9;
float3 nRaw = (b0 - sqrt(max(disc0, 0.0))) * dd - Cc;
float3 n = normalize(nRaw);
// Disc coordinates for the events: the surface normal's x and y on the planet, beyond its limb the sky's.
float2 p = hit ? n.xy : uv * (Dc * W0);
float3 col = C;
float4 sc0 = S0; float4 sc1 = S1;
float air = saturate((1.0 - PM.y) / 0.3);
float fine = saturate(1.0 / max(fwr * 90.0, 0.35));   // detail fades where a pixel is coarser than it
float3 emit = 0;

if (front && rr0 < 1.03) {
    float r2 = dot(p, p);
    float lat = asin(clamp(dot(n, Av), -1.0, 1.0));
    float lon = atan2(dot(n, Bv), dot(n, Cv)) - spin;
    // Bands: two layers a half period apart, sheared by the winds, crossfaded.
    float ph = frac(T / 240.0);
    float w1 = sin(3.14159265 * ph); w1 *= w1;
    // The baked lat-long bands, scrolled by the winds (a longitude shift per latitude).
    float zl = F.zonal(lat);
    float lvl = max(log2(max(fwr * 652.0 / max(n.z, 0.2), 1.0)) - 0.5, 0.0);
    float vv = 0.5 - lat / 3.14159265;
    float3 alb = lerp(Texture2DSampleLevel(BandTex, BandTexSampler, float2((lon - zl * frac(ph + 0.5) * 240.0) / 6.2831853, vv), lvl).rgb,
                      Texture2DSampleLevel(BandTex, BandTexSampler, float2((lon - zl * ph * 240.0) / 6.2831853, vv), lvl).rgb, w1);
    // The storm: a large oval riding its band, with a collar and a spiral.
    float slat = 0.34;
    float slon = PM.z + F.zonal(slat) * T + 0.12 * sin(T * 0.0023);
    float dx = F.wrap(lon - slon) * cos(lat) / 0.46;
    float dy = (lat - slat) / 0.21;
    float dd2 = sqrt(dx * dx + dy * dy);
    float ang = atan2(dy, dx);
    float sa2 = ang + dd2 * 4.5 - T * 0.015;
    // Only the storm's patch pays for its swirl.
    float sw = 0.5, sw2 = 0.5;
    if (dd2 < 1.5) {
        sw = F.fbm(float3(cos(sa2) * 1.4 + 3.0, sin(sa2) * 1.4 + 3.0, dd2 * 3.0 + 3.1), 5);
        sw2 = F.fbm(float3(cos(sa2 * 2.0) * 3.4 + 9.0, sin(sa2 * 2.0) * 3.4 + 1.0, dd2 * 7.0 + 8.1), 4);
    }
    float inner = smoothstep(1.06, 0.78, dd2);
    float3 stormCol = lerp(float3(0.94, 0.66, 0.44), float3(0.62, 0.22, 0.13), saturate(sw * 2.2 - 0.35 + (sw2 - 0.5) * 0.5 * fine));
    stormCol = lerp(stormCol, float3(0.45, 0.16, 0.10), smoothstep(0.3, 0.0, dd2) * 0.7);
    float collar = smoothstep(1.0, 1.12, dd2) * smoothstep(1.45, 1.2, dd2);
    alb *= 1.0 - 0.38 * collar;
    alb = lerp(alb, stormCol, inner);
    alb = lerp(alb, float3(0.97, 0.88, 0.74), smoothstep(0.08, 0.0, abs(dd2 - 0.94)) * 0.35);
    // Impact scars: dark smears that stretch with the winds and fade.
    for (int s = 0; s < 2; s++) {
        float4 S = s == 0 ? sc0 : sc1;
        float age = S.z;
        if (age >= 0.0 && age < 420.0) {
            float sdx = F.wrap(lon - (S.y + F.zonal(S.x) * age)) * cos(S.x);
            float sdy = lat - S.x;
            float sx = 0.13 + 0.0006 * age, sy = 0.075 + 0.00012 * age;
            float rr = sdx * sdx / (sx * sx) + sdy * sdy / (sy * sy);
            float rag = 0.7 + 0.6 * F.noise(float3(sdx * 38.0, sdy * 38.0, 5.0));
            float fade = exp(-age / 150.0) * smoothstep(0.0, 5.0, age);
            float dark = (0.85 * exp(-rr * rag) + 0.3 * exp(-rr * 0.25)) * fade;
            alb = lerp(alb, alb * float3(0.1, 0.075, 0.07), saturate(dark));
            alb += float3(1.0, 0.92, 0.8) * 0.2 * exp(-rr * 0.3) * exp(-age / 14.0) * smoothstep(0.0, 1.0, age);
            float dist = length(float2(sdx, sdy));
            emit += float3(1.0, 0.78, 0.5) * exp(-dist * dist / 0.0012) * exp(-age * 1.1) * 7.0;
            float rs = 0.03 + 0.045 * pow(age, 0.7);
            emit += float3(1.0, 0.85, 0.7) * exp(-pow((dist - rs) / 0.011, 2.0)) * exp(-age / 7.0) * smoothstep(0.0, 0.3, age) * 1.6;
        }
    }
    // Light: the sun's side, warm toward the terminator, a pale blue limb.
    float ndl = dot(n, Sv);
    float diff = smoothstep(-0.1, 0.5, ndl);
    float3 warm = lerp(float3(1.0, 0.52, 0.3), float3(1.0, 0.97, 0.92), smoothstep(0.0, 0.45, ndl));
    float limb = lerp(0.5, 1.0, pow(saturate(n.z), 0.4));
    float sh = 0.0;
    float sa = dot(Sv, Av);
    if (abs(sa) > 0.02) {
        float tt = -dot(n, Av) / sa;
        float3 Y = n + Sv * tt;
        sh = tt > 0.0 ? Texture2DSampleLevel(RingTex, RingTexSampler, float2(saturate((length(Y) - 1.2) / 1.3), 0.5), 1.0).b * 0.5 : 0.0;
    }
    float rim = pow(1.0 - saturate(n.z), 3.5);
    float3 pl = alb * (0.07 * float3(0.6, 0.72, 1.0) + diff * warm * limb * (1.0 - sh)) * G;
    pl += float3(0.45, 0.62, 1.0) * rim * (0.08 + 0.5 * diff) * G;
    pl += emit * Ev * 0.35;
    // By night the planet hides the stars; by day it only adds its lit side to
    // the sky (its dark side is the sky), like the moon at noon.
    col = col * (1.0 - edge * (1.0 - air * 0.3)) + pl * edge * PM.y;
}
// The ring, in front of the planet where it is nearer than the surface. The
// eye is near its plane, so it sweeps a wide arc of the sky. Its radial
// profile is a baked 1D strip.
float adv = dot(dd, Av);
float tr = dot(Cc, Av) / (abs(adv) > 1e-4 ? adv : 1e-4);
float3 qr = tr * dd - Cc;
float rho = length(qr);
float urA = (rho - 1.2) / 1.3;
float grA = min(max(abs(ddx(urA)), abs(ddy(urA))), 0.5);   // outside the branches: valid derivatives
if (abs(Av.z) > 0.05 && tr > 0.0 && tr < ts && rho > 1.2 && rho < 2.5) {
    float ur = urA;
    float3 rt = Texture2DSampleLevel(RingTex, RingTexSampler, float2(ur, 0.5), log2(max(grA * 1024.0, 1.0))).rgb;
    float dn = rt.r;
    float b = dot(qr, Sv);
    float disc = b * b - (dot(qr, qr) - 1.0);
    float psh = b < 0.0 ? smoothstep(0.0, 0.04, disc) : 0.0;
    float faceLit = (Av.z * dot(Sv, Av) > 0.0) ? 1.0 : 0.4;
    float3 rc = lerp(float3(0.58, 0.48, 0.38), float3(0.93, 0.86, 0.72), rt.g) * 0.7 * G * faceLit * lerp(1.0, 0.035, psh);
    // Thin where the plane is seen edge-on (the eye is near it).
    dn *= saturate(abs(adv) * 14.0) * smoothstep(0.0, 0.012, ur) * smoothstep(1.0, 0.988, ur);
    col = col * (1.0 - dn * 0.92 * (1.0 - air * 0.5)) + rc * dn * 0.92 * PM.y;
}
// Events, over everything.
float3 ev = 0;
if (front && CB.x > 0.0) {
    ev += float3(0.7, 0.88, 1.0) * F.streak(p, CA.xy, CA.zw, 0.006, 0.026) * CB.x * 2.2;
    float2 hd = p - CA.xy;
    ev += float3(1.0, 0.96, 0.88) * (exp(-dot(hd, hd) / 0.0006) * 5.0 + exp(-dot(hd, hd) / 0.012) * 0.6) * CB.x;
}
float4 ma[3] = {M0A, M1A, M2A};
float4 mb[3] = {M0B, M1B, M2B};
for (int m = 0; m < 3; m++) {
    if (front && mb[m].x > 0.0) {
        ev += float3(1.0, 0.66, 0.36) * F.streak(p, ma[m].xy, ma[m].zw, 0.003, 0.009) * mb[m].x * 2.4;
        float2 hm = p - ma[m].xy;
        ev += float3(1.0, 0.85, 0.6) * exp(-dot(hm, hm) / 0.00012) * mb[m].x * 1.5;
    }
}
// Impact flashes bleed out over the limb and the sky.
for (int k = 0; k < 2; k++) {
    float4 S = k == 0 ? sc0 : sc1;
    if (S.z >= 0.0 && S.z < 12.0) {
        float lw = S.y + F.zonal(S.x) * S.z + spin;
        float3 nn = sin(S.x) * Av + cos(S.x) * (cos(lw) * Cv + sin(lw) * Bv);
        float2 hp = p - nn.xy;
        float vis = saturate(nn.z * 5.0 + 0.4);
        ev += float3(1.0, 0.85, 0.65) * (exp(-dot(hp, hp) / 0.006) * exp(-S.z * 1.6) * 5.0 + exp(-dot(hp, hp) / 0.05) * exp(-S.z * 0.8) * 0.5) * vis;
    }
}
return col + ev * Ev * lerp(1.0, 0.0, 0.0);
"""


# The sky's life (Source/Autocraft/AcSkyFx.cpp feeds the parameters): ships
# to and from the gas giant, the comet and the meteor shower. All drawn from
# the pixel's direction `d` (unit, Unreal axes, Z up), added to the sky before
# the haze, so the horizon haze softens them. FM = (clock, night, meteor
# rate, burst); SA/SB/SC the ship (head+size, tail+engine, fade/day/gain/0);
# (directions are sent as v * 0.5 + 0.5: vector parameters are clamped at 0)
# KA/KB the comet (head+strength, tail tangent+0); FA/FB the fireball.
SKYFX_HLSL = r"""
struct SF {
    float hash(float3 p) {
        p = frac(p * float3(0.1031, 0.1030, 0.0973));
        p += dot(p, p.yzx + 33.33);
        return frac((p.x + p.y) * p.z);
    }
    float noise2(float2 x) {
        float2 i = floor(x); float2 f = frac(x); f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(hash(float3(i, 1.0)), hash(float3(i + float2(1, 0), 1.0)), f.x),
                    lerp(hash(float3(i + float2(0, 1), 1.0)), hash(float3(i + float2(1, 1), 1.0)), f.x), f.y);
    }
    float seg(float3 d, float3 a, float3 b, out float h) {
        float3 ab = b - a;
        h = saturate(dot(d - a, ab) / max(dot(ab, ab), 1e-12));
        return length(d - a - ab * h);
    }
    // One meteor: along a meridian from the zenith. th/ph the pixel's polar
    // angle and azimuth, a streak from polar angle `t0` to `t1` (tail to
    // head) on azimuth `phi`, `w` its half width in radians.
    float streak(float th, float ph, float phi, float tail, float head, float w, float pw) {
        float dp = ph - phi; dp = dp - 6.2831853 * floor(dp / 6.2831853 + 0.5);
        if (cos(dp) < 0.0) return 0.0;
        float perp = sin(th) * sin(dp);
        float al = (th - tail) / max(head - tail, 1e-4);
        if (al <= 0.0 || al >= 1.0) { 
            float de = al <= 0.0 ? tail - th : th - head;
            float hh = sqrt(perp * perp + de * de);
            return al >= 1.0 ? exp(-hh * hh / (w * w * 3.0)) * 1.4 : 0.0;
        }
        float ww = max(w, pw * 0.9);
        return exp(-perp * perp / (ww * ww)) * pow(al, 2.0);
    }
};
SF F;
float3 d = normalize(D);
float pw = length(fwidth(d));
float3 ev = 0;
float vis = FM.y;

// A ship: a bright craft with an engine glow and a faint contrail.
if (SC.x > 0.001 && dot(d, SA.xyz * 2.0 - 1.0) > 0.95) {
    float3 S = SA.xyz * 2.0 - 1.0; float3 T = SB.xyz * 2.0 - 1.0;
    float sz = max(SA.w, pw * 1.5);
    float h;
    float dc = F.seg(d, S, T, h);
    float dS = length(d - S);
    float3 Eg = S + (T - S) * 0.07;
    float dE = length(d - Eg);
    float k = SC.x * SC.z;
    float eng = SB.w;
    ev += float3(0.95, 0.97, 1.0) * (exp(-dS * dS / (sz * sz)) * 6.0 + exp(-dS / (sz * 3.0)) * 0.12) * k;
    ev += float3(1.0, 0.56, 0.2) * (exp(-dE * dE / (sz * sz * 2.4)) * 4.0 + exp(-dE / (sz * 3.0)) * 0.22) * eng * k * SC.y;
    float w = sz * (0.4 + 2.4 * h);
    ev += lerp(float3(1.0, 0.62, 0.32), float3(0.62, 0.76, 1.0), h) * exp(-dc * dc / (w * w)) * pow(1.0 - h, 1.3) * 0.8 * eng * k * SC.y;
}

if (vis > 0.01) {
    // The comet.
    if (KA.w > 0.001 && dot(d, KA.xyz * 2.0 - 1.0) > 0.6) {
        float3 H = KA.xyz * 2.0 - 1.0; float3 Tn = KB.xyz * 2.0 - 1.0; float3 Nn = cross(H, Tn);
        float cz = dot(d, H);
        float2 q = float2(dot(d, Tn), dot(d, Nn)) / cz;
        float r = length(q);
        float3 c = float3(0.7, 0.95, 1.0) * (exp(-r * r / 0.000016) * 9.0 + exp(-r / 0.006) * 1.3 + exp(-r / 0.028) * 0.2);
        c += float3(1.0, 0.95, 0.85) * exp(-r * r / 0.0000025) * 6.0;
        float x = q.x;
        if (x > -0.02 && x < 0.52) {
            for (int i = 0; i < 3; i++) {
                float kk = i == 0 ? 0.35 : (i == 1 ? 0.62 : 1.0);
                float yc = -kk * x * x;
                float w = 0.007 + 0.085 * max(x, 0.0) * (0.6 + 0.4 * kk);
                float dy = q.y - yc;
                float prof = exp(-dy * dy / (w * w)) * smoothstep(-0.01, 0.02, x) * exp(-x / (0.1 + 0.03 * i)) * (1.0 - smoothstep(0.3, 0.42, x));
                float stri = 0.6 + 0.4 * F.noise2(float2(x * 9.0 + kk * 3.0, dy / w * 1.3));
                c += lerp(float3(1.0, 0.86, 0.62), float3(1.0, 0.6, 0.45), saturate(x * 3.0)) * prof * stri * 0.42;
            }
            float yi = 0.008 * sin(x * 7.0 + 1.0) * x * 2.0 + 0.015 * x * x;
            float wi = 0.004 + 0.02 * max(x, 0.0);
            float dyi = q.y - yi;
            float prof = exp(-dyi * dyi / (wi * wi)) * smoothstep(0.0, 0.015, x) * exp(-x / 0.22) * (1.0 - smoothstep(0.42, 0.52, x));
            float rays = 0.5 + 0.5 * F.noise2(float2(x * 26.0, dyi / wi * 2.2));
            c += float3(0.32, 0.62, 1.0) * prof * rays * 0.8;
        }
        ev += c * KA.w * vis * 1.0;
    }
    // The meteor shower: radiant at the zenith.
    float th = acos(clamp(d.z, -1.0, 1.0));
    float ph = atan2(d.y, d.x);
    float rate = FM.z; float burst = FM.w;
    float m = 0.0;
    float3 tint = float3(1.0, 0.93, 0.78);
    if (d.z > 0.0) {
        for (int i = 0; i < 14; i++) {
            bool bu = i >= 6;
            if (bu && burst <= 0.001) break;
            float P = bu ? 2.3 + 0.23 * (i - 6) : 17.0 + 4.1 * i;
            float tt = FM.x + i * 13.37;
            float kc = floor(tt / P);
            float lt = tt - kc * P;
            float h1 = F.hash(float3(kc, i, 1.0));
            float life = 0.5 + 0.8 * F.hash(float3(kc, i, 3.0));
            float s0 = F.hash(float3(kc, i, 2.0)) * (P - life - 0.1);
            float u = (lt - s0) / life;
            float gate = bu ? burst : rate;
            if (u > 0.0 && u < 1.0 && h1 < gate) {
                float phi = 6.2831853 * F.hash(float3(kc, i, 4.0));
                float t0 = 0.05 + 0.6 * F.hash(float3(kc, i, 5.0));
                float len = 0.1 + 0.3 * F.hash(float3(kc, i, 6.0));
                float br = 0.35 + 0.9 * pow(F.hash(float3(kc, i, 7.0)), 1.6);
                float head = t0 + len * (1.0 - (1.0 - u) * (1.0 - u));
                float tail = t0 + len * max(0.0, u - 0.5) * 1.0;
                float ls = F.streak(th, ph, phi, tail, head, 0.0009 + 0.0005 * br, pw);
                m += ls * br * sin(3.14159 * u);
            }
        }
    }
    ev += tint * m * 4.0 * vis;
    // The fireball: a long white-green trail with a bloom, then a lingering train.
    if (FB.x > 0.001) {
        float u = FA.w;
        float sp = FA.y + (FA.z - FA.y) * min(u, 1.0);
        float tl = FA.y + (FA.z - FA.y) * saturate(u - 0.55);
        float fs = F.streak(th, ph, FA.x, min(tl, sp - 0.003), sp, 0.0022, pw);
        ev += lerp(float3(1.0, 0.95, 0.85), float3(0.5, 1.0, 0.8), 0.4) * fs * FB.x * 3.0 * vis;
        if (u < 1.0) {
            float dp = ph - FA.x; dp = dp - 6.2831853 * floor(dp / 6.2831853 + 0.5);
            float2 dv = float2(th - sp, sin(th) * dp);
            float r2 = dot(dv, dv);
            ev += float3(1.0, 0.97, 0.88) * (exp(-r2 / 0.000012) * 12.0 + exp(-r2 / 0.00045) * 1.2 + exp(-r2 / 0.01) * 0.12) * FB.x * vis;
            ev += float3(0.6, 1.0, 0.8) * exp(-r2 / 0.0035) * 0.3 * FB.x * vis;
        }
    }
}
return C + ev * 3.0;
"""


def import_textures():
    """The baked gas giant maps (Tools/Editor/bake_gas_giant.py makes the PNGs)."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content-src", "sky")
    out = {}
    for name, png, srgb, comp in (
            ("T_AcGasGiantBands", "gasgiant_bands.png", True, unreal.TextureCompressionSettings.TC_DEFAULT),
            ("T_AcGasGiantRing", "gasgiant_ring.png", False, unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)):
        path = f"{MAT_DIR}/{name}"
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.normpath(os.path.join(src, png)))
        task.set_editor_property("destination_path", MAT_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.load_asset(path)
        tex.set_editor_property("srgb", srgb)
        tex.set_editor_property("compression_settings", comp)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP if name.endswith("Bands") else unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        tex.set_editor_property("never_stream", True)
        assets.save_asset(path, only_if_is_dirty=False)
        out[name] = tex
    return out


def build():
    global BAND_TEX, RING_TEX
    tx = import_textures()
    BAND_TEX, RING_TEX = tx["T_AcGasGiantBands"], tx["T_AcGasGiantRing"]
    path = f"{MAT_DIR}/{MAT_NAME}"
    mat = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("is_sky", True)
    mat.set_editor_property("two_sided", True)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def scalar(name, value, x, y):
        s = node(unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", name)
        s.set_editor_property("default_value", value)
        return s

    sky = node(unreal.MaterialExpressionSkyAtmosphereViewLuminance, -700, -200)
    sun = node(unreal.MaterialExpressionSkyAtmosphereLightDiskLuminance, -700, -80)
    sun.set_editor_property("light_index", 0)
    moon = node(unreal.MaterialExpressionSkyAtmosphereLightDiskLuminance, -700, 40)
    moon.set_editor_property("light_index", 1)

    # Toward the pixel from the eye: minus the camera vector.
    cam = node(unreal.MaterialExpressionCameraVectorWS, -1100, 200)
    neg = node(unreal.MaterialExpressionMultiply, -950, 200)
    neg.set_editor_property("const_b", -1.0)
    mel.connect_material_expressions(cam, "", neg, "A")

    stars = node(unreal.MaterialExpressionCustom, -700, 200)
    stars.set_editor_property("code", STARS_HLSL)
    stars.set_editor_property("description", "Swift star field")
    stars.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ("D", "Stars", "StarGain"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    stars.set_editor_property("inputs", inputs)
    if not mel.connect_material_expressions(neg, "", stars, "D"):
        raise RuntimeError("could not connect D")
    if not mel.connect_material_expressions(scalar("Stars", 0.0, -1100, 320), "", stars, "Stars"):
        raise RuntimeError("could not connect Stars")
    if not mel.connect_material_expressions(scalar("StarGain", 1.5, -1100, 400), "", stars, "StarGain"):
        raise RuntimeError("could not connect StarGain")

    add2 = node(unreal.MaterialExpressionAdd, -250, -50)
    mel.connect_material_expressions(sky, "", add2, "A")
    mel.connect_material_expressions(moon, "", add2, "B")
    add3 = node(unreal.MaterialExpressionAdd, -100, 50)
    mel.connect_material_expressions(add2, "", add3, "A")
    mel.connect_material_expressions(stars, "", add3, "B")


    # The gas giant, between the sky and the haze.
    def vec4(name, default, x, y):
        vp = node(unreal.MaterialExpressionVectorParameter, x, y)
        vp.set_editor_property("parameter_name", name)
        vp.set_editor_property("default_value", unreal.LinearColor(*default))
        ap = node(unreal.MaterialExpressionAppendVector, x + 150, y)
        mel.connect_material_expressions(vp, "", ap, "A")
        mel.connect_material_expressions(vp, "A", ap, "B")
        return ap

    def tex_obj(name, tex, x, y):
        t = node(unreal.MaterialExpressionTextureObjectParameter, x, y)
        t.set_editor_property("parameter_name", name)
        t.set_editor_property("texture", tex)
        return t

    planet = node(unreal.MaterialExpressionCustom, 0, 600)
    planet.set_editor_property("code", PLANET_HLSL)
    planet.set_editor_property("description", "Gas giant")
    planet.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    plist = [("C", None, None), ("D", None, None),
             ("PD", "PlanetDir", (0.3, 0.3, 0.9, 2.2)), ("PA", "PlanetAxis", (0, 0, 1, 0)),
             ("PS", "PlanetSun", (0.5, 0.5, 0.5, 1)), ("PM", "PlanetMisc", (0, 1, 0, 0)),
             ("CA", "CometA", (0, 0, 0, 0)), ("CB", "CometB", (0, 0, 0, 0)),
             ("S0", "Scar0", (0, 0, -1, 0)), ("S1", "Scar1", (0, 0, -1, 0))]
    for i in range(3):
        plist.append((f"M{i}A", f"Met{i}A", (0, 0, 0, 0)))
        plist.append((f"M{i}B", f"Met{i}B", (0, 0, 0, 0)))
    plist += [("BandTex", None, None), ("RingTex", None, None)]
    pin = []
    for inp, _, _ in plist:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", inp)
        pin.append(ci)
    planet.set_editor_property("inputs", pin)
    y = 700
    for inp, pname, default in plist:
        if inp == "C":
            src = add3
        elif inp == "D":
            src = neg
        elif inp in ("BandTex", "RingTex"):
            src = tex_obj(inp, BAND_TEX if inp == "BandTex" else RING_TEX, -1700, y)
            y += 90
        else:
            src = vec4(pname, default, -1400, y)
            y += 90
            if pname == "PlanetDir":
                planet_dir = src
        if not mel.connect_material_expressions(src, "", planet, inp):
            raise RuntimeError(f"could not connect {inp}")

    # The sky's life, between the planet and the haze.
    skyfx = node(unreal.MaterialExpressionCustom, 250, 600)
    skyfx.set_editor_property("code", SKYFX_HLSL)
    skyfx.set_editor_property("description", "Ships, comet, meteors")
    skyfx.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    flist = [("C", None, None), ("D", None, None),
             ("FM", "SkyFxMisc", (0, 0, 0.35, 0)), ("SA", "ShipA", (0, 0, 1, 0)), ("SB", "ShipB", (0, 0, 1, 0)),
             ("SC", "ShipC", (0, 0, 0, 0)), ("KA", "CometA2", (0, 0, 1, 0)), ("KB", "CometB2", (1, 0, 0, 0)),
             ("FA", "FireA", (0, 0, 0, 0)), ("FB", "FireB", (0, 0, 0, 0))]
    fin = []
    for inp, _, _ in flist:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", inp)
        fin.append(ci)
    skyfx.set_editor_property("inputs", fin)
    y = 1700
    for inp, pname, default in flist:
        if inp == "C":
            src = planet
        elif inp == "D":
            src = neg
        else:
            src = vec4(pname, default, -1400, y)
            y += 90
        if not mel.connect_material_expressions(src, "", skyfx, inp):
            raise RuntimeError(f"could not connect {inp}")

    # The haze rising from the horizon over all but the sun's disc.
    haze_p = node(unreal.MaterialExpressionVectorParameter, -400, 300)
    haze_p.set_editor_property("parameter_name", "Haze")
    haze_p.set_editor_property("default_value", unreal.LinearColor(0.34, 0.32, 0.3, 12.0))
    haze4 = node(unreal.MaterialExpressionAppendVector, -250, 300)
    mel.connect_material_expressions(haze_p, "", haze4, "A")
    mel.connect_material_expressions(haze_p, "A", haze4, "B")
    haze = node(unreal.MaterialExpressionCustom, 100, 50)
    haze.set_editor_property("code", HAZE_HLSL)
    haze.set_editor_property("description", "Swift haze rising from the horizon")
    haze.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    hin = []
    for name in ("C", "Sun", "D", "Haze", "PD"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        hin.append(ci)
    haze.set_editor_property("inputs", hin)
    for src, pin in ((skyfx, "C"), (sun, "Sun"), (neg, "D"), (haze4, "Haze"), (planet_dir, "PD")):
        if not mel.connect_material_expressions(src, "", haze, pin):
            raise RuntimeError(f"could not connect {pin}")
    if not mel.connect_material_property(haze, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("could not connect emissive")

    mel.recompile_material(mat)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


unreal.log(f"make_sky_material: built {build().get_path_name()}")

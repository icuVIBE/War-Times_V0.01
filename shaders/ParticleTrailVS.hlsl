// ============================================================
// ParticleTrailVS.hlsl — Trail / ribbon vertex shader
//   Reads consecutive trail points from a structured buffer
//   and expands them into a ribbon strip facing the camera.
// ============================================================

#include "Common.hlsli"
#include "ParticleCommon.hlsli"

// Trail point buffer: each entry stores a position + half-width + color
struct TrailPoint {
    float3 position;
    float  halfWidth;
    float4 color;
};

StructuredBuffer<TrailPoint> TrailPoints : register(t2);

struct VSOutput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    // Each trail segment uses 6 vertices (2 triangles), like billboard
    // vertexID / 6 = segment index, vertexID % 6 = corner within segment
    uint segIdx   = vertexID / 6;
    uint cornerID = vertexID % 6;

    // We need two consecutive trail points for a segment
    TrailPoint p0 = TrailPoints[segIdx];
    TrailPoint p1 = TrailPoints[segIdx + 1];

    // Direction along ribbon
    float3 segDir = p1.position - p0.position;
    float segLen = length(segDir);
    if (segLen < 0.0001) segDir = float3(0, 1, 0);
    else segDir /= segLen;

    // Camera direction for billboarding the ribbon
    float3 toCam0 = normalize(gCameraPosition - p0.position);
    float3 toCam1 = normalize(gCameraPosition - p1.position);

    float3 right0 = normalize(cross(segDir, toCam0));
    float3 right1 = normalize(cross(segDir, toCam1));

    // 4 corners of the segment quad
    float3 corners[4];
    corners[0] = p0.position - right0 * p0.halfWidth; // bottom-left
    corners[1] = p0.position + right0 * p0.halfWidth; // bottom-right
    corners[2] = p1.position - right1 * p1.halfWidth; // top-left
    corners[3] = p1.position + right1 * p1.halfWidth; // top-right

    float4 colors[4] = { p0.color, p0.color, p1.color, p1.color };
    float2 uvs[4] = { float2(0, 1), float2(1, 1), float2(0, 0), float2(1, 0) };

    // Map 6 vertices (2 triangles) to 4 corners: 0,2,3, 0,3,1
    static const uint triMap[6] = { 0, 2, 3, 0, 3, 1 };
    uint ci = triMap[cornerID];

    VSOutput o;
    o.position = mul(float4(corners[ci], 1.0), gViewProjection);
    o.texCoord = uvs[ci];
    o.color    = colors[ci];
    return o;
}

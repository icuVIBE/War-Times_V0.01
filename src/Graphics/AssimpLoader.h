#pragma once

#include <string>
#include <d3d11.h>

namespace WT {

class Mesh;

// ============================================================
// AssimpLoader — loads standard 3D model formats via Assimp
//
// Supported formats: .obj, .fbx, .gltf, .glb, .blend
//
// All geometry is converted to the engine's VertexPosNormalColor
// format (pos3f + norm3f + color4f + uv2f = 48 bytes).
// Multiple meshes in a scene are merged into a single Mesh.
// ============================================================
class AssimpLoader {
public:
    // Load a model file, creating a GPU Mesh
    static bool LoadModel(ID3D11Device* device, const std::wstring& filepath,
                          Mesh& outMesh);

    // Check if a file extension is supported by Assimp loader
    static bool IsSupportedFormat(const std::wstring& extension);
};

} // namespace WT

#include "AssimpLoader.h"
#include "Mesh.h"
#include "Core/ResourceManager.h"
#include "Util/Log.h"
#include "Util/MathHelpers.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <vector>
#include <cfloat>
#include <algorithm>
#include <filesystem>

namespace WT {

// Supported file extensions (lowercase, with dot)
static const wchar_t* s_supportedExts[] = {
    L".obj", L".fbx", L".gltf", L".glb", L".blend"
};

bool AssimpLoader::IsSupportedFormat(const std::wstring& ext) {
    std::wstring lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    for (auto& supported : s_supportedExts) {
        if (lower == supported) return true;
    }
    return false;
}

// Recursively collect all meshes from a scene node, applying transforms
static void CollectMeshes(const aiScene* scene, const aiNode* node,
                          const aiMatrix4x4& parentTransform,
                          std::vector<VertexPosNormalColor>& outVerts,
                          std::vector<UINT>& outIndices) {
    aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;

    // Normal matrix = inverse transpose of the 3x3 part
    aiMatrix3x3 normalMatrix(globalTransform);
    normalMatrix.Inverse().Transpose();

    for (unsigned int m = 0; m < node->mNumMeshes; m++) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];
        UINT baseVertex = static_cast<UINT>(outVerts.size());

        // Get material color for vertex color fallback
        float matR = 0.8f, matG = 0.8f, matB = 0.8f, matA = 1.0f;
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

            // If material has a diffuse texture, use white vertex color so
            // the texture passes through unmodified (shader multiplies them)
            aiString texPath;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS
                && texPath.length > 0) {
                matR = 1.0f; matG = 1.0f; matB = 1.0f; matA = 1.0f;
            } else {
                aiColor4D diffuse;
                if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
                    matR = diffuse.r; matG = diffuse.g;
                    matB = diffuse.b; matA = diffuse.a;
                }
            }
        }

        // Vertices
        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            VertexPosNormalColor vert = {};

            // Position: apply node transform, then convert Assimp→D3D11 (no axis swap needed,
            // Assimp normalizes to right-handed Y-up; we just negate Z for LH)
            aiVector3D pos = globalTransform * mesh->mVertices[v];
            vert.Position = { pos.x, pos.y, pos.z };

            // Normal: apply normal matrix
            if (mesh->HasNormals()) {
                aiVector3D norm = normalMatrix * mesh->mNormals[v];
                norm.Normalize();
                vert.Normal = { norm.x, norm.y, norm.z };
            }

            // Vertex color (first color channel) or material diffuse fallback
            if (mesh->HasVertexColors(0)) {
                const auto& c = mesh->mColors[0][v];
                vert.Color = { c.r, c.g, c.b, c.a };
            } else {
                vert.Color = { matR, matG, matB, matA };
            }

            // UV (first UV channel)
            if (mesh->HasTextureCoords(0)) {
                vert.TexCoord = { mesh->mTextureCoords[0][v].x,
                                  mesh->mTextureCoords[0][v].y };
            }

            outVerts.push_back(vert);
        }

        // Indices
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            // Should always be 3 after aiProcess_Triangulate
            for (unsigned int i = 0; i < face.mNumIndices; i++) {
                outIndices.push_back(baseVertex + face.mIndices[i]);
            }
        }
    }

    // Recurse children
    for (unsigned int c = 0; c < node->mNumChildren; c++) {
        CollectMeshes(scene, node->mChildren[c], globalTransform, outVerts, outIndices);
    }
}

bool AssimpLoader::LoadModel(ID3D11Device* device, const std::wstring& filepath,
                             Mesh& outMesh) {
    // Convert wstring path to UTF-8 for Assimp
    std::filesystem::path fsPath(filepath);
    std::string pathUtf8 = fsPath.string();

    Assimp::Importer importer;

    unsigned int flags =
        aiProcess_Triangulate |           // Triangulate all faces
        aiProcess_GenSmoothNormals |      // Generate normals if missing
        aiProcess_JoinIdenticalVertices | // Weld duplicate vertices
        aiProcess_CalcTangentSpace |      // Calc tangents (future use)
        aiProcess_PreTransformVertices |  // Flatten scene graph
        aiProcess_FlipUVs;               // Flip V for D3D convention

    const aiScene* scene = importer.ReadFile(pathUtf8, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        LOG_ERROR("AssimpLoader: Failed to load '%s': %s",
                  pathUtf8.c_str(), importer.GetErrorString());
        return false;
    }

    // Collect all geometry
    std::vector<VertexPosNormalColor> vertices;
    std::vector<UINT> indices;
    aiMatrix4x4 identity;
    CollectMeshes(scene, scene->mRootNode, identity, vertices, indices);

    if (vertices.empty() || indices.empty()) {
        LOG_ERROR("AssimpLoader: No geometry found in '%s'", pathUtf8.c_str());
        return false;
    }

    // Compute bounds
    XMFLOAT3 mn = { FLT_MAX, FLT_MAX, FLT_MAX };
    XMFLOAT3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& v : vertices) {
        mn.x = (std::min)(mn.x, v.Position.x);
        mn.y = (std::min)(mn.y, v.Position.y);
        mn.z = (std::min)(mn.z, v.Position.z);
        mx.x = (std::max)(mx.x, v.Position.x);
        mx.y = (std::max)(mx.y, v.Position.y);
        mx.z = (std::max)(mx.z, v.Position.z);
    }

    // Create GPU mesh
    if (!outMesh.Create(device, vertices, indices)) {
        LOG_ERROR("AssimpLoader: Failed to create GPU mesh for '%s'", pathUtf8.c_str());
        return false;
    }
    outMesh.SetBounds(mn, mx);

    LOG_INFO("AssimpLoader: Loaded '%s' — %u verts, %u tris",
             pathUtf8.c_str(),
             static_cast<unsigned>(vertices.size()),
             static_cast<unsigned>(indices.size() / 3));
    return true;
}

} // namespace WT

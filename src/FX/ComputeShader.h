#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>

namespace WT {

using Microsoft::WRL::ComPtr;

// Standalone compute shader wrapper with hot-reload support
class ComputeShader {
public:
    bool Load(ID3D11Device* device, const std::wstring& path, const char* entryPoint);

    void Bind(ID3D11DeviceContext* ctx) const;
    void Dispatch(ID3D11DeviceContext* ctx, UINT groupsX, UINT groupsY = 1, UINT groupsZ = 1) const;

    // Unbind all CS UAVs/SRVs (call after dispatch)
    static void UnbindAll(ID3D11DeviceContext* ctx);

    bool IsValid() const { return m_shader != nullptr; }

    // Hot-reload
    bool HasFileChanged() const;
    bool Reload(ID3D11Device* device);

private:
    ComPtr<ID3DBlob> CompileCS(const std::wstring& path, const char* entryPoint);

    ComPtr<ID3D11ComputeShader> m_shader;
    std::wstring m_path;
    std::string  m_entry;
    FILETIME     m_lastWrite = {};
};

} // namespace WT

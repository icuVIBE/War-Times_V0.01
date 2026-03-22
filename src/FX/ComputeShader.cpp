#include "ComputeShader.h"
#include "Util/Log.h"

namespace WT {

bool ComputeShader::Load(ID3D11Device* device, const std::wstring& path, const char* entryPoint) {
    auto blob = CompileCS(path, entryPoint);
    if (!blob) return false;

    HRESULT hr = device->CreateComputeShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        m_shader.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("CreateComputeShader failed"); return false; }

    m_path  = path;
    m_entry = entryPoint;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        m_lastWrite = fad.ftLastWriteTime;

    return true;
}

void ComputeShader::Bind(ID3D11DeviceContext* ctx) const {
    ctx->CSSetShader(m_shader.Get(), nullptr, 0);
}

void ComputeShader::Dispatch(ID3D11DeviceContext* ctx, UINT groupsX, UINT groupsY, UINT groupsZ) const {
    ctx->CSSetShader(m_shader.Get(), nullptr, 0);
    ctx->Dispatch(groupsX, groupsY, groupsZ);
}

void ComputeShader::UnbindAll(ID3D11DeviceContext* ctx) {
    ctx->CSSetShader(nullptr, nullptr, 0);
    ID3D11UnorderedAccessView* nullUAVs[8] = {};
    ctx->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
    ID3D11ShaderResourceView* nullSRVs[8] = {};
    ctx->CSSetShaderResources(0, 8, nullSRVs);
    ID3D11Buffer* nullCBs[2] = {};
    ctx->CSSetConstantBuffers(0, 2, nullCBs);
}

bool ComputeShader::HasFileChanged() const {
    if (m_path.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &fad)) return false;
    return CompareFileTime(&fad.ftLastWriteTime, &m_lastWrite) != 0;
}

bool ComputeShader::Reload(ID3D11Device* device) {
    if (m_path.empty()) return false;
    auto blob = CompileCS(m_path, m_entry.c_str());
    if (!blob) { LOG_ERROR("CS hot-reload failed"); return false; }

    m_shader.Reset();
    HRESULT hr = device->CreateComputeShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        m_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &fad))
        m_lastWrite = fad.ftLastWriteTime;

    LOG_INFO("Compute shader reloaded");
    return true;
}

ComPtr<ID3DBlob> ComputeShader::CompileCS(const std::wstring& path, const char* entryPoint) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> shaderBlob, errorBlob;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, "cs_5_0", flags, 0,
        shaderBlob.GetAddressOf(), errorBlob.GetAddressOf());

    if (FAILED(hr)) {
        if (errorBlob)
            LOG_ERROR("CS compile error: %s", static_cast<const char*>(errorBlob->GetBufferPointer()));
        else
            LOG_ERROR("Failed to compile compute shader (file not found?)");
        return nullptr;
    }
    return shaderBlob;
}

} // namespace WT

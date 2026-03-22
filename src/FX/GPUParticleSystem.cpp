#include "GPUParticleSystem.h"
#include "Util/Log.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstdlib>

namespace WT {

// GPU particle struct size must match HLSL (96 bytes)
struct GPUParticleGPU {
    XMFLOAT3 position;     float age;
    XMFLOAT3 velocity;     float lifetime;
    XMFLOAT4 colorStart;
    XMFLOAT4 colorEnd;
    float sizeStart;        float sizeEnd;
    float rotation;         float rotationSpeed;
    UINT  alive;            UINT  emitterType;
    float bounciness;       float drag;
};
static_assert(sizeof(GPUParticleGPU) == 96, "GPUParticle must be 96 bytes");

// ============================================================
// Init
// ============================================================
bool GPUParticleSystem::Init(ID3D11Device* device, const std::wstring& shaderDir, UINT maxParticles) {
    m_maxParticles = maxParticles;

    if (!CreateBuffers(device)) return false;
    if (!CreateShaders(device, shaderDir)) return false;

    // Run init CS to fill dead list
    ComPtr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(ctx.GetAddressOf());

    // Bind CBParticleSim (b6) with maxParticles
    CBParticleSim simData = {};
    simData.DeltaTime = 0;
    simData.MaxParticles = static_cast<float>(m_maxParticles);
    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(m_cbSimulate.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &simData, sizeof(simData));
    ctx->Unmap(m_cbSimulate.Get(), 0);
    ctx->CSSetConstantBuffers(6, 1, m_cbSimulate.GetAddressOf());

    ID3D11UnorderedAccessView* uavs[] = { m_particlePoolUAV.Get(), m_deadListUAV.Get(), m_countersUAV.Get() };
    ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
    m_initCS.Dispatch(ctx.Get(), (m_maxParticles + 255) / 256);
    ComputeShader::UnbindAll(ctx.Get());

    m_initialized = true;
    LOG_INFO("GPUParticleSystem initialized (%u max particles)", m_maxParticles);
    return true;
}

void GPUParticleSystem::Shutdown() {
    m_particlePool.Reset(); m_deadList.Reset(); m_aliveList.Reset();
    m_counters.Reset(); m_drawArgs.Reset(); m_counterStaging.Reset();
    m_particlePoolUAV.Reset(); m_deadListUAV.Reset(); m_aliveListUAV.Reset();
    m_countersUAV.Reset(); m_drawArgsUAV.Reset();
    m_particlePoolSRV.Reset(); m_aliveListSRV.Reset();
    m_cbEmitter.Reset(); m_cbSimulate.Reset();
    m_billboardVS.Reset(); m_billboardPS.Reset();
    m_blendAlpha.Reset(); m_blendAdditive.Reset(); m_depthReadOnly.Reset();
    m_emitters.clear();
    m_pendingBursts.clear();
    m_initialized = false;
}

// ============================================================
// Buffer creation
// ============================================================
bool GPUParticleSystem::CreateBuffers(ID3D11Device* device) {
    HRESULT hr;
    const UINT stride = sizeof(GPUParticleGPU); // 96 bytes

    // ---- Particle Pool (Structured Buffer + UAV + SRV) ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = stride * m_maxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = stride;
        hr = device->CreateBuffer(&desc, nullptr, m_particlePool.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create particle pool buffer"); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = m_maxParticles;
        hr = device->CreateUnorderedAccessView(m_particlePool.Get(), &uavDesc, m_particlePoolUAV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create particle pool UAV"); return false; }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = m_maxParticles;
        hr = device->CreateShaderResourceView(m_particlePool.Get(), &srvDesc, m_particlePoolSRV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create particle pool SRV"); return false; }
    }

    // ---- Dead List (Structured Buffer<uint> + UAV) ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(UINT) * m_maxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(UINT);
        hr = device->CreateBuffer(&desc, nullptr, m_deadList.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create dead list buffer"); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = m_maxParticles;
        hr = device->CreateUnorderedAccessView(m_deadList.Get(), &uavDesc, m_deadListUAV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create dead list UAV"); return false; }
    }

    // ---- Alive List (Structured Buffer<uint> + UAV + SRV) ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(UINT) * m_maxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(UINT);
        hr = device->CreateBuffer(&desc, nullptr, m_aliveList.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create alive list buffer"); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = m_maxParticles;
        hr = device->CreateUnorderedAccessView(m_aliveList.Get(), &uavDesc, m_aliveListUAV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create alive list UAV"); return false; }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = m_maxParticles;
        hr = device->CreateShaderResourceView(m_aliveList.Get(), &srvDesc, m_aliveListSRV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create alive list SRV"); return false; }
    }

    // ---- Counters (ByteAddress Buffer + UAV) ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 16; // [0]=deadCount, [4]=aliveCount (need 4-byte aligned, at least 16)
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        hr = device->CreateBuffer(&desc, nullptr, m_counters.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create counters buffer"); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.NumElements = 4; // 4 x 32-bit = 16 bytes
        hr = device->CreateUnorderedAccessView(m_counters.Get(), &uavDesc, m_countersUAV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create counters UAV"); return false; }
    }

    // ---- Draw Args (ByteAddress Buffer for DrawInstancedIndirect) ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 16; // 4 x uint32 for DrawInstancedIndirect
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        hr = device->CreateBuffer(&desc, nullptr, m_drawArgs.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create draw args buffer"); return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.NumElements = 4;
        hr = device->CreateUnorderedAccessView(m_drawArgs.Get(), &uavDesc, m_drawArgsUAV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create draw args UAV"); return false; }
    }

    // ---- Counter Staging Buffer (CPU readback) ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 16;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateBuffer(&desc, nullptr, m_counterStaging.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create counter staging buffer"); return false; }
    }

    // ---- Constant Buffers ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(CBParticleEmitter);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateBuffer(&desc, nullptr, m_cbEmitter.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create emitter CB"); return false; }
    }
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(CBParticleSim);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateBuffer(&desc, nullptr, m_cbSimulate.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("Failed to create simulate CB"); return false; }
    }

    // ---- Blend States ----
    {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device->CreateBlendState(&bd, m_blendAlpha.GetAddressOf());
        if (FAILED(hr)) return false;

        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        hr = device->CreateBlendState(&bd, m_blendAdditive.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    // ---- Depth Read-Only State ----
    {
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable = TRUE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; // read-only
        dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        hr = device->CreateDepthStencilState(&dsd, m_depthReadOnly.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    return true;
}

// ============================================================
// Shader creation
// ============================================================
bool GPUParticleSystem::CreateShaders(ID3D11Device* device, const std::wstring& shaderDir) {
    // Compute shaders
    if (!m_initCS.Load(device, shaderDir + L"ParticleInitCS.hlsl", "CSMain")) return false;
    if (!m_resetCS.Load(device, shaderDir + L"ParticleResetCS.hlsl", "CSMain")) return false;
    if (!m_spawnCS.Load(device, shaderDir + L"ParticleSpawnCS.hlsl", "CSMain")) return false;
    if (!m_simulateCS.Load(device, shaderDir + L"ParticleSimulateCS.hlsl", "CSMain")) return false;
    if (!m_buildArgsCS.Load(device, shaderDir + L"ParticleBuildArgsCS.hlsl", "CSMain")) return false;

    // Billboard VS (no input layout — uses SV_VertexID + SV_InstanceID)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        ComPtr<ID3DBlob> blob, errBlob;
        std::wstring path = shaderDir + L"ParticleBillboardVS.hlsl";
        HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                         "VSMain", "vs_5_0", flags, 0,
                                         blob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) {
            if (errBlob) LOG_ERROR("BillboardVS: %s", (const char*)errBlob->GetBufferPointer());
            return false;
        }
        hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                                         m_billboardVS.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    // Billboard PS
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        ComPtr<ID3DBlob> blob, errBlob;
        std::wstring path = shaderDir + L"ParticleBillboardPS.hlsl";
        HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                         "PSMain", "ps_5_0", flags, 0,
                                         blob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) {
            if (errBlob) LOG_ERROR("BillboardPS: %s", (const char*)errBlob->GetBufferPointer());
            return false;
        }
        hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                                        m_billboardPS.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    return true;
}

// ============================================================
// Emitter management
// ============================================================
int GPUParticleSystem::AddEmitter(const ParticleEmitterDef& def, const XMFLOAT3& pos, float duration) {
    EmitterInstance inst;
    inst.id = m_nextEmitterId++;
    inst.def = def;
    inst.position = pos;
    inst.duration = duration;
    inst.alive = true;
    m_emitters.push_back(inst);
    return inst.id;
}

void GPUParticleSystem::RemoveEmitter(int id) {
    for (auto& e : m_emitters) {
        if (e.id == id) { e.alive = false; break; }
    }
}

void GPUParticleSystem::SetEmitterPosition(int id, const XMFLOAT3& pos) {
    for (auto& e : m_emitters) {
        if (e.id == id) { e.position = pos; break; }
    }
}

EmitterInstance* GPUParticleSystem::GetEmitter(int id) {
    for (auto& e : m_emitters) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void GPUParticleSystem::SpawnBurst(const XMFLOAT3& pos, const ParticleEmitterDef& def, int count) {
    BurstRequest req;
    req.position = pos;
    req.direction = { 0.0f, 1.0f, 0.0f };
    req.def = def;
    req.count = count;
    m_pendingBursts.push_back(req);
}

void GPUParticleSystem::SpawnBurst(const XMFLOAT3& pos, const XMFLOAT3& direction,
                                    const ParticleEmitterDef& def, int count) {
    BurstRequest req;
    req.position = pos;
    req.direction = direction;
    req.def = def;
    req.count = count;
    m_pendingBursts.push_back(req);
}

// ============================================================
// Internal spawn dispatch
// ============================================================
void GPUParticleSystem::DispatchSpawn(ID3D11DeviceContext* ctx, const XMFLOAT3& pos,
                                       const XMFLOAT3& direction,
                                       const ParticleEmitterDef& def, int count, float seed) {
    if (count <= 0) return;

    // Fill emitter CB
    CBParticleEmitter emitData = {};
    emitData.EmitPosition = pos;
    emitData.SpawnCount = static_cast<float>(count);
    emitData.VelocityMin = def.velocityMin;
    emitData.LifetimeMin = def.lifetimeMin;
    emitData.VelocityMax = def.velocityMax;
    emitData.LifetimeMax = def.lifetimeMax;
    emitData.ColorStart = def.colorStart;
    emitData.ColorEnd = def.colorEnd;
    emitData.SizeStartMin = def.sizeStartMin;
    emitData.SizeStartMax = def.sizeStartMax;
    emitData.SizeEndMin = def.sizeEndMin;
    emitData.SizeEndMax = def.sizeEndMax;
    emitData.Gravity = def.gravity;
    emitData.Drag = def.drag;
    emitData.RotMin = def.rotationMin;
    emitData.RotMax = def.rotationMax;
    emitData.RotSpeedMin = def.rotSpeedMin;
    emitData.RotSpeedMax = def.rotSpeedMax;
    emitData.ShapeRadius = def.shapeRadius;
    emitData.Bounciness = def.bounciness;
    emitData.Shape = static_cast<uint32_t>(def.shape);
    emitData.Flags = (def.depthCollision ? 1u : 0u) | (def.additive ? 2u : 0u);
    emitData.ConeAngle = def.shapeConeAngle;
    emitData.Speed = def.speed;
    emitData.ShapeBoxHalf = def.shapeBoxHalf;
    emitData.RandomSeed = seed;
    emitData.EmitDirection = direction;
    emitData.Pad0 = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(m_cbEmitter.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &emitData, sizeof(emitData));
    ctx->Unmap(m_cbEmitter.Get(), 0);
    ctx->CSSetConstantBuffers(5, 1, m_cbEmitter.GetAddressOf());

    // Bind UAVs: u0=pool, u1=deadList, u2=counters
    ID3D11UnorderedAccessView* uavs[] = { m_particlePoolUAV.Get(), m_deadListUAV.Get(), m_countersUAV.Get() };
    ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

    m_spawnCS.Dispatch(ctx, (count + 255) / 256);
}

// ============================================================
// Per-frame GPU update
// ============================================================
void GPUParticleSystem::Update(ID3D11DeviceContext* ctx, float dt,
                                ID3D11ShaderResourceView* depthSRV,
                                ID3D11Buffer* cbPerFrame) {
    if (!m_initialized) return;
    m_frameTime += dt;

    // ---- Step 1: Reset alive counter ----
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, m_countersUAV.Get() };
        ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
        m_resetCS.Dispatch(ctx, 1);
    }

    // ---- Step 2: Process continuous emitters & pending bursts ----
    // Bind per-frame CB (b0) for compute shaders too (needed by simulate)
    ctx->CSSetConstantBuffers(0, 1, &cbPerFrame);

    // Update simulate CB (b6)
    {
        CBParticleSim simData = {};
        simData.DeltaTime = dt;
        simData.MaxParticles = static_cast<float>(m_maxParticles);
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(m_cbSimulate.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &simData, sizeof(simData));
        ctx->Unmap(m_cbSimulate.Get(), 0);
        ctx->CSSetConstantBuffers(6, 1, m_cbSimulate.GetAddressOf());
    }

    float seedBase = m_frameTime * 1000.0f;

    // Continuous emitters: accumulate spawn count
    for (auto& em : m_emitters) {
        if (!em.alive) continue;

        em.age += dt;
        if (em.duration > 0 && em.age >= em.duration) {
            em.alive = false;
            continue;
        }

        em.spawnAccum += em.def.spawnRate * dt;
        int toSpawn = static_cast<int>(em.spawnAccum);
        if (toSpawn > 0) {
            em.spawnAccum -= static_cast<float>(toSpawn);
            DispatchSpawn(ctx, em.position, em.direction, em.def, toSpawn, seedBase + em.id * 7919.0f);
        }
    }

    // One-shot bursts
    for (auto& burst : m_pendingBursts) {
        DispatchSpawn(ctx, burst.position, burst.direction, burst.def, burst.count, seedBase + 104729.0f);
        seedBase += 13.0f;
    }
    m_pendingBursts.clear();

    // Remove dead emitters
    m_emitters.erase(
        std::remove_if(m_emitters.begin(), m_emitters.end(),
            [](const EmitterInstance& e) { return !e.alive; }),
        m_emitters.end());

    // ---- Step 3: Simulate all particles ----
    {
        ID3D11UnorderedAccessView* uavs[] = {
            m_particlePoolUAV.Get(), m_deadListUAV.Get(),
            m_countersUAV.Get(), m_aliveListUAV.Get()
        };
        ctx->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);

        // Bind depth texture for collision (t0 in CS)
        if (depthSRV) {
            ctx->CSSetShaderResources(0, 1, &depthSRV);
        }

        m_simulateCS.Dispatch(ctx, (m_maxParticles + 255) / 256);
    }

    // ---- Step 4: Build indirect draw args ----
    {
        ID3D11UnorderedAccessView* uavs[] = {
            nullptr, nullptr, m_countersUAV.Get(), nullptr, m_drawArgsUAV.Get()
        };
        ctx->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
        m_buildArgsCS.Dispatch(ctx, 1);
    }

    // Unbind all CS resources
    ComputeShader::UnbindAll(ctx);

    // ---- Step 5: Readback alive count (1 frame latency) ----
    ctx->CopyResource(m_counterStaging.Get(), m_counters.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(m_counterStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        auto* data = static_cast<const UINT*>(mapped.pData);
        m_aliveCountReadback = data[1]; // [4 bytes offset] = aliveCount
        ctx->Unmap(m_counterStaging.Get(), 0);
    }
}

// ============================================================
// Render billboard particles
// ============================================================
void GPUParticleSystem::Render(ID3D11DeviceContext* ctx,
                                ID3D11Buffer* cbPerFrame,
                                ID3D11ShaderResourceView* defaultWhiteSRV,
                                ID3D11ShaderResourceView* depthSRV) {
    if (!m_initialized || m_aliveCountReadback == 0) return;

    // Save current state
    ComPtr<ID3D11BlendState> oldBlend;
    FLOAT oldBlendFactor[4];
    UINT oldSampleMask;
    ctx->OMGetBlendState(oldBlend.GetAddressOf(), oldBlendFactor, &oldSampleMask);

    ComPtr<ID3D11DepthStencilState> oldDepthState;
    UINT oldStencilRef;
    ctx->OMGetDepthStencilState(oldDepthState.GetAddressOf(), &oldStencilRef);

    // Set particle render state
    float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(m_blendAlpha.Get(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_depthReadOnly.Get(), 0);

    // Bind shaders (no input layout for billboard)
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_billboardVS.Get(), nullptr, 0);
    ctx->PSSetShader(m_billboardPS.Get(), nullptr, 0);

    // Bind CBPerFrame (b0) to VS+PS
    ctx->VSSetConstantBuffers(0, 1, &cbPerFrame);
    ctx->PSSetConstantBuffers(0, 1, &cbPerFrame);

    // Bind particle pool (t2) and alive list (t3) to VS
    ID3D11ShaderResourceView* vsSRVs[] = { nullptr, nullptr, m_particlePoolSRV.Get(), m_aliveListSRV.Get() };
    ctx->VSSetShaderResources(0, 4, vsSRVs);

    // Bind default white texture to PS (t4) as particle texture
    if (defaultWhiteSRV) {
        ctx->PSSetShaderResources(4, 1, &defaultWhiteSRV);
    }

    // Bind scene depth to PS (t5) for soft-particle fade
    if (depthSRV) {
        ctx->PSSetShaderResources(5, 1, &depthSRV);
    }

    // Draw using indirect args
    ctx->DrawInstancedIndirect(m_drawArgs.Get(), 0);

    // Unbind SRVs
    ID3D11ShaderResourceView* nullSRVs[6] = {};
    ctx->VSSetShaderResources(0, 4, nullSRVs);
    ctx->PSSetShaderResources(4, 2, nullSRVs); // t4 + t5

    // Restore state
    ctx->OMSetBlendState(oldBlend.Get(), oldBlendFactor, oldSampleMask);
    ctx->OMSetDepthStencilState(oldDepthState.Get(), oldStencilRef);
}

// ============================================================
// Hot-reload
// ============================================================
void GPUParticleSystem::HotReload(ID3D11Device* device) {
    if (m_initCS.HasFileChanged())     m_initCS.Reload(device);
    if (m_resetCS.HasFileChanged())    m_resetCS.Reload(device);
    if (m_spawnCS.HasFileChanged())    m_spawnCS.Reload(device);
    if (m_simulateCS.HasFileChanged()) m_simulateCS.Reload(device);
    if (m_buildArgsCS.HasFileChanged()) m_buildArgsCS.Reload(device);
}

} // namespace WT

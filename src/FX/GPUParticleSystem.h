#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include "FX/ComputeShader.h"
#include "FX/ParticleEmitter.h"

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ============================================================
// GPUParticleSystem — AAA GPU-driven particle system
//
//  Pipeline per frame:
//    1. Reset alive counter
//    2. Spawn new particles (consume from dead list)
//    3. Simulate all particles (physics, depth collision, build alive list)
//    4. Build indirect draw args from alive count
//    5. Render billboards via DrawInstancedIndirect
//
//  Buffers:
//    ParticlePool  — RWStructuredBuffer<GPUParticle>[maxParticles]
//    DeadList      — RWStructuredBuffer<uint>[maxParticles]  (free-index stack)
//    AliveList     — RWStructuredBuffer<uint>[maxParticles]  (built each frame)
//    Counters      — RWByteAddressBuffer: [0]=deadCount, [4]=aliveCount
//    DrawArgs      — Indirect draw args buffer (16 bytes)
// ============================================================
class GPUParticleSystem {
public:
    bool Init(ID3D11Device* device, const std::wstring& shaderDir, UINT maxParticles = 65536);
    void Shutdown();

    // ---- Emitter management ----
    int  AddEmitter(const ParticleEmitterDef& def, const XMFLOAT3& pos,
                    float duration = -1.0f);
    void RemoveEmitter(int id);
    void SetEmitterPosition(int id, const XMFLOAT3& pos);
    EmitterInstance* GetEmitter(int id);

    // ---- One-shot burst spawning ----
    void SpawnBurst(const XMFLOAT3& pos, const ParticleEmitterDef& def, int count);
    void SpawnBurst(const XMFLOAT3& pos, const XMFLOAT3& direction,
                    const ParticleEmitterDef& def, int count);

    // ---- Per-frame GPU update ----
    void Update(ID3D11DeviceContext* ctx, float dt,
                ID3D11ShaderResourceView* depthSRV,
                ID3D11Buffer* cbPerFrame);

    // ---- Render all alive billboard particles ----
    void Render(ID3D11DeviceContext* ctx,
                ID3D11Buffer* cbPerFrame,
                ID3D11ShaderResourceView* defaultWhiteSRV,
                ID3D11ShaderResourceView* depthSRV = nullptr);

    // ---- Query ----
    UINT GetMaxParticles() const { return m_maxParticles; }
    UINT GetAliveCountCPU() const { return m_aliveCountReadback; }

    // Hot-reload compute shaders
    void HotReload(ID3D11Device* device);

private:
    bool CreateBuffers(ID3D11Device* device);
    bool CreateShaders(ID3D11Device* device, const std::wstring& shaderDir);
    void DispatchSpawn(ID3D11DeviceContext* ctx, const XMFLOAT3& pos,
                       const XMFLOAT3& direction,
                       const ParticleEmitterDef& def, int count, float seed);

    // GPU buffers
    ComPtr<ID3D11Buffer> m_particlePool;     // RWStructuredBuffer<GPUParticle>
    ComPtr<ID3D11Buffer> m_deadList;         // RWStructuredBuffer<uint>
    ComPtr<ID3D11Buffer> m_aliveList;        // RWStructuredBuffer<uint>
    ComPtr<ID3D11Buffer> m_counters;         // RWByteAddressBuffer
    ComPtr<ID3D11Buffer> m_drawArgs;         // IndirectArgs buffer
    ComPtr<ID3D11Buffer> m_counterStaging;   // For CPU readback of alive count

    // UAVs
    ComPtr<ID3D11UnorderedAccessView> m_particlePoolUAV;
    ComPtr<ID3D11UnorderedAccessView> m_deadListUAV;
    ComPtr<ID3D11UnorderedAccessView> m_aliveListUAV;
    ComPtr<ID3D11UnorderedAccessView> m_countersUAV;
    ComPtr<ID3D11UnorderedAccessView> m_drawArgsUAV;

    // SRVs (for rendering)
    ComPtr<ID3D11ShaderResourceView> m_particlePoolSRV;
    ComPtr<ID3D11ShaderResourceView> m_aliveListSRV;

    // Compute shaders
    ComputeShader m_initCS;
    ComputeShader m_resetCS;
    ComputeShader m_spawnCS;
    ComputeShader m_simulateCS;
    ComputeShader m_buildArgsCS;

    // Billboard render shaders (VS/PS only — no input layout needed)
    ComPtr<ID3D11VertexShader>  m_billboardVS;
    ComPtr<ID3D11PixelShader>   m_billboardPS;

    // Blend states
    ComPtr<ID3D11BlendState>    m_blendAlpha;
    ComPtr<ID3D11BlendState>    m_blendAdditive;
    ComPtr<ID3D11DepthStencilState> m_depthReadOnly; // depth test ON, write OFF

    // Emitter CB (b5)
    ComPtr<ID3D11Buffer> m_cbEmitter;
    // Simulate CB (b6)
    ComPtr<ID3D11Buffer> m_cbSimulate;

    UINT m_maxParticles = 0;
    bool m_initialized  = false;
    UINT m_aliveCountReadback = 0; // CPU-side alive count (1 frame latency)
    float m_frameTime = 0.0f;

    // Active emitters
    std::vector<EmitterInstance> m_emitters;
    int m_nextEmitterId = 1;

    // Pending one-shot bursts
    struct BurstRequest {
        XMFLOAT3 position;
        XMFLOAT3 direction;
        ParticleEmitterDef def;
        int count;
    };
    std::vector<BurstRequest> m_pendingBursts;
};

} // namespace WT

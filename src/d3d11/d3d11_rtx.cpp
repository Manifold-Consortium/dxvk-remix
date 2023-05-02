#include "d3d11_rtx.h"
#include "d3d11_device.h"

namespace dxvk {
  D3D11Rtx::D3D11Rtx(D3D11Device* d3d11Device)
    : m_parent(d3d11Device),
      m_rtStagingData(d3d11Device->m_dxvkDevice,
      (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { }

void D3D11Rtx::Initialize() {
  ID3D11DeviceContext* dctx = nullptr;
  m_parent->GetImmediateContext(&dctx);
  assert(dctx);
  m_dctx = static_cast<D3D11DeviceContext*>(dctx);
  Logger::info("Welcome to D3D11Rtx");
}

void D3D11Rtx::ResetSwapChain(const DXGI_SWAP_CHAIN_DESC1 &presentationParameters) {
  if (0 == memcmp(&m_activePresentParams, &presentationParameters, sizeof(presentationParameters)))
    return;

  // Cache these
  m_activePresentParams = presentationParameters;

  // Inform the backend about potential presenter update
  m_dctx->EmitCs([cWidth  = m_activePresentParams.Width,
                  cHeight = m_activePresentParams.Height](DxvkContext *ctx) {
    static_cast<RtxContext *>(ctx)->resetScreenResolution({cWidth, cHeight, 1});
  });
}

D3D11Rtx::DrawCallType D3D11Rtx::makeDrawCallType(const Draw& drawContext) {
  int currentDrawCallID = m_drawCallID++;
  if (currentDrawCallID < RtxOptions::Get()->getDrawCallRange().x || 
      currentDrawCallID > RtxOptions::Get()->getDrawCallRange().y) {
    return { RtxGeometryStatus::Ignored, false };
  }

  // TODO: Only certain topologies of calls are worth ray tracing
  // TODO: Check if the current render target is primary
  // TODO: Check UI only to the primary render target

  if (drawContext.NumVertices == 0) {
    ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, verticies count was 0."));
    return { RtxGeometryStatus::Ignored, false };
  }

  // We only look at RT 0 currently.
  const uint32_t kRenderTargetIndex = 0;
  if (m_dctx->m_state.om.renderTargetViews[kRenderTargetIndex].ref() == nullptr) {
    ONCE(Logger::info("[RTX-Compatibility-Info] Skipped drawcall, as no color render target bound."));
    return { RtxGeometryStatus::Ignored, false };
  }

  // Force as rasterized for now
  return {
    RtxGeometryStatus::Rasterized,
    false, // UI rendering detected => trigger RTX injection
  };

  return { RtxGeometryStatus::RayTraced, false };
}

void D3D11Rtx::PrepareUnindexedDrawGeometryForRT(const Draw& context) {
  VertexContext vertices[caps::MaxStreams] {0};
  ZeroMemory(vertices, sizeof(vertices));

  ONCE(Logger::info("got vertex slice for PrepareUnindexedDrawGeometryForRT"));

  // Capture all vertices in the vertex buffer before they get sent to the GPU
  D3D11_MAPPED_SUBRESOURCE mappedResource{};
  D3D10DeviceLock lock = m_dctx->LockContext();
  for (uint32_t i = 0; i < caps::MaxStreams; i++) {
    const auto& dx11Vbo = m_dctx->m_state.ia.vertexBuffers[i].buffer.ref();
    if (dx11Vbo == nullptr) break;
    const auto& dx11VboOffset = m_dctx->m_state.ia.vertexBuffers[i].offset;
    const auto& dx11VboStride = m_dctx->m_state.ia.vertexBuffers[i].stride;
#ifdef MF_VBO_DEBUG
    // can't map local-only visibile buffers
    if (dx11Vbo->GetMapMode() != D3D11_COMMON_BUFFER_MAP_MODE_NONE) {
      if (FAILED(m_dctx->Map(dx11Vbo, 0, D3D11_MAP_READ_WRITE, 0, &mappedResource))) {
        Logger::err("failed to map dx11Vbo");
      } else {
        Vertex* pVertices = reinterpret_cast<Vertex*>(mappedResource.pData);
        pVertices->position.x -= std::clamp(pVertices->position.x, 0.0f, 0.2f);
        pVertices->position.y -= std::clamp(pVertices->position.y, 0.0f, 0.1f);
        m_dctx->Unmap(dx11Vbo, 0);
      }
    }
#endif
    vertices[i].offset  = dx11VboOffset;
    vertices[i].stride  = dx11VboStride;
    vertices[i].buffer  = dx11Vbo->GetMappedSlice();
    dx11Vbo->Release();
  }
  internalPrepareDraw(std::nullopt, vertices, context);
  return;
}

void D3D11Rtx::PrepareIndexedDrawGeometryForRT(const Draw& context) {
  ONCE(Logger::info("got vertex slice for PrepareIndexedDrawGeometryForRT"));
  // Indexes and indicies
  ID3D11Buffer *i_indexBuffer = nullptr;
  DXGI_FORMAT   indexFormat   = DXGI_FORMAT_UNKNOWN;
  UINT          indexOffset   = 0;
  IndexContext  indices;

  // Vertexes (offsets and strides)
  VertexContext vertices[caps::MaxStreams] { 0 };
  ZeroMemory(vertices, sizeof(vertices));

  // Capture index buffer
  m_dctx->IAGetIndexBuffer(
    &i_indexBuffer,
    &indexFormat,
    &indexOffset
  );
  assert(i_indexBuffer != nullptr);
  const auto indexBuffer = GetCommonBuffer(static_cast<D3D11Buffer *>(i_indexBuffer));
  assert(indexBuffer != nullptr);
  indices.indexBuffer = indexBuffer->GetMappedSlice();
  indices.indexType   = DecodeIndexType(indexFormat);
 
  // Capture all vertex buffers we care about
  D3D10DeviceLock lock = m_dctx->LockContext();
  for (uint32_t i = 0; (i < caps::MaxStreams); i++) {
    const auto& dx11Vbo = m_dctx->m_state.ia.vertexBuffers[i].buffer.ref();
    if (dx11Vbo == nullptr) break;
    const auto& dx11VboOffset = m_dctx->m_state.ia.vertexBuffers[i].offset;
    const auto& dx11VboStride = m_dctx->m_state.ia.vertexBuffers[i].stride;
    vertices[i].offset = dx11VboOffset;
    vertices[i].stride = dx11VboStride;
    vertices[i].buffer = dx11Vbo->GetMappedSlice();
    dx11Vbo->Release();
  }
  internalPrepareDraw(indices, vertices, context);
  i_indexBuffer->Release();
  return;
}

template<typename T>
void D3D11Rtx::copyIndices(const uint32_t indexCount, T* pIndicesDst, const T* pIndices, uint32_t& minIndex, uint32_t& maxIndex) {
  ZoneScoped;

  assert(indexCount >= 3);

  // Find min/max index
  {
    ZoneScopedN("Find min/max");

    fast::findMinMax<T>(indexCount, pIndices, minIndex, maxIndex);
  }

  // Modify the indices if the min index is non-zero
  {
    ZoneScopedN("Copy indices");

    if (minIndex != 0) {
      fast::copySubtract<T>(pIndicesDst, pIndices, indexCount, (T) minIndex);
    } else {
      memcpy(pIndicesDst, pIndices, sizeof(T) * indexCount);
    }
  }
}

template <typename T>
DxvkBufferSlice
D3D11Rtx::processIndexBuffer(const uint32_t indexCount,
                             const uint32_t startIndex,
                             const DxvkBufferSliceHandle &indexSlice,
                             uint32_t &minIndex, uint32_t &maxIndex)
{
  ZoneScoped;

  const size_t indexStride   = sizeof(T);
  const size_t numIndexBytes = indexCount  * indexStride;
  const size_t indexOffset   = indexStride * startIndex;

  // Get our slice of the staging ring buffer
  const DxvkBufferSlice& stagingSlice = m_rtStagingData.alloc(CACHE_LINE_SIZE, numIndexBytes);

  // Acquire prevents the staging allocator from re-using this memory
  stagingSlice.buffer()->acquire(DxvkAccess::Read);

  const uint8_t* pBaseIndex = (uint8_t*) indexSlice.mapPtr + indexOffset;

  T* pIndices = (T*) pBaseIndex;
  T* pIndicesDst = (T*) stagingSlice.mapPtr(0);
  copyIndices<T>(indexCount, pIndicesDst, pIndices, minIndex, maxIndex);

  return stagingSlice;
}

void D3D11Rtx::internalPrepareDraw(
  std::optional<const IndexContext> indexContext,
  const VertexContext vertexContext[caps::MaxStreams],
  const Draw& drawContext)
{
  ZoneScoped;

  // RTX was injected => treat everything else as rasterized 
  if (m_rtxInjectTriggered) {
    return;
  }

  auto [status, triggerRtxInjection] = makeDrawCallType(drawContext);

  if (status == RtxGeometryStatus::Ignored) {
    return;
  } else if (triggerRtxInjection) {
    m_dctx->EmitCs([](DxvkContext* ctx) {
      static_cast<RtxContext*>(ctx)->injectRTX();
    });
    m_rtxInjectTriggered = true;
    return;
  }

  D3D11_RASTERIZER_DESC  pDesc;
  auto cullMode = D3D11_CULL_NONE;

  // Do we even have a rasterizer yet?
  // When we do, update the culling mode used
  if (m_dctx->m_state.rs.state != nullptr) {
    m_dctx->m_state.rs.state->GetDesc(&pDesc);
    cullMode = pDesc.CullMode;
  }

  // Grab current vertex topology
  D3D_PRIMITIVE_TOPOLOGY pTop = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  m_dctx->IAGetPrimitiveTopology(&pTop);

  // The packet we'll send to RtxContext with information about geometry
  RasterGeometry geoData;
  geoData.cullMode  = DecodeCullMode(cullMode);
  geoData.frontFace = VK_FRONT_FACE_CLOCKWISE;
  geoData.topology  = DecodeTopology(pTop);

  int vertexIndexOffset = drawContext.BaseVertexIndex;
  uint32_t minIndex = 0, maxIndex = 0;

  // Process index buffer
  if (indexContext.has_value()) {
    if (indexContext.value().indexType != VK_INDEX_TYPE_NONE_KHR) {
      // XXX: we are only targetting triangles right now
      geoData.indexCount = drawContext.NumVertices;

      if (indexContext.value().indexType == VK_INDEX_TYPE_UINT16)
        geoData.indexBuffer = RasterBuffer(
            processIndexBuffer<uint16_t>(
                geoData.indexCount, drawContext.StartIndex,
                indexContext.value().indexBuffer, minIndex, maxIndex), 0, 2,
                indexContext.value().indexType);
      else
        geoData.indexBuffer = RasterBuffer(
            processIndexBuffer<uint32_t>(
                geoData.indexCount, drawContext.StartIndex,
                indexContext.value().indexBuffer, minIndex, maxIndex), 0, 4,
                indexContext.value().indexType);

      // Unlikely, but invalid
      if (maxIndex == minIndex) {
        Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, no triangles detected in index buffer.");
        return;
      }

      geoData.vertexCount = maxIndex - minIndex + 1;
      vertexIndexOffset  += minIndex;
    } else {
      // TODO: map the topology type for the target to the verticies to vertex calculation
      geoData.vertexCount = drawContext.NumVertices;
    }
  }

  if (geoData.vertexCount == 0) {
    ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, no vertices detected."));
    return;
  }

  // Fetch all the legacy state (colour modes, alpha test, etc...)
  // const DxvkRtxLegacyState legacyState = createLegacyState(m_parent);

  // Fetch all the render state and send it to rtx context (textures, transforms, etc.)
  const uint32_t idealTexcoordIndex = processRenderState();

  // Copy all the vertices into a staging buffer.  Assign fields of the geoData structure.
  // processVertices(vertexContext, vertexIndexOffset, idealTexcoordIndex, geoData);
  
  // Calculate given XXHashes for current geometry
  // geoData.futureGeometryHashes = computeHash(geoData, (maxIndex - minIndex));
  // geoData.futureGeometryHashes = std::shared_future<GeometryHashes>();
  
  // Process and compute hashes for skinning data
  // std::shared_future<SkinningData> futureSkinningData = processSkinning(geoData);

  // if (RtxOptions::Get()->calculateMeshBoundingBox()) {
  //   geoData.futureBoundingBox = computeAxisAlignedBoundingBox(geoData);
  // }
  // geoData.futureBoundingBox = std::shared_future<AxisAlignBoundingBox>();

  // Send it
  m_dctx->EmitCs([geoData](DxvkContext* ctx) {
    assert(static_cast<RtxContext*>(ctx));
    RtxContext* rtxCtx = static_cast<RtxContext*>(ctx);
    rtxCtx->setShaderState(true, true);
    // rtxCtx->setLegacyState(legacyState);
    // rtxCtx->setGeometry(geoData, RtxGeometryStatus::Rasterized); // XXX
    // rtxCtx->setSkinningData(futureSkinningData);
  });
}

uint32_t D3D11Rtx::processTextures() {
  // We don't support full legacy materials in fixed function mode yet..
  // This implementation finds the most relevant textures bound from the
  // following criteria:
  //   - Texture actually bound (and used) by stage
  //   - First N textures bound to a specific texcoord index
  //   - Prefer lowest texcoord index
  // In non-fixed function (shaders), take the first N textures.

  // Currently we only support 2 textures
  constexpr uint32_t kMaxTextureBindings = 2;
  constexpr uint32_t NumTexcoordBins = kMaxTextureBindings;

  // Build a mapping of texcoord indices to stage
  constexpr uint8_t kInvalidStage = 0xFF;
  uint8_t texcoordIndexToStage[NumTexcoordBins] {};

  // Find the ideal textures for raytracing, initialize the data to invalid (out of range) to unbind unused textures
  uint32_t texSlotsForRT[kMaxTextureBindings] {};
  memset(&texSlotsForRT[0], 0xFFFFffff, sizeof(texSlotsForRT));

  // Create a new render target view for each texture that we want to capture.
  ID3D11RenderTargetView* renderTargetViews[1] = { nullptr };
  m_dctx->OMGetRenderTargets(1, renderTargetViews, nullptr);

#ifdef SRV
  // Create a new shader resource view for each texture that we want to capture.
  ID3D11Resource* resource = nullptr;
  ID3D11ShaderResourceView* shaderResourceViews[1] = { nullptr };
  renderTargetViews[0]->GetResource(&resource);
  m_parent->CreateShaderResourceView(resource, nullptr, &shaderResourceViews[0]);
  resource->Release();

  // XXX: Do we even need a shader resource if we can 
  if (shaderResourceViews[0] != nullptr)
    // Bind the shader resource views to the appropriate texture slots in your shader.
    // XXX
    m_dctx->PSSetShaderResources(0, 1, shaderResourceViews);
#endif

  // Set the render target views and shader resource views to the appropriate slots in the pipeline.
  ID3D11UnorderedAccessView* uavs[1] = { nullptr };
  m_dctx->OMSetRenderTargetsAndUnorderedAccessViews(1, renderTargetViews, nullptr, 0, 1, uavs, nullptr);

  // Create a staging texture.
  ID3D11Texture2D* stagingTexture = nullptr;
  D3D11_TEXTURE2D_DESC desc;
  renderTargetViews[0]->GetResource((ID3D11Resource**)&stagingTexture);
  stagingTexture->GetDesc(&desc);
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.MiscFlags = 0;
  ID3D11Texture2D* texture;
  m_parent->CreateTexture2D(&desc, nullptr, &texture);

  // Copy the texture to the staging texture.
  m_dctx->CopyResource(texture, stagingTexture);

  // Map the staging texture and read its contents.
  D3D11_MAPPED_SUBRESOURCE mappedResource;
  m_dctx->Map(texture, 0, D3D11_MAP_READ, 0, &mappedResource);
  // TODO: Happy fun time here
  m_dctx->Unmap(texture, 0);

  // Release the resources.
  texture->Release();
  stagingTexture->Release();
  renderTargetViews[0]->Release();
#ifdef SRV
  if (shaderResourceViews[0] != nullptr)
    shaderResourceViews[0]->Release();
#endif

  return 0;
}

uint32_t D3D11Rtx::processRenderState() {
  return processTextures();
}

void D3D11Rtx::EndFrame() {
  // Inform backend of end-frame
  m_dctx->EmitCs([](DxvkContext* ctx) {
    static_cast<RtxContext*>(ctx)->endFrame();
  });
  // Reset for the next frame
  m_rtxInjectTriggered = false;
  m_drawCallID = 0;
}
} // namespace dxvk

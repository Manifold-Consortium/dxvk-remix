#pragma once

#include "../dxvk/dxvk_buffer.h"
#include "../util/util_flags.h"
#include "../util/util_threadpool.h"
#include "d3d11_rtx_caps.h"
#include "d3d11_context.h"

#include <vector>
#include <optional>

namespace dxvk {
struct D3D11BufferSlice;
class  DxvkDevice;

enum class D3D11RtxFlag : uint32_t {
  DirtyObjectTransform,
  DirtyCameraTransforms,
  DirtyLights,
  DirtyClipPlanes,
};
using D3D11RtxFlags = Flags<D3D11RtxFlag>;

// This class handles all of the RTX operations that are required from the D3D11 side.
struct D3D11Rtx {

  struct Draw {
    INT   BaseVertexIndex;
    UINT  MinVertexIndex;
    UINT  NumVertices;
    UINT  StartIndex;
  };
  struct DrawCallType {
    RtxGeometryStatus status;
    bool triggerRtxInjection;
  };
  struct IndexContext {
    VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
    DxvkBufferSliceHandle indexBuffer;
  };
  struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
  };
  struct VertexContext {
    uint32_t stride = 0;
    uint32_t offset = 0;
    DxvkBufferSliceHandle buffer;
  };
  // Give threads specific tasks, to reduce the chance of
  // critical work being pre-empted.
  enum class WorkerTasks : uint8_t {
    kSkinningThread = 1 << 0,

    kHashingThread0 = 1 << 1,
    kHashingThread1 = 1 << 2,
    kHashingThread2 = 1 << 3,

    kHashingThreads = (kHashingThread0 | kHashingThread1 | kHashingThread2),
    kAllThreads = (kHashingThreads | kSkinningThread)
  };

  /**
   * @brief D3D11Rtx Object
   * @param d3d11Device 
  */
  D3D11Rtx(D3D11Device *d3d11Device);

  /**
   * @brief: Initialize the D3D11 RTX interface
   */
  void Initialize();

  /**
   * @brief: Signal that we've reached the end of the frame.
   */
  void EndFrame();

  /**
   * @brief: check our drawcontext against calls we support and output the type context
   * @param drawContext 
   * @return DrawCallType
  */
  DrawCallType makeDrawCallType(const Draw& drawContext);

  /**
   * @brief Experimental: prepare some unindexed vertexes for capture to the RT pipeline
   * @param context 
  */
  void PrepareUnindexedDrawGeometryForRT(const Draw& context);

  /**
   * @brief Experimental: prepare some indexed vertexes for capture to the RT pipeline
   * @param context 
  */
  void PrepareIndexedDrawGeometryForRT(const Draw& context);

  /**
   * @brief Experimental: prepare captures indicies and verticies for RT draw
   * @param indexContext 
   * @param vertexContext 
   * @param drawContext 
  */
  void internalPrepareDraw(
    std::optional<const IndexContext> indexContext,
    const VertexContext vertexContext[caps::MaxStreams],
    const Draw& drawContext);

  /**
   * @brief copy indices buffers
   * @tparam T 
   * @param indexCount 
   * @param pIndicesDst 
   * @param pIndices 
   * @param minIndex 
   * @param maxIndex 
  */
  template<typename T>
  void copyIndices(const uint32_t indexCount, T* pIndicesDst, const T* pIndices, uint32_t& minIndex, uint32_t& maxIndex);

  /**
   * @brief
   * @tparam T 
   * @param  indexCount 
   * @param  startIndex 
   * @param  indexSlice 
   * @param  minIndex 
   * @param  maxIndex 
   * @return DxvkBufferSlice
  */
  template <typename T>
  DxvkBufferSlice processIndexBuffer(const uint32_t indexCount,
                                     const uint32_t startIndex,
                                     const DxvkBufferSliceHandle &indexSlice,
                                     uint32_t &minIndex, uint32_t &maxIndex);

  /**
   * @brief 
  */
  uint32_t processTextures();
  
  /**
   * @brief 
  */
  uint32_t processRenderState();

  /**
   * @brief: Signal that a parameter needs to be updated for RTX
   *
   * @param [in] flag: parameter that requires updating
   */
  void SetDirty(D3D11RtxFlag flag) { m_flags.set(flag); }

  /**
   * @brief: Signal that a swapchain has been resized or reconfigured.
   *
   * @param [in] presentationParameters: A reference to the D3D present params.
   */
  void ResetSwapChain(const DXGI_SWAP_CHAIN_DESC1 &presentationParameters);

  // TODO
  bool isRenderingUI() { };

  // WorkerThreadPool<4 * 1024, true, false> m_gpeWorkers;
  DxvkStagingDataAlloc  m_rtStagingData;
  D3D11Device          *m_parent;
  D3D11DeviceContext   *m_dctx;
  DXGI_SWAP_CHAIN_DESC1 m_activePresentParams;
  D3D11RtxFlags         m_flags;
  uint32_t              m_drawCallID = 0;

  std::atomic_bool m_rtxInjectTriggered = false;

  /* TODO
  const Direct3DState9& d3d11State() const;

  void prepareVertexCapture(const int vertexIndexOffset, const uint32_t
  vertexCount);

  void processVertices(const VertexContext vertexContext[caps::MaxStreams], int
  vertexIndexOffset, uint32_t idealTexcoordIndex, RasterGeometry& geoData);
  */
};
} // namespace dxvk

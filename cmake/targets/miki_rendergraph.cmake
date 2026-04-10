# =============================================================================
# miki_rendergraph — Render Graph core (Builder, Compiler, Executor)
# =============================================================================

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/include/miki/rendergraph/RenderGraphTypes.h")
    return()
endif()

if(NOT TARGET miki_rhi)
    message(WARNING "[miki] miki_rendergraph requires miki_rhi")
    return()
endif()

set(MIKI_RENDERGRAPH_SOURCES
    src/miki/rendergraph/RenderGraphBuilder.cpp
    src/miki/rendergraph/PassBuilder.cpp
    src/miki/rendergraph/RenderGraphCompiler.cpp
    src/miki/rendergraph/RenderGraphExecutor.cpp
    src/miki/rendergraph/BarrierEmitter.cpp
    src/miki/rendergraph/TransientResourceAllocator.cpp
    src/miki/rendergraph/PassRecorder.cpp
    src/miki/rendergraph/BatchSubmitter.cpp
    src/miki/rendergraph/AsyncComputeScheduler.cpp
    src/miki/rendergraph/BarrierSynthesizer.cpp
    src/miki/rendergraph/PassReorderer.cpp
    src/miki/rendergraph/PassMerger.cpp
    src/miki/rendergraph/RenderGraphSerializer.cpp
    src/miki/rendergraph/TransientHeapPool.cpp
    src/miki/rendergraph/FrameOrchestrator.cpp
    src/miki/rendergraph/RenderGraphDebug.cpp
)

add_library(miki_rendergraph OBJECT ${MIKI_RENDERGRAPH_SOURCES})
target_link_libraries(miki_rendergraph PUBLIC miki_rhi)
target_link_libraries(miki_rendergraph PUBLIC miki::third_party::nlohmann_json)
miki_setup_library(miki_rendergraph)

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
    src/miki/rendergraph/RenderGraphSerializer.cpp
)

add_library(miki_rendergraph OBJECT ${MIKI_RENDERGRAPH_SOURCES})
target_link_libraries(miki_rendergraph PUBLIC miki_rhi)
target_link_libraries(miki_rendergraph PRIVATE miki::third_party::nlohmann_json)
miki_setup_library(miki_rendergraph)

// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>

#include "video_core/pica.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"

class RasterizerCacheOpenGL : NonCopyable {
public:
    ~RasterizerCacheOpenGL();

    /// Loads a texture from 3DS memory to OpenGL and caches it (if not already cached)
    void LoadAndBindTexture(OpenGLState &state, unsigned texture_unit, const Pica::DebugUtils::TextureInfo& info);

    void LoadAndBindTexture(OpenGLState &state, unsigned texture_unit, const Pica::Regs::FullTextureConfig& config) {
        LoadAndBindTexture(state, texture_unit, Pica::DebugUtils::TextureInfo::FromPicaRegister(config.config, config.format));
    }

    /// Flush any cached resource that touches the flushed region
    void NotifyFlush(PAddr addr, u32 size, bool ignore_hash = false);

    /// Flush all cached OpenGL resources tracked by this cache manager
    void FullFlush();

private:
    struct CachedTexture {
        OGLTexture texture;
        GLuint width;
        GLuint height;
        u32 size;
        u64 hash;
        PAddr addr;
    };

    std::map<PAddr, std::unique_ptr<CachedTexture>> texture_cache;
};

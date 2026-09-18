#include "RenderTarget.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <cstdio>

namespace Potato {

RenderTarget::~RenderTarget() {
    Destroy();
}

bool RenderTarget::Create(int w, int h) {
    if (w <= 0 || h <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "RenderTarget::Create 非法尺寸 %dx%d", w, h);
        LOG_ERROR(msg);
        return false;
    }
    Destroy(); // 重入安全：resize 直接 Create 新尺寸

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, colorTex, 0);

    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depthRb);

    const bool ok =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "RenderTarget::Create FBO 不完整 %dx%d", w, h);
        LOG_ERROR(msg);
        Destroy();
        return false;
    }
    width = w;
    height = h;
    return true;
}

void RenderTarget::Destroy() {
    if (depthRb) {
        glDeleteRenderbuffers(1, &depthRb);
        depthRb = 0;
    }
    if (colorTex) {
        glDeleteTextures(1, &colorTex);
        colorTex = 0;
    }
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    width = height = 0;
}

void RenderTarget::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void RenderTarget::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RenderTarget::ReadColor(std::vector<unsigned char>& outRGBA) const {
    if (!IsValid()) return false;
    outRGBA.resize((size_t)width * height * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                 outRGBA.data());
    return true;
}

bool RenderTarget::ReadDepth(std::vector<float>& outDepth) const {
    if (!IsValid()) return false;
    outDepth.resize((size_t)width * height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT,
                 outDepth.data());
    return true;
}

} // namespace Potato

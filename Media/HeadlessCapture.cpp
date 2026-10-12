#include "HeadlessCapture.h"

#include "Rendering/ImageCodec.h"

#include <glad/glad.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <cstring>

namespace Potato {
namespace Media {

// 骨架說明：本檔含 GL，僅由需要 GL 的 exe target 連結（AD-1）。
// 無 GL 環境時 Begin() 乾淨回 false，不會崩潰。

HeadlessCapture::~HeadlessCapture() { End(); }

bool HeadlessCapture::Begin(int width, int height) {
    if (ready_ || width <= 0 || height <= 0) return false;

    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window_ = glfwCreateWindow(width, height, "capture", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }
    GLFWwindow* win = static_cast<GLFWwindow*>(window_);
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(win);
        glfwTerminate();
        window_ = nullptr;
        return false;
    }

    // FBO + RGBA8 color texture（讀回目標）
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex_, 0);
    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        End();
        return false;
    }

    width_ = width;
    height_ = height;
    ready_ = true;
    return true;
}

std::vector<uint8_t>
HeadlessCapture::Grab(const std::function<bool()>& draw) {
    std::vector<uint8_t> empty;
    if (!ready_) return empty;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
    if (!draw || !draw()) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return empty;
    }

    // glReadPixels 是 bottom-up；翻成 top-down 才出檔（AD-3 單一語意）
    std::vector<uint8_t> raw(static_cast<size_t>(width_) * height_ * 4);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                 raw.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const size_t rowBytes = static_cast<size_t>(width_) * 4;
    std::vector<uint8_t> out(raw.size());
    for (int y = 0; y < height_; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * rowBytes,
                    raw.data() +
                        static_cast<size_t>(height_ - 1 - y) * rowBytes,
                    rowBytes);
    }
    return out;
}

bool HeadlessCapture::GrabToFile(const std::function<bool()>& draw,
                                 const std::string& pngPath) {
    const std::vector<uint8_t> px = Grab(draw);
    if (px.empty()) return false;
    return ImageCodec::WritePNGFile(pngPath, width_, height_, px.data());
}

void HeadlessCapture::End() {
    if (window_) {
        GLFWwindow* win = static_cast<GLFWwindow*>(window_);
        glfwMakeContextCurrent(win);
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (colorTex_) glDeleteTextures(1, &colorTex_);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(win);
        window_ = nullptr;
    }
    fbo_ = 0;
    colorTex_ = 0;
    ready_ = false;
    width_ = height_ = 0;
    glfwTerminate();
}

} // namespace Media
} // namespace Potato

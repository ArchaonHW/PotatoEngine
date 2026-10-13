package com.potato.agent.service;

import org.springframework.stereotype.Service;

import com.potato.agent.jni.NativeAgent;
import com.potato.agent.jni.NativeRender;

/**
 * Agent 繪圖——全域唯一 headless RenderingServer。
 * agent 經 render_* 工具組裝 2D canvas / 3D 場景並 draw_frame。
 */
@Service
public class RenderService {

    public int init() {
        NativeJsonSupport.requireNative();
        int count = NativeRender.nInit();
        if (count < 0) {
            throw new IllegalStateException("render server 初始化失敗");
        }
        return count;
    }

    public void shutdown() {
        if (NativeAgent.isLoaded()) {
            NativeRender.nShutdown();
        }
    }

    public String invoke(String tool, Object params) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(
                NativeRender.nInvoke(tool,
                        NativeJsonSupport.toJson(params)));
    }

    public String listTools() {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(NativeRender.nListTools());
    }

    public String stats() {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(NativeRender.nStats());
    }
}

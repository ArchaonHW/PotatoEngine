package com.potato.agent.jni;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import jakarta.annotation.PostConstruct;

/**
 * 啟動時定位並載入 PotatoAgentJni.dll。
 * 搜尋順序：potato.jni.dll 屬性（POTATO_JNI_DLL 環變數可覆寫）
 * → 工作目錄相對的 build-ninja/build-mingw 輸出。
 * 找不到只記警告——服務仍啟動，原生操作回 503。
 */
@Component
public class NativeLibLoader {

    private static final Logger log = LoggerFactory.getLogger(NativeLibLoader.class);

    @Value("${potato.jni.dll:}")
    private String configuredPath;

    @PostConstruct
    void load() {
        if (NativeAgent.isLoaded()) {
            return;
        }
        List<Path> candidates = new ArrayList<>();
        if (configuredPath != null && !configuredPath.isBlank()) {
            candidates.add(Path.of(configuredPath));
        }
        Path cwd = Path.of("").toAbsolutePath();
        for (String build : new String[] {"build-ninja", "build-mingw", "build-mingw-ascii", "build-msvc", "build"}) {
            for (String name : new String[] {"PotatoAgentJni.dll", "libPotatoAgentJni.dll"}) {
                candidates.add(cwd.resolve(build + "/bin/" + name));
                candidates.add(cwd.resolve(build + "/" + name));
                candidates.add(cwd.resolve("../" + build + "/bin/" + name));
                candidates.add(cwd.resolve("../" + build + "/" + name));
                candidates.add(cwd.resolve("../../" + build + "/bin/" + name));
                candidates.add(cwd.resolve("../../" + build + "/" + name));
            }
        }
        for (Path p : candidates) {
            if (!Files.isRegularFile(p)) {
                continue;
            }
            try {
                NativeAgent.load(p.toAbsolutePath().normalize().toString());
                log.info("PotatoAgentJni 載入成功: {}", p);
                return;
            } catch (UnsatisfiedLinkError e) {
                log.warn("載入 {} 失敗: {}", p, e.getMessage());
            }
        }
        log.warn("找不到 PotatoAgentJni.dll——原生 agent 功能停用；"
                + "請先以 CMake 建置或用 -Dpotato.jni.dll / POTATO_JNI_DLL 指定路徑");
    }
}

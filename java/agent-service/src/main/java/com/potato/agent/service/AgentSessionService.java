package com.potato.agent.service;

import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

import org.springframework.stereotype.Service;

import com.potato.agent.jni.NativeAgent;

/**
 * JNI session 生命週期管理。
 * 每個 session 對應一個原生 handle；同 handle 的所有呼叫經
 * synchronized(lock) 序列化（原生端非執行緒安全）。
 */
@Service
public class AgentSessionService {

    public record StepResult(double x, double y, double rawReward,
                             double shapedReward, boolean done, int action,
                             double[] values) {
    }

    public record GridInfo(int width, int height, int goalX, int goalY,
                           int[][] obstacles) {
    }

    public record SessionInfo(String id, String algo, int gridSize,
                              boolean randMaps, long seed) {
    }

    private static final class Entry {
        final long handle;
        final String algo;
        final int gridSize;
        final boolean randMaps;
        final long seed;
        final Object lock = new Object();

        Entry(long handle, String algo, int gridSize, boolean randMaps, long seed) {
            this.handle = handle;
            this.algo = algo;
            this.gridSize = gridSize;
            this.randMaps = randMaps;
            this.seed = seed;
        }
    }

    private final Map<String, Entry> sessions = new ConcurrentHashMap<>();

    public boolean nativeReady() {
        return NativeAgent.isLoaded();
    }

    public int sessionCount() {
        return sessions.size();
    }

    public SessionInfo create(String algo, int gridSize, boolean randMaps, long seed) {
        requireNative();
        long h = NativeAgent.nCreate(algo, gridSize, randMaps, seed);
        if (h <= 0) {
            throw new IllegalArgumentException("原生 session 建立失敗（碼 " + h
                    + "）：確認 algo/gridSize，randMaps 不支援 qlearning");
        }
        String id = UUID.randomUUID().toString();
        sessions.put(id, new Entry(h, algo, gridSize, randMaps, seed));
        return new SessionInfo(id, algo, gridSize, randMaps, seed);
    }

    public void destroy(String id) {
        Entry e = sessions.remove(id);
        if (e != null) {
            synchronized (e.lock) {
                NativeAgent.nDestroy(e.handle);
            }
        }
    }

    public int[] reset(String id) {
        Entry e = entry(id);
        synchronized (e.lock) {
            return NativeAgent.nReset(e.handle);
        }
    }

    public GridInfo grid(String id) {
        Entry e = entry(id);
        synchronized (e.lock) {
            double[] d = NativeAgent.nGrid(e.handle);
            int w = (int) d[0];
            int h = (int) d[1];
            int n = (int) d[4];
            int[][] obs = new int[n][2];
            for (int i = 0; i < n; i++) {
                obs[i][0] = (int) d[5 + i * 2];
                obs[i][1] = (int) d[6 + i * 2];
            }
            return new GridInfo(w, h, (int) d[2], (int) d[3], obs);
        }
    }

    public StepResult stepAuto(String id, boolean explore, boolean learn) {
        Entry e = entry(id);
        synchronized (e.lock) {
            double[] d = NativeAgent.nStepAuto(e.handle, explore, learn);
            double[] values = {d[6], d[7], d[8], d[9]};
            return new StepResult(d[0], d[1], d[2], d[3], d[4] > 0.5,
                    (int) d[5], values);
        }
    }

    public double[] actionValues(String id) {
        Entry e = entry(id);
        synchronized (e.lock) {
            return NativeAgent.nActionValues(e.handle);
        }
    }

    public void endEpisode(String id, boolean learn) {
        Entry e = entry(id);
        synchronized (e.lock) {
            NativeAgent.nEndEpisode(e.handle, learn);
        }
    }

    public double explorationRate(String id) {
        Entry e = entry(id);
        synchronized (e.lock) {
            return NativeAgent.nGetExplorationRate(e.handle);
        }
    }

    public void setExplorationRate(String id, double rate) {
        Entry e = entry(id);
        synchronized (e.lock) {
            NativeAgent.nSetExplorationRate(e.handle, rate);
        }
    }

    public boolean saveModel(String id, String path) {
        Entry e = entry(id);
        synchronized (e.lock) {
            return NativeAgent.nSaveModel(e.handle, path);
        }
    }

    public boolean loadModel(String id, String path) {
        Entry e = entry(id);
        synchronized (e.lock) {
            return NativeAgent.nLoadModel(e.handle, path);
        }
    }

    public SessionInfo info(String id) {
        Entry e = entry(id);
        return new SessionInfo(id, e.algo, e.gridSize, e.randMaps, e.seed);
    }

    private Entry entry(String id) {
        Entry e = sessions.get(id);
        if (e == null) {
            throw new IllegalArgumentException("session 不存在: " + id);
        }
        return e;
    }

    private static void requireNative() {
        if (!NativeAgent.isLoaded()) {
            throw new IllegalStateException("PotatoAgentJni.dll 未載入");
        }
    }
}

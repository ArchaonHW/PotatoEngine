package com.potato.agent.web;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.potato.agent.service.ChainService;

/**
 * /ws/chain——AgentChain 執行進度推播。
 *
 * 指令（client→server, JSON text）：
 *   {"cmd":"run","spec":{...同 POST /api/chain/run...},"pollMs":50}
 *   {"cmd":"stop"}  （停止推播,原生端執行緒會自行跑完回收）
 *
 * 事件（server→client）：
 *   {"type":"start","execId":N}
 *   {"type":"node","node":"id","output":"..."}   （原生端逐節點回呼）
 *   {"type":"done","result":{...ChainResult...}}
 *   {"type":"error","message":"..."}
 */
public class ChainSocketHandler extends TextWebSocketHandler {

    private static final Logger log =
            LoggerFactory.getLogger(ChainSocketHandler.class);

    private final ChainService svc;
    private final ObjectMapper om = new ObjectMapper();
    private final ExecutorService pool = Executors.newCachedThreadPool();

    // 每條連線各自的停止旗標（sessionId → flag）——避免共用旗標
    // 讓一個 client 的 stop/close 停掉所有連線的推播
    private final ConcurrentHashMap<String, AtomicBoolean> stopFlags =
            new ConcurrentHashMap<>();

    public ChainSocketHandler(ChainService svc) {
        this.svc = svc;
    }

    @Override
    protected void handleTextMessage(WebSocketSession session,
                                     TextMessage message) {
        JsonNode cmd;
        try {
            cmd = om.readTree(message.getPayload());
        } catch (Exception e) {
            send(session, error("JSON 解析失敗: " + e.getMessage()));
            return;
        }
        switch (cmd.path("cmd").asText("")) {
            case "run" -> startRun(session, cmd);
            case "stop" -> flag(session).set(true);
            default -> send(session, error("未知指令"));
        }
    }

    private void startRun(WebSocketSession session, JsonNode cmd) {
        JsonNode spec = cmd.get("spec");
        if (spec == null || !spec.isObject()) {
            send(session, error("需帶 spec（同 POST /api/chain/run）"));
            return;
        }
        long pollMs = Math.max(20, cmd.path("pollMs").asLong(50));
        AtomicBoolean stop = flag(session);
        stop.set(false);
        pool.submit(() -> runLoop(session, spec, pollMs, stop));
    }

    private AtomicBoolean flag(WebSocketSession session) {
        return stopFlags.computeIfAbsent(
                session.getId(), k -> new AtomicBoolean());
    }

    private void runLoop(WebSocketSession session, JsonNode spec,
                         long pollMs, AtomicBoolean stop) {
        try {
            JsonNode started = om.readTree(svc.runAsync(spec));
            if (!started.path("success").asBoolean(false)) {
                send(session, error(
                        started.path("error").asText("async 啟動失敗")));
                return;
            }
            long execId = started.path("execId").asLong();
            ObjectNode st = om.createObjectNode();
            st.put("type", "start");
            st.put("execId", execId);
            send(session, st);

            while (!stop.get()) {
                JsonNode poll = om.readTree(svc.poll(execId));
                for (JsonNode ev : poll.path("events")) {
                    ObjectNode n = om.createObjectNode();
                    n.put("type", "node");
                    n.put("node", ev.path("node").asText());
                    n.put("output", ev.path("output").asText());
                    send(session, n);
                }
                if (poll.path("done").asBoolean(false)) {
                    ObjectNode d = om.createObjectNode();
                    d.put("type", "done");
                    d.set("result", poll.get("result"));
                    send(session, d);
                    return;
                }
                Thread.sleep(pollMs);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            log.warn("chain run loop 錯誤", e);
            send(session, error("run 失敗: " + e.getMessage()));
        }
    }

    private ObjectNode error(String msg) {
        ObjectNode m = om.createObjectNode();
        m.put("type", "error");
        m.put("message", msg);
        return m;
    }

    private void send(WebSocketSession session, JsonNode msg) {
        try {
            synchronized (session) {
                if (session.isOpen()) {
                    session.sendMessage(
                            new TextMessage(om.writeValueAsString(msg)));
                }
            }
        } catch (IOException e) {
            log.debug("WS 推播失敗: {}", e.getMessage());
        }
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session,
                                      CloseStatus status) {
        flag(session).set(true);
        stopFlags.remove(session.getId());
    }
}

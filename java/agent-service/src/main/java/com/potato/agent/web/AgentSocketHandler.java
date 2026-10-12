package com.potato.agent.web;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.potato.agent.service.AgentSessionService;
import com.potato.agent.service.AgentSessionService.GridInfo;
import com.potato.agent.service.AgentSessionService.StepResult;

/**
 * /ws/agent 即時走位推播。
 *
 * 指令（client→server, JSON text）：
 *   {"cmd":"run","sessionId":"..","episodes":50,"maxSteps":100,
 *    "learn":true,"explore":true,"delayMs":80}
 *   {"cmd":"stop"}
 *
 * 事件（server→client）：
 *   runStart / grid（rand 模式每次 reset 後）/ step / episodeEnd / runEnd / error
 */
public class AgentSocketHandler extends TextWebSocketHandler {

    private static final Logger log = LoggerFactory.getLogger(AgentSocketHandler.class);
    private static final String[] ACTION_NAMES = {"up", "down", "left", "right"};

    private final AgentSessionService svc;
    private final ObjectMapper om = new ObjectMapper();
    private final ExecutorService pool = Executors.newCachedThreadPool();

    private volatile boolean stopRequested = false;

    public AgentSocketHandler(AgentSessionService svc) {
        this.svc = svc;
    }

    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) {
        JsonNode cmd;
        try {
            cmd = om.readTree(message.getPayload());
        } catch (Exception e) {
            send(session, error("JSON 解析失敗: " + e.getMessage()));
            return;
        }
        String type = cmd.path("cmd").asText("");
        switch (type) {
            case "run" -> startRun(session, cmd);
            case "stop" -> stopRequested = true;
            default -> send(session, error("未知指令: " + type));
        }
    }

    private void startRun(WebSocketSession session, JsonNode cmd) {
        String sessionId = cmd.path("sessionId").asText("");
        int episodes = Math.max(1, cmd.path("episodes").asInt(50));
        int maxSteps = Math.max(1, cmd.path("maxSteps").asInt(100));
        boolean learn = cmd.path("learn").asBoolean(true);
        boolean explore = cmd.path("explore").asBoolean(true);
        long delayMs = Math.max(0, cmd.path("delayMs").asLong(80));

        if (!svc.nativeReady()) {
            send(session, error("PotatoAgentJni.dll 未載入"));
            return;
        }
        pool.submit(() -> runLoop(session, sessionId, episodes, maxSteps,
                learn, explore, delayMs));
    }

    private void runLoop(WebSocketSession session, String sessionId,
                         int episodes, int maxSteps, boolean learn,
                         boolean explore, long delayMs) {
        stopRequested = false;
        int successes = 0;
        double rewardSum = 0;
        try {
            ObjectNode start = om.createObjectNode();
            start.put("type", "runStart");
            start.put("episodes", episodes);
            start.put("learn", learn);
            send(session, start);

            for (int ep = 0; ep < episodes && !stopRequested; ep++) {
                int[] pos = svc.reset(sessionId);
                send(session, gridMsg(sessionId, pos));

                double epReward = 0;
                int steps = 0;
                boolean done = false;
                for (int st = 0; st < maxSteps && !stopRequested; st++) {
                    StepResult r = svc.stepAuto(sessionId, explore, learn);
                    epReward += r.shapedReward();
                    steps++;
                    done = r.done();
                    send(session, stepMsg(ep, st, r));
                    if (delayMs > 0) {
                        Thread.sleep(delayMs);
                    }
                    if (done) {
                        break;
                    }
                }
                svc.endEpisode(sessionId, learn);
                if (done) {
                    successes++;
                }
                rewardSum += epReward;

                ObjectNode ee = om.createObjectNode();
                ee.put("type", "episodeEnd");
                ee.put("ep", ep);
                ee.put("reward", epReward);
                ee.put("steps", steps);
                ee.put("success", done);
                ee.put("epsilon", svc.explorationRate(sessionId));
                send(session, ee);
            }

            ObjectNode end = om.createObjectNode();
            end.put("type", "runEnd");
            end.put("episodes", episodes);
            end.put("successes", successes);
            end.put("successRate", episodes > 0 ? 100.0 * successes / episodes : 0);
            end.put("avgReward", episodes > 0 ? rewardSum / episodes : 0);
            end.put("stopped", stopRequested);
            send(session, end);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            log.warn("run loop 錯誤", e);
            send(session, error("run 失敗: " + e.getMessage()));
        }
    }

    private ObjectNode gridMsg(String sessionId, int[] pos) {
        GridInfo g = svc.grid(sessionId);
        ObjectNode m = om.createObjectNode();
        m.put("type", "grid");
        m.put("w", g.width());
        m.put("h", g.height());
        m.put("goalX", g.goalX());
        m.put("goalY", g.goalY());
        m.put("x", pos[0]);
        m.put("y", pos[1]);
        ArrayNode obs = m.putArray("obstacles");
        for (int[] o : g.obstacles()) {
            ArrayNode cell = obs.addArray();
            cell.add(o[0]);
            cell.add(o[1]);
        }
        return m;
    }

    private ObjectNode stepMsg(int ep, int st, StepResult r) {
        ObjectNode m = om.createObjectNode();
        m.put("type", "step");
        m.put("ep", ep);
        m.put("step", st);
        m.put("x", (int) r.x());
        m.put("y", (int) r.y());
        m.put("action", r.action());
        m.put("actionName", ACTION_NAMES[r.action() & 3]);
        m.put("reward", r.rawReward());
        m.put("shapedReward", r.shapedReward());
        m.put("done", r.done());
        ArrayNode v = m.putArray("values");
        for (double d : r.values()) {
            v.add(d);
        }
        return m;
    }

    private ObjectNode error(String msg) {
        ObjectNode m = om.createObjectNode();
        m.put("type", "error");
        m.put("message", msg);
        return m;
    }

    private void send(WebSocketSession session, ObjectNode msg) {
        try {
            synchronized (session) {
                if (session.isOpen()) {
                    session.sendMessage(new TextMessage(om.writeValueAsString(msg)));
                }
            }
        } catch (IOException e) {
            log.debug("WS 推播失敗: {}", e.getMessage());
        }
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        stopRequested = true;
    }
}

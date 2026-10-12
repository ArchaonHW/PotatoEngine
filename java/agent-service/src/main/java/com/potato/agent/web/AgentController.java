package com.potato.agent.web;

import java.util.Map;

import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import com.potato.agent.service.AgentSessionService;
import com.potato.agent.service.AgentSessionService.GridInfo;
import com.potato.agent.service.AgentSessionService.SessionInfo;
import com.potato.agent.service.AgentSessionService.StepResult;

/**
 * RL agent session 的 REST API。
 * 即時走位推播走 WebSocket（/ws/agent），此處只做 session 管理與單步操作。
 */
@RestController
@RequestMapping("/api/agent")
public class AgentController {

    public record CreateRequest(String algo, Integer gridSize, Boolean randMaps,
                                Long seed, String modelPath) {
    }

    public record CreateResponse(String id, String algo, int gridSize,
                                 boolean randMaps, long seed, GridInfo grid) {
    }

    public record ModelRequest(String path) {
    }

    private final AgentSessionService svc;

    public AgentController(AgentSessionService svc) {
        this.svc = svc;
    }

    @GetMapping("/status")
    public Map<String, Object> status() {
        return Map.of(
                "nativeLoaded", svc.nativeReady(),
                "sessions", svc.sessionCount(),
                "algorithms", new String[] {"qlearning", "dqn", "pg", "ac"});
    }

    @PostMapping("/sessions")
    public CreateResponse create(@RequestBody CreateRequest req) {
        String algo = req.algo() != null ? req.algo() : "dqn";
        int size = req.gridSize() != null ? req.gridSize() : 6;
        boolean rand = req.randMaps() != null && req.randMaps();
        long seed = req.seed() != null ? req.seed() : -1;
        SessionInfo info = svc.create(algo, size, rand, seed);
        if (req.modelPath() != null && !req.modelPath().isBlank()) {
            if (!svc.loadModel(info.id(), req.modelPath())) {
                svc.destroy(info.id());
                throw new IllegalArgumentException("模型載入失敗: " + req.modelPath());
            }
        }
        svc.reset(info.id());
        return new CreateResponse(info.id(), info.algo(), info.gridSize(),
                info.randMaps(), info.seed(), svc.grid(info.id()));
    }

    @DeleteMapping("/sessions/{id}")
    public Map<String, Object> destroy(@PathVariable String id) {
        svc.destroy(id);
        return Map.of("ok", true);
    }

    @GetMapping("/sessions/{id}/grid")
    public GridInfo grid(@PathVariable String id) {
        return svc.grid(id);
    }

    @PostMapping("/sessions/{id}/reset")
    public Map<String, Object> reset(@PathVariable String id) {
        int[] pos = svc.reset(id);
        return Map.of("x", pos[0], "y", pos[1], "grid", svc.grid(id));
    }

    @PostMapping("/sessions/{id}/step")
    public StepResult step(@PathVariable String id,
                           @RequestParam(defaultValue = "true") boolean explore,
                           @RequestParam(defaultValue = "false") boolean learn) {
        return svc.stepAuto(id, explore, learn);
    }

    @PostMapping("/sessions/{id}/end-episode")
    public Map<String, Object> endEpisode(@PathVariable String id,
                                          @RequestParam(defaultValue = "true") boolean learn) {
        svc.endEpisode(id, learn);
        return Map.of("ok", true, "epsilon", svc.explorationRate(id));
    }

    @PostMapping("/sessions/{id}/explore-rate")
    public Map<String, Object> exploreRate(@PathVariable String id,
                                           @RequestParam double rate) {
        svc.setExplorationRate(id, rate);
        return Map.of("ok", true, "epsilon", svc.explorationRate(id));
    }

    @PostMapping("/sessions/{id}/model/save")
    public Map<String, Object> save(@PathVariable String id,
                                    @RequestBody ModelRequest req) {
        return Map.of("ok", svc.saveModel(id, req.path()));
    }

    @PostMapping("/sessions/{id}/model/load")
    public Map<String, Object> load(@PathVariable String id,
                                    @RequestBody ModelRequest req) {
        return Map.of("ok", svc.loadModel(id, req.path()));
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}

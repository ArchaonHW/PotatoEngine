package com.potato.agent.web;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

import com.potato.agent.service.AgentSessionService;
import com.potato.agent.service.ChainService;

@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {

    private final AgentSessionService svc;
    private final ChainService chainSvc;

    public WebSocketConfig(AgentSessionService svc, ChainService chainSvc) {
        this.svc = svc;
        this.chainSvc = chainSvc;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(new AgentSocketHandler(svc), "/ws/agent")
                .setAllowedOriginPatterns("*");
        registry.addHandler(new ChainSocketHandler(chainSvc), "/ws/chain")
                .setAllowedOriginPatterns("*");
    }
}

package com.potato.agent.web;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

import com.potato.agent.service.AgentSessionService;

@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {

    private final AgentSessionService svc;

    public WebSocketConfig(AgentSessionService svc) {
        this.svc = svc;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(new AgentSocketHandler(svc), "/ws/agent")
                .setAllowedOriginPatterns("*");
    }
}

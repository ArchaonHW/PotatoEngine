package com.potato.web3.web;

import java.util.List;
import java.util.Map;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.webmvc.test.autoconfigure.WebMvcTest;
import org.springframework.http.MediaType;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;

import com.potato.web3.service.AnchorService;
import com.potato.web3.service.WalletService;
import com.potato.web3.service.WalletService.WalletInfo;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

/**
 * Web3 REST 契約切片測試——service 層以 mock 取代,
 * 驗證路由/參數綁定/回應結構。
 */
@WebMvcTest(Web3Controller.class)
class Web3ControllerTest {

    @Autowired
    private MockMvc mvc;

    @MockitoBean
    private WalletService walletService;

    @MockitoBean
    private AnchorService anchorService;

    private static final WalletInfo WALLET = new WalletInfo(
            "0x1111111111111111111111111111111111111111",
            "did:potato:0x1111111111111111111111111111111111111111",
            "aabbcc");

    @Test
    void statusContainsBackend() throws Exception {
        when(anchorService.backend()).thenReturn("local");
        when(walletService.serviceWallet()).thenReturn(WALLET);
        mvc.perform(get("/api/web3/status"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.backend").value("local"))
                .andExpect(jsonPath("$.serviceWallet.address")
                        .value(WALLET.address()));
    }

    @Test
    void createWalletReturnsDid() throws Exception {
        when(walletService.createWallet()).thenReturn(WALLET);
        mvc.perform(post("/api/web3/wallets"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.did")
                        .value("did:potato:" + WALLET.address()));
    }

    @Test
    void createAgentBindsName() throws Exception {
        when(walletService.createAgent("render-bot")).thenReturn(WALLET);
        mvc.perform(post("/api/web3/agents")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"name\":\"render-bot\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.address").value(WALLET.address()));
    }

    @Test
    void agentSignPassesDid() throws Exception {
        when(walletService.sign(eq("did:potato:0xabc"), eq("hello")))
                .thenReturn(Map.of("signature", "0xsig"));
        mvc.perform(post("/api/web3/agents/did:potato:0xabc/sign")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"message\":\"hello\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.signature").value("0xsig"));
    }

    @Test
    void anchorWithLedgerSessionRoutesToAnchorLedger() throws Exception {
        var rec = new AnchorService.AnchorRecord(1, "t", "aa", "src",
                "local", "0xsigner", "sig:0xproof");
        when(anchorService.anchorLedger("ledger-1")).thenReturn(rec);
        mvc.perform(post("/api/web3/anchors")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"ledgerSessionId\":\"ledger-1\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.headHash").value("aa"))
                .andExpect(jsonPath("$.signer").value("0xsigner"));
    }

    @Test
    void illegalArgBecomes400() throws Exception {
        when(walletService.sign(any(), any()))
                .thenThrow(new IllegalArgumentException("錢包不存在"));
        mvc.perform(post("/api/web3/sign")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"address\":\"0xnope\",\"message\":\"m\"}"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.error").value("錢包不存在"));
    }
}

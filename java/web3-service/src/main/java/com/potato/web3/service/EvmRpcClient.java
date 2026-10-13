package com.potato.web3.service;

import java.math.BigInteger;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

import org.springframework.web.client.RestClient;

import com.potato.web3.crypto.EthereumCrypto;

/**
 * EVM JSON-RPC 精簡 client——只包錨定需要的四支 method。
 * 不走 web3j（300+ transitive deps）；raw tx 由本專案自簽。
 */
public class EvmRpcClient {

    private final RestClient http;
    private final AtomicLong ids = new AtomicLong(1);

    public EvmRpcClient(String rpcUrl) {
        this.http = RestClient.builder().baseUrl(rpcUrl).build();
    }

    private Object call(String method, List<Object> params) {
        Map<String, Object> body = Map.of(
                "jsonrpc", "2.0",
                "method", method,
                "params", params,
                "id", ids.getAndIncrement());
        Map<?, ?> resp = http.post()
                .body(body)
                .retrieve()
                .body(Map.class);
        if (resp == null) {
            throw new IllegalStateException("RPC 空回應");
        }
        Object err = resp.get("error");
        if (err != null) {
            throw new IllegalStateException("RPC 錯誤: " + err);
        }
        return resp.get("result");
    }

    private static BigInteger hexToBig(Object hex) {
        String h = (String) hex;
        return new BigInteger(h.substring(2), 16);
    }

    public BigInteger nonce(String address) {
        return hexToBig(call("eth_getTransactionCount",
                List.of(address, "pending")));
    }

    public BigInteger gasPrice() {
        return hexToBig(call("eth_gasPrice", List.of()));
    }

    public BigInteger chainId() {
        return hexToBig(call("eth_chainId", List.of()));
    }

    public String sendRawTransaction(byte[] signedTx) {
        return (String) call("eth_sendRawTransaction",
                List.of("0x" + EthereumCrypto.Hex.encode(signedTx)));
    }

    public Map<?, ?> transactionReceipt(String txHash) {
        Object r = call("eth_getTransactionReceipt", List.of(txHash));
        return r instanceof Map<?, ?> m ? m : null;
    }
}

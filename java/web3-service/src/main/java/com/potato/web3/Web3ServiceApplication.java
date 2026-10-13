package com.potato.web3;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.cloud.client.discovery.EnableDiscoveryClient;

/**
 * web3-service——錢包/簽章/帳本錨定/agent 鏈上身份。
 */
@SpringBootApplication
@EnableDiscoveryClient
public class Web3ServiceApplication {

    public static void main(String[] args) {
        SpringApplication.run(Web3ServiceApplication.class, args);
    }
}

package com.potato.web3.crypto;

import java.math.BigInteger;
import java.util.List;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * RLP 編碼已知向量 + EIP-155 簽章確定性（RFC6979 deterministic-k）。
 */
class EvmRawTxTest {

    @Test
    void rlpSingleByte() {
        // 0x00-0x7f 單位元組原樣
        assertArrayEquals(new byte[]{0x42},
                EvmRawTx.rlpBytes(new byte[]{0x42}));
    }

    @Test
    void rlpStringDog() {
        // "dog" → 0x83 'd' 'o' 'g'（Ethereum 黃皮書範例）
        assertArrayEquals(new byte[]{(byte) 0x83, 'd', 'o', 'g'},
                EvmRawTx.rlpBytes("dog".getBytes()));
    }

    @Test
    void rlpListCatDog() {
        // ["cat","dog"] → 0xc8 83 'cat' 83 'dog'
        byte[] expect = {(byte) 0xc8, (byte) 0x83, 'c', 'a', 't',
                (byte) 0x83, 'd', 'o', 'g'};
        assertArrayEquals(expect, EvmRawTx.rlpList(List.of(
                EvmRawTx.rlpBytes("cat".getBytes()),
                EvmRawTx.rlpBytes("dog".getBytes()))));
    }

    @Test
    void rlpEmptyAndZero() {
        // 空字串 → 0x80;uint 0 編碼為空字串 → 0x80
        assertArrayEquals(new byte[]{(byte) 0x80},
                EvmRawTx.rlpBytes(new byte[0]));
        assertArrayEquals(new byte[]{(byte) 0x80},
                EvmRawTx.rlpUint(BigInteger.ZERO));
    }

    @Test
    void rlpLongString() {
        // >55 bytes → 0xb7+lenOfLen 形式
        byte[] b = new byte[1024];
        byte[] enc = EvmRawTx.rlpBytes(b);
        assertEquals((byte) 0xb9, enc[0]);  // 0xb7 + 2（1024 需 2 bytes）
        assertEquals(0x04, enc[1] & 0xFF);
        assertEquals(0x00, enc[2] & 0xFF);
        assertEquals(1027, enc.length);
    }

    @Test
    void signDeterministic() {
        // RFC6979 deterministic-k：同 tx 同 key → 位元組級相同簽章
        byte[] to = EthereumCrypto.Hex.decode(
                "0x3535353535353535353535353535353535353535");
        byte[] tx1 = EvmRawTx.sign(
                BigInteger.valueOf(9), BigInteger.valueOf(20_000_000_000L),
                BigInteger.valueOf(21000), to, BigInteger.TEN.pow(18),
                new byte[0], BigInteger.ONE,
                new BigInteger(
                    "4646464646464646464646464646464646464646464646464646464646464646",
                    16));
        byte[] tx2 = EvmRawTx.sign(
                BigInteger.valueOf(9), BigInteger.valueOf(20_000_000_000L),
                BigInteger.valueOf(21000), to, BigInteger.TEN.pow(18),
                new byte[0], BigInteger.ONE,
                new BigInteger(
                    "4646464646464646464646464646464646464646464646464646464646464646",
                    16));
        assertArrayEquals(tx1, tx2);
        assertTrue(tx1.length > 60);  // r+s 各 32B 以上
        assertEquals((byte) 0xf8, (byte) (tx1[0] & 0xF8));  // RLP list 前綴
    }
}

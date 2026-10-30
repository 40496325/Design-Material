# REII Increment 4 smoke test - run 20261028_212514

* Ground station: `COM10`  |  Crane: `COM12`
* Link: ch 77 (2477 MHz), 2 Mbps, CRC-16, 5-byte addresses, ARC 5 / ARD 500 us (ICD v1 IF10.0)
* Pass criteria: PER < 5.0 % and max RTT < 4.15 ms

## Gates

### G1 Crane radio (ESP32-S3, J1 + CE): **PASS**

    OK   SPI probe OK (nRF24L01+), SETUP_AW=03
    OK   CE high (J7.10=1, J3.3=1, paths J3.3(MCP GPA0)+J7.10(GPIO10))
         RIG,LINK,profile=ICD,mode=APP,ch=77,rate=2M,pa=LOW(-12dBm),arc=5,ard_us=500,crc=16,aw=5,payload=dyn32+ackpl32,listen=REII2,ce_paths=J3.3(MCP GPA0)+J7.10(GPIO10),ce_j7_10=1,ce_j3_3=1,cfg_errors=0
         RIG,REGS,CONFIG=0F,EN_AA=03,EN_RXADDR=02,SETUP_AW=03,SETUP_RETR=15,RF_CH=77,RF_SETUP=0A,STATUS=0E,FIFO=01,RX_PW_P1=32,DYNPD=03,FEATURE=06
         RIG,ADDR,RX_ADDR_P1=0x3249494552('REII2'),TX_ADDR=0x3149494552,RX_ADDR_P0=0x3149494552
    OK   listening on 'REII2'

### G2 Ground station radio (NodeMCU-32S): **PASS**

    OK   radio.begin() OK
    OK   isPVariant=1 (nRF24L01+)
         GS,LINK,mode=APP,ch=77,rate=2M,pa=LOW,arc=5,ard_us=500,profile=ICD,payload=dyn32+ackpl32,tx_to=REII2,radio=OK

### G3 Both ends set to profile STATIC, APP mode, PA LOW: **PASS**

    OK   GS mode=APP,ch=77,rate=2M,pa=LOW,arc=5,ard_us=500,profile=STATIC,payload=static32,tx_to=REII2,radio=OK
    OK   crane profile=STATIC ch=77 pa=LOW config=0F

### G4 Smoke STATIC PA LOW: **FAIL**

         PER 1.0 % (limit 5.0), RTT max 4.637 ms (limit 4.15), crane received 200, ACK lost 2, never arrived 0

### G3 Both ends set to profile ICD, APP mode, PA LOW: **PASS**

    OK   GS mode=APP,ch=77,rate=2M,pa=LOW,arc=5,ard_us=500,profile=ICD,payload=dyn32+ackpl32,tx_to=REII2,radio=OK
    OK   crane profile=ICD ch=77 pa=LOW config=0F

### G4 Smoke ICD PA LOW: **PASS**

         PER 0.0 % (limit 5.0), RTT max 3.188 ms (limit 4.15), crane received 200, ACK lost 0, never arrived 0
         199/200 ACKs carried a valid 0x81 telemetry frame; 197 of them echo the previous round's seq (the ACK payload is pre-loaded, so it answers the command before this one)

## Smoke runs

| profile | PA | n | ACKed | PER % | crane rx | ACK lost | never arrived | RTT avg / max ms | retries (max) | telemetry | result |
|---|---|---|---|---|---|---|---|---|---|---|---|
| STATIC | LOW | 200 | 198 | 1.0 | 200 | 2 | 0 | 0.734 / 4.637 | 14 (5) |  | **FAIL** |
| ICD | LOW | 200 | 200 | 0.0 | 200 | 0 | 0 | 0.818 / 3.188 | 5 (3) | 199 | **PASS** |

* ICD PA LOW: 199/200 ACKs carried a valid 0x81 telemetry frame; 197 of them echo the previous round's seq (the ACK payload is pre-loaded, so it answers the command before this one)

## Files

* `smoke.csv` - one row per round (rig_rx = the crane logged that seq, so 'never arrived' and 'arrived but ACK lost' are separated)
* `sweep.csv` - one row per profile / PA run
* `groundstation.log`, `rig.log` - raw captures

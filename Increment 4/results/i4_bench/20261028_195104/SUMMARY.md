# REII Increment 4 smoke test - run 20261028_195104

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

## RF diagnosis

```
{
  "gs_scan_total": 152,
  "rig_scan_total": 0,
  "crane_to_gs_rpd": [
    34,
    8586
  ],
  "gs_to_crane_rpd": [
    0,
    3557
  ]
}
```

* Crane receiver heard NO energy on any channel: crane nRF receive path is dead, or CE is not reaching the module (check the CE jumper on J3.3 / J7.10).
* GS did not hear the crane's carrier (RPD 34/8586): crane transmitter or CE path at fault, or the radios are too far apart.
* Crane did not hear the GS carrier (RPD 0/3557): crane receiver / CE at fault, or GS transmitter dead.

## Files

* `smoke.csv` - one row per round (rig_rx = the crane logged that seq, so 'never arrived' and 'arrived but ACK lost' are separated)
* `sweep.csv` - one row per profile / PA run
* `groundstation.log`, `rig.log` - raw captures

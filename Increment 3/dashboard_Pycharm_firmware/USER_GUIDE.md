# REII Dashboard User Guide (Architecture A1, D-10)

## Overview

The REII dashboard provides a real-time web interface for monitoring and controlling the main crane MCU over USB-CDC serial connection. It implements **Decision D-10**: Python web stack (Flask + Socket.IO) with WebSocket telemetry and CMD hex command framing.

## Architecture

- **Backend**: Flask (Python) with Socket.IO for WebSocket communication
- **Serial**: USB-CDC at 115200 baud on COM12 (configurable via `REII_GS_PORT` and `REII_GS_BAUD` env vars)
- **Protocol**: 32-byte ICD frames per PROVISIONAL §4 (D-01). Pack/unpack implemented in `protocol.py`.
- **Frontend**: Vanilla HTML/JS (`templates/index.html`, `static/app.js`, `static/style.css`)
- **Data flow**: Groundstation MCU → Serial → Python reader → Socket.IO → Browser

## Prerequisites

1. **Python 3.8+** with required packages:
   ```bash
   pip install -r requirements.txt
   # Required packages: flask, flask-cors, python-socketio
   ```

2. **Groundstation firmware** flashed on COM12 with:
   - Main MCU firmware (v1.0 pin allocations + serial CMD task)
   - Uplink telemetry: ACK-payload 32-byte frames (0x81 magic)
   - Downlink CMD: `CMD <64 hex chars>\n` format

3. **Physical connection**: ESP32-S3 board connected to PC via USB, mapped to COM12 (or configure port)

## Running the Dashboard

### 1. Start the Flask server

```bash
cd d:\DESIGN GIT\REII_Cline_Project_Context_Verified_New\project_sources\03_increment_3\dashboard
python app.py
```

Expected output:
```
 *  Running on http://0.0.0.0:5000
 *  Debug mode: off
[app] serial reader started on COM12
```

### 2. Open the web interface

Open your browser and navigate to:
```
http://localhost:5000
```

You should see the dashboard with real-time telemetry display.

## User Interface

### Main Areas

1. **Telemetry Panel** (left/top):
   - Displays latest uplink telemetry from the MCU
   - Fields: trolley position, hoist position, velocities, load, VFD current, temperature, magnet status, error code, fault flags, system health
   - Auto-updates via WebSocket

2. **Command Panel** (right/top):
   - **E-stop**: Emergency stop (toggle)
   - **Reset**: System reset (toggle)
   - **Manual mode**: Enable manual control (checkbox)
   - **Trolley control**: Slider (-128 to 127, forward/backward)
   - **Hoist control**: Slider (-128 to 127, up/down)
   - **Magnet control**: Toggle (0/1)

3. **Status Panel** (bottom):
   - Serial port status (open/closed)
   - Last command sent
   - Connection status

### Command Protocol

The dashboard sends commands to the groundstation using the **`CMD <64 hex chars>\n`** format:

```
CMD 81020000000000000000000000000000000000000000000000000000000000000\n
```

This frame contains:
- Magic: 0x81 (uplink magic)
- Type: 0x02 (CMD type)
- Seq: sequence number (auto-incremented)
- Mode flags: estop/reset/manual bits
- Trolley: -128 to 127
- Hoist: -128 to 127
- Magnet: 0 or 1

### Real-time Telemetry

The dashboard receives **ACK-payload telemetry** from the groundstation in this format:
```
ACK,1,Payload=810200000000000000000000000000000000000000000000000000000000000
```

Fields decoded:
- **Magic**: 0x81
- **SeqAck**: acknowledged sequence number
- **TrolleyPos**: i16 LE in mm (×0.001 for meters)
- **HoistPos**: i16 LE in mm (×0.001 for meters)
- **TrolleyVel**: i16 LE in mm/s
- **HoistVel**: i16 LE in mm/s
- **Load**: u16 in kg×10
- **VFD_Current**: u16 in A×10
- **Temp**: i8 in °C
- **MagnetStat**: u8 (0=off, 1=on)
- **ErrorCode**: u8
- **FaultFlags**: u8
- **SysHealth**: u8

## Testing the End-to-End System

### 1. PING/PONG Test

Open the browser console and type:
```javascript
socket.emit('text', 'PING');
```
Expected: Receive a `pong` event with `{ok: 1, rtt_ms: <value>}`

## Troubleshooting

### Common Issues

1. **Serial port not opening**:
   - Ensure COM12 is correct (check Device Manager)
   - Set `REII_GS_PORT` environment variable if different
   - Ensure no other program is using COM12

2. **No telemetry data**:
   - Verify groundstation firmware is flashed and running
   - Check that the MCU is transmitting ACK-payload frames
   - Ensure the serial monitor is not also open on COM12

3. **Command not reaching MCU**:
   - Verify the `CMD <hex>
` format is correct (64 hex chars = 32 bytes)
   - Check that manual mode is enabled (toggle the Manual checkbox)
   - Ensure estop is not engaged (E-stop should be off)

4. **WebSocket connection lost**:
   - Refresh the browser page
   - Ensure the Flask server is still running
   - Check that no firewall is blocking port 5000

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `REII_GS_PORT` | `COM12` | Groundstation serial port |
| `REII_GS_BAUD` | `115200` | Groundstation baud rate |
| `REII_SECRET_KEY` | `reii-dev-secret` | Flask secret key |

## Development Notes

### Adding New Telemetry Fields

1. Update `protocol.py` `UplinkFrame` dataclass with new fields
2. Update `_ACK_PAYLOAD_RE` regex in `serial_reader.py` if parsing format changes
3. Update `on_serial_event()` in `app.py` to emit new fields
4. Update `templates/index.html` to display new fields

### Adding New Commands

1. Update `DownlinkFrame` in `protocol.py` with new fields
2. Update `on_command()` in `app.py` to construct new frame format
3. Update MCU firmware `process_downlink_cmd()` to handle new command
4. Update dashboard UI in `static/app.js`/`templates/index.html`

### Changing Serial Port

Set the `REII_GS_PORT` environment variable before running `python app.py`:
```bash
REII_GS_PORT=/dev/ttyUSB0 python app.py
```

## References

- **Decision D-10**: Dashboard stack: Python web (Flask/FastAPI + HTML/JS frontend)
- **Decision D-01**: ICD §4 32-byte payload map PROVISIONAL (awaiting signed ICD)
- **Protocol**: `project_sources/03_increment_3/dashboard/protocol.py`
- **Serial Reader**: `project_sources/03_increment_3/dashboard/serial_reader.py`
- **Main MCU Firmware**: `NEWEST_EngDes26_start main rig micro/main/main.c`

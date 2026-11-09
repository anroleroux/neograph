# neoGraph Protocol Reference

This document describes how the parts of neoGraph talk to each other, **as the code
behaves today**. Where the behaviour is surprising or wrong, it is described as-is
and listed under [Known issues](#known-issues).

```mermaid
flowchart LR
    UI["Browser UI<br/>(ui/)"] -- "Part 1: packets over WebSocket" --- C0["Core 0<br/>network, flash, WiFi LED,<br/>USB keyboard"]
    C0 -- "Part 3: program in shared RAM,<br/>FIFO words" --- C1["Core 1<br/>runs the graph: GPIO, PWM,<br/>ADC, temperature"]
    C0 --- F[("Flash<br/>Part 2: config")]
    C0 -- "USB keyboard" --- PC["PC"]
```

| Part | What it covers | Code |
|---|---|---|
| [1. Transport](#part-1-transport-ui--hw) | Packets between the browser UI and the device | `src/websockets.c`, `src/tcpserver.c`, `ui/content/network.js` |
| [2. Config model](#part-2-config-model) | Messages, element types, the graph, networking, flash storage | `src/config.c`, `ui/content/graph.js`, `ui/content/components/` |
| [3. Core 0 ↔ core 1](#part-3-core-0--core-1) | The program core 1 runs, and the words between the two RP2350 cores | `src/program.c`, `src/hwman.c`, `src/main.c`, `src/config.c` |
| [USB keyboards](#usb-keyboards) | Keystrokes typed to the PC the device is plugged into | `src/keyboard.c`, `src/usb_descriptors.c`, `ui/content/components/keyboard.js` |

## Conventions

- **Byte order.** Every multi-byte number is big endian.
- **Hex.** Bytes are written as two hex digits (`0A`), FIFO words as eight (`80000200`).
- **Status.** Tables mark each entry **Implemented**, **Partial** (some of it works) or
  **Reserved** (the number is allocated but no code sends or handles it).
- **Constants.** Every number named here has a constant in
  [`src/protocol.h`](src/protocol.h), mirrored in
  [`ui/content/protocol.js`](ui/content/protocol.js). Use the constants, not the numbers.
- **Examples are tested.** Every `hex example=…`, `fifo example=…` and `program example=…` block in this file
  is extracted by `make test` and checked (see
  [Keeping this document honest](#keeping-this-document-honest)). In those blocks,
  `#` starts a comment.

---

## Part 1: Transport (UI ↔ HW)

### Connection

The device runs one TCP server on **port 80**.

- A plain HTTP request on any path returns the UI: one gzip-compressed HTML page.
- A request carrying `Upgrade: websocket` and `Connection: Upgrade` is upgraded to a
  WebSocket, on any path. The UI connects to `ws://<device host>`.

On the WebSocket:

- **Binary frames** carry one packet each (below).
- **Text frames** carry telemetry from the device (see [Telemetry](#telemetry-text-frames)).
- Ping is answered with pong, and close closes the connection.

### Session

```mermaid
sequenceDiagram
    participant UI as Browser UI
    participant C0 as Core 0
    participant C1 as Core 1
    Note over C0,C1: at boot
    C0->>C1: program compiled from the stored config
    UI->>C0: HTTP GET /
    C0-->>UI: index.html (gzip)
    UI->>C0: WebSocket upgrade
    C0-->>UI: 101 Switching Protocols
    C0-->>UI: 0x0D Device ID
    C0-->>UI: 0x0A Config (if one is stored)
    Note over UI: user edits network fields
    UI->>C0: 0x0B / 0x0C field update (written to flash)
    Note over UI: user saves the graph
    UI->>C0: 0x0A Config (written to flash)
    C0->>C1: program compiled from it
    Note over UI: user presses a button
    UI->>C0: 0x01 Command
    C0->>C1: action word: sets the button's signal
    C1-->>C0: LED tag + value (LED sinks only)
    loop every 5 ticks (1.2 s each)
        C1-->>C0: uptime
        C0-->>UI: text frame "uptime N"
    end
    loop every 100 ms
        Note over C0: reads the signals core 1 runs
        C0-->>UI: text frame "sig I O V" for each changed signal (each one every second)
    end
```

### Packet header

| Byte | Field | Notes |
|---|---|---|
| 0 | Packet type | See the table below |
| 1–2 | Payload length | Bytes after the header, big endian |
| 3… | Payload | A sequence of [messages](#message-header), except for 0x0D |

### Packet types

| Type | Constant | Direction | Payload | Status |
|---|---|---|---|---|
| `01` | `PKT_COMMAND` | UI → HW | Messages | Implemented |
| `02` | `PKT_DATA_VALUE` | — | — | Reserved |
| `0A` | `PKT_CONFIG` | UI ↔ HW | Messages | Implemented |
| `0B` | `PKT_UPDATE_CONFIG` | UI → HW | One message | Implemented |
| `0C` | `PKT_UPDATE_SECRET` | UI → HW | One message | Implemented |
| `0D` | `PKT_DEVICE_ID` | HW → UI | 8 raw bytes | Implemented |

The device sends no reply or acknowledgement to any packet.

#### `0D` Device ID

Sent by the device immediately after the WebSocket handshake. The payload is the
8-byte unique board ID (the DID). The UI uses it to find its own device among the
`B0` device elements of the config that follows.

```hex example=device-id
0D 00 08                    # Device ID, 8 bytes
BD FC EA C1 4E 1E 79 4D     # DID
```

#### `0A` Config

The full configuration: networking elements and the graph. The same packet,
header included, is what the device stores in flash (see [Storage](#storage)).

- **At boot.** If flash holds a config, core 0 compiles it into a program for core 1
  (see [Part 3](#part-3-core-0--core-1)), so the graph runs without a browser.
- **HW → UI.** Sent on connect, after `0D`, if flash holds a config. The UI
  rebuilds its graph from it (see [Restoring the graph](#restoring-the-graph)).
  Connecting doesn't set core 1 up again: its outputs keep their state.
- **UI → HW.** Sent when the user saves the graph. The device writes the whole
  packet to flash, then compiles it into a program, which core 1 runs from its next
  scan.

[Part 2](#part-2-config-model) describes the payload; [The graph](#the-graph) has a
full example.

#### `01` Command

Sent by the UI when the user operates a UI input. Each message names the element
that was operated, with the values of its outputs as the payload.

| Message byte | Field | Notes |
|---|---|---|
| 0 | Message type | The type of the operated element: `10` button, `12` slider or `13` 2D slider; or `35` clock, which sets the device's clock (below) |
| 1 | Flags | `00` |
| 2–3 | Element ID | The operated element |
| 4 | Payload length | The element's number of outputs: `01`, or `02` for a 2D slider |
| 5… | Values | One byte per output, in output order, each a fraction of full scale: `00` = 0 to `FF` = 1 (`VALUE_FULL`) |

**Values** run from 0 to 1, sent as one byte, `round(value × 255)`:

- **Button:** `00` released / `FF` pressed. A toggle button changes on each click; a
  momentary one (`BUTTON_MOMENTARY`) sends `FF` when pressed and `00` when let go.
- **Slider:** its position, `00`–`FF` (shown as 0.00–1.00). Dragging sends a command
  each time the byte changes.
- **2D slider:** X, then Y (see [2D sliders](#2d-sliders)). Dragging sends a command,
  with both values, each time either byte changes.

On the device each value becomes the [signal](#signals) of that output of the element,
0 to 1 (byte ÷ 255). Outputs read it: an LED or GPO is on from 0.5 up (`80`,
`VALUE_HALF`, and up). A PWM maps it onto its duty cycle or frequency, whichever that
input drives (see [Control payloads](#control-payloads)).

For each message, core 0 sends one [action word](#core-0--core-1-action-words) to core 1
per payload byte: byte *i* is the new value of the element's output *i*. Bytes after
the first `FIFO_OUTPUTS_MAX` (32) are ignored.

**A clock's command** (`35`) sets the device's [clock](#clocks) instead: its payload is
`CLOCK_SET_LEN` (3) bytes, the hour (0–23), minute and second (0–59), and core 0 sets the
clock to that time at once (`clock_set`), with no action word. The device has one
clock, so the element ID only says which clock node it was set from. A command that
is not a time of day, or not 3 bytes, is ignored.

```hex example=command-clock
01 00 08                    # Command, 8 bytes
35 00 00 07 03  07 1E 00    # clock n7 → 07:30:00
```

```hex example=command-button
01 00 06                    # Command, 6 bytes
10 00 00 01 01  FF          # button n1 → pressed, value 1
```

```hex example=command-slider
01 00 06                    # Command, 6 bytes
12 00 00 04 01  BF          # slider n4 → value 0.75
```

```hex example=command-slider-2d
01 00 07                    # Command, 7 bytes
13 00 00 06 02  FF 80       # 2D slider n6 → X 1, Y 0.5
```

#### `0B` Update config field, `0C` Update secret field

Change one field of a networking element without resending the whole config. The
payload is one `B0`–`B3` message (see [Networking](#networking)).

The device looks through the stored messages for one with the **same type, element
ID and field ID** (first payload byte), and replaces that message with the new one.
`0B` edits the config store; `0C` edits the secrets store.

- **`0B` adds a field** the device or network doesn't have yet (a `B1` or `B3`): the
  message goes after the element's last message, so fields added later, such as
  the [addresses](#network-addresses), can be set on a device configured before them.
  `0C` only replaces.
- **A password is never stored by `0B`:** the config store is sent to the UI.
- If the element isn't stored (`0B`) or no stored message matches (`0C`), or the
  result would not fit its flash region, nothing changes, and nothing is reported.
- The UI applies each `0B` to its copy of the config too, so saving the graph, which
  sends the networking messages back, keeps the change.
- The change is only written to flash. Networking reads it at the next boot.

```hex example=update-ssid
0B 00 08                    # Update config field, 8 bytes
B3 00 00 00 03  02 58 59    # network 0, field SSID = "XY"
```

```hex example=update-options
0B 00 07                    # Update config field, 7 bytes
B2 00 00 00 02  00 00       # network 0, field options = 0 (access point)
```

```hex example=update-ap-address
0B 00 0A                    # Update config field, 10 bytes
B3 00 00 00 05  04 0A 00 00 01   # network 0, field access point address = 10.0.0.1
```

```hex example=update-password
0C 00 0E                    # Update secret field, 14 bytes
B3 00 00 00 09  03 6E 65 77 70 61 73 73 31   # network 0, field password = "newpass1"
```

### Telemetry text frames

Core 0 sends the most recently connected client WebSocket text frames:

| Text | Meaning |
|---|---|
| `uptime N` | Uptime in 1.2 s ticks (integer), from core 1's [uptime word](#core-1--core-0) |
| `sig I O V` | The value V of output O of element `nI`: a [signal](#signals) of the program core 1 runs, in its unit (°C, volts, 0 to 1), up to 4 significant digits |
| `clock S` | What internet time is doing for the device's [clock](#clocks): `off`, `offline`, `waiting` or `synced`. Sent while the config has a clock, each second and when it changes |

- **Signals.** Every 100 ms core 0 reads the signals of the program core 1 runs, and
  sends a `sig` frame for each one whose value has changed; every second, and when
  core 1 takes a new program, it sends every one. A browser that connects learns them
  all within a second.
- **In the UI.** A `sig` frame goes to the node `nI`. A hardware input or logic
  element hands the value to the nodes wired to it: a [core temperature](#core-temperature)
  in °C, a [DS18B20](#ds18b20-temperature-sensors) its temperature in °C (output 0) and
  whether it answers (output 1), a [rotary encoder](#rotary-encoders) its position in
  clicks (output 0) and whether it is pressed (output 1), a [clock](#clocks) its hour,
  minute and second (outputs 0–2), an [ADC](#adc-inputs) in volts, a [GPIO input](#gpio-inputs) or
  [logic](#logic) on from 0.5. Other nodes, such as a button or slider, ignore theirs,
  and so does an element the page doesn't have. The UI ignores `uptime N`, and shows
  `clock S` in a clock node's settings.

```text
sig 1 0 31.27               # core temperature n1: 31.27 °C
sig 10 0 1.65               # ADC n10: 1.65 V
sig 19 0 1                  # GPIO input n19: on
sig 6 1 0.502               # 2D slider n6, Y: 0.502
sig 4 1 2                   # counter n4, its count: 2
sig 7 0 22.44               # DS18B20 n7: 22.44 °C (4 significant digits)
sig 7 1 1                   # DS18B20 n7: found
sig 8 0 12                  # rotary encoder n8: 12 clicks
```

### Limits

| Limit | Value | Where |
|---|---|---|
| Config packet | 4096 bytes including the packet header (`PKT_CONFIG_MAX`). The UI refuses to save a bigger graph, and the device refuses to store one. | `PKT_CONFIG_MAX`, `FLASH_SIZE` |
| Received frame | 4110 bytes including the WebSocket header (`WS_RX_BUF`: one frame carrying the largest packet). A frame may span TCP segments, and a segment may hold several frames; a bigger frame closes the connection. Fragmented messages (continuation frames) are not supported. | `handle_websocket_msg` |
| Sent frame | 65535 bytes, and no more than lwIP's free send buffer (`TCP_SND_BUF`, 17520 bytes) | `send_websocket_frame` |
| UI page | No limit: it is streamed from flash one segment at a time as the browser acknowledges it | `send_page` |
| Stored secrets | 256 bytes | `SECURE_FLASH_SIZE` |
| Clients | The most recent connection only receives telemetry | `tcp_server_accept` |

---

## Part 2: Config model

### Message header

Every packet payload except `0D` is a sequence of messages:

| Byte | Field | Notes |
|---|---|---|
| 0 | Message type | Which kind of element; see [Element types](#element-types) |
| 1 | Flags | Bit 0 `MSG_FLAG_OUTPUT`: the payload's [source references](#source-references) name an output. Bit 1 `MSG_FLAG_SHOWN`: a hardware input (`30` core temperature, `31` GPIO input, `32` ADC) or a logic element (`40`–`4D`) shows a tile in the Command view; the device ignores it (see [Hardware input tiles](#hardware-input-tiles)). Bit 2 `MSG_FLAG_PART`: logic that is a part of a copy of a [block](#blocks); the device runs it as any logic, and the UI skips it when it restores the graph. Other bits are 0. |
| 2–3 | Element ID | Big endian |
| 4 | Payload length | Bytes after this header, 0–255 |
| 5… | Payload | Depends on the type |

### Element IDs

- **Control elements** (`00`–`7F`) get their IDs from the UI, counting from 1 on
  each page load. In the graph a node shows its ID as `n<ID>`.
- **Networking elements** (`B0`–`B3`) have their own IDs. The device and network
  created on first boot both have ID 0.
- **Labels, positions and wires** (`A0`–`A2`) take the ID of the control element
  they describe (see [Labels, positions and wires](#labels-positions-and-wires)).
- **Block definitions** (`A3`, `A4`) have their own IDs, 1 to 255. The messages
  inside a definition have IDs of their own too, counting from 1 in each (see
  [Blocks](#blocks)). The parts of a copy of a block take control element IDs.
- IDs are unique within each of those groups, not across them.
- **ID 0 in a source reference means "not connected"**, so a control element never
  has ID 0.

### Source references

An output names the input wired to it with a **source reference**: which element,
and which of its outputs. Most sources have one output, 0; a
[2D slider](#2d-sliders) has two, X (0) and Y (1), and a [counter](#logic) two, done
(0) and its count (1).

| Message flags | Reference | Bytes |
|---|---|---|
| `MSG_FLAG_OUTPUT` (`01`) | The element ID, then its output | 3 |
| `00` | The element ID; its output is 0 | 2 |

- **The UI writes every reference with its output:** LED, GPO, PWM, USB keyboard and
  plot messages, and a PWM's `A2` wires, have the flag set.
- **Configs saved before sources had several outputs** have 2-byte references and the
  flag clear. The device and the UI still read them, as output 0, and the next save
  writes them with outputs. A `legacy-config` example is below.
- **Not connected** is ID 0, output 0.
- **A feedback wire** (see [Loops](#loops)) has bit 7 of its output byte set
  (`MSG_REF_FEEDBACK`, `80`): `00 04 80` is output 0 of `n4`, fed back. Bits 0–6 are
  the output. Only a reference in a logic element's payload is a feedback one.
- The payload tables below give the bytes with the flag set.

```hex example=legacy-config
0A 00 0C                                        # Config, 12 bytes
10 00 00 01 00                                  # button n1
20 00 00 02 02  00 01                           # LED n2 ← n1 (output 0), saved without outputs
```

### Element types

Control types use bits 4 to 6 of the type byte:

```text
  7   6   5   4   3   2   1   0
+---+---+---+---+---------------+
| 0 | 0 | H | I |   subtype     |   UI and hardware elements
+---+---+---+---+---------------+
| 0 | 1 |       subtype         |   logic elements
+---+---+-----------------------+
L (MSG_LOGIC_BIT, 0x40): 1 = logic element: runs on the device, reads sources, and is a source
                         (`MSG_LOGIC_LAST`, 0x4D, is the last defined)
H (MSG_HW_BIT, 0x20):    0 = UI element, 1 = hardware element
I (MSG_INPUT_BIT, 0x10): 0 = output,     1 = input
```

| Type | Constant | Element | Status |
|---|---|---|---|
| `00` | `MSG_UI_GAUGE` | UI output gauge | Reserved |
| `01` | `MSG_UI_INDICATOR` | UI output indicator: a light | Implemented |
| `02` | `MSG_UI_GRAPH` | UI output graph: a plot | Implemented |
| `10` | `MSG_UI_BUTTON` | UI input button | Implemented |
| `11` | `MSG_UI_SWITCH` | UI input switch | Reserved |
| `12` | `MSG_UI_SLIDER` | UI input slider | Implemented |
| `13` | `MSG_UI_SLIDER_2D` | UI input 2D slider: two outputs, X and Y | Implemented |
| `14` | `MSG_UI_MULTI_SLIDER` | UI input multi slider | Reserved |
| `15` | `MSG_UI_DIAL` | UI input dial | Reserved |
| `16` | `MSG_UI_TIMER` | UI input timer | Reserved |
| `20` | `MSG_HW_LED` | HW output: onboard LED | Implemented |
| `21` | `MSG_HW_GPO` | HW output: GPIO pin | Implemented |
| `22` | `MSG_HW_PWM` | HW output: PWM pin | Implemented |
| `23` | `MSG_HW_KEYBOARD` | HW output: a keystroke typed to the PC over USB | Implemented |
| `24` | `MSG_HW_STEPPER` | HW output: a stepper motor on four pins, through a ULN2003 | Implemented |
| `25` | `MSG_HW_SERVO` | HW output: a hobby servo on a PWM pin | Implemented |
| `26` | `MSG_HW_NEOPIXEL` | HW output: NeoPixels (WS2812), an array, circle or doughnut showing a graphic | Implemented |
| `30` | `MSG_HW_CORE` | HW input: core temperature | Implemented |
| `31` | `MSG_HW_GPI` | HW input: GPIO pin | Implemented |
| `32` | `MSG_HW_ADC` | HW input: ADC | Implemented |
| `33` | `MSG_HW_DS18B20` | HW input: a DS18B20 temperature sensor on a 1-Wire pin; two outputs | Implemented |
| `34` | `MSG_HW_ENCODER` | HW input: a rotary encoder and its push switch; two outputs | Implemented |
| `35` | `MSG_HW_CLOCK` | HW input: the device's clock, a time of day; three outputs, hour, minute and second | Implemented |
| `40` | `MSG_LOGIC_COMPARE` | Logic: a floating input above or below a threshold | Implemented |
| `41` | `MSG_LOGIC_AND` | Logic: on while every input is on | Implemented |
| `42` | `MSG_LOGIC_OR` | Logic: on while any input is on | Implemented |
| `43` | `MSG_LOGIC_NOT` | Logic: on while its input is off | Implemented |
| `44` | `MSG_LOGIC_DELAY` | Logic: its input, after an on and an off delay | Implemented |
| `45` | `MSG_LOGIC_SCALE` | Logic: a floating input mapped from one range onto another | Implemented |
| `46` | `MSG_LOGIC_MIN` | Logic: the least of its floating inputs | Implemented |
| `47` | `MSG_LOGIC_MAX` | Logic: the greatest of its floating inputs | Implemented |
| `48` | `MSG_LOGIC_PULSE` | Logic: on for a time after its input goes on | Implemented |
| `49` | `MSG_LOGIC_BLINK` | Logic: on and off in turn while its input is on | Implemented |
| `4A` | `MSG_LOGIC_TOGGLE` | Logic: turns over each time its input goes on | Implemented |
| `4B` | `MSG_LOGIC_LATCH` | Logic: on once set, off once reset | Implemented |
| `4C` | `MSG_LOGIC_COUNTER` | Logic: counts its input going on, up to a preset; two outputs, done and its count | Implemented |
| `4D` | `MSG_LOGIC_SINE` | Logic: a sine wave, floating, while its input is on | Implemented |
| `60` | `MSG_SUB` | A copy of a [block](#blocks); the device runs its parts, not it | Implemented |
| `61` | `MSG_SUB_INPUT` | In a block's definition: an input | Implemented |
| `62` | `MSG_SUB_OUTPUT` | In a block's definition: an output | Implemented |
| `A0` | `MSG_STRING` | String: a node's label | Implemented |
| `A1` | `MSG_LAYOUT` | A node's position in the graph | Implemented |
| `A2` | `MSG_WIRE` | The shape of the wire into an output | Implemented |
| `A3` | `MSG_SUB_DEF` | A block's definition: its name | Implemented |
| `A4` | `MSG_SUB_PART` | One message of a block's definition, whole | Implemented |
| `B0` | `MSG_DEVICE` | Device | Implemented |
| `B1` | `MSG_DEVICE_FIELD` | Device field | Implemented |
| `B2` | `MSG_NETWORK` | Network | Implemented |
| `B3` | `MSG_NETWORK_FIELD` | Network field | Implemented |

### Control payloads

| Element | Payload length | Payload |
|---|---|---|
| `10` Button | 0–1 | Byte 0, bit 0 (`BUTTON_MOMENTARY`): 0 = each click toggles it, 1 = it is on while held. Other bits are 0. The UI leaves the byte out for a toggle button (as buttons saved before it existed), and sends `01` for a momentary one. The device ignores button messages in a config. |
| `12` Slider | 1 | Byte 0, bit 0 (`SLIDER_VERTICAL`): 0 = horizontal (UI default), 1 = vertical. Other bits are 0. The device ignores slider messages in a config: the orientation is for the Command view. |
| `13` 2D slider | 1–4 | Byte 0, bit 0 (`SLIDER_RETURN`): 0 = it stays where it is let go (UI default), 1 = it returns home. Bits 1–2 (`SLIDER_EASE_MASK`), how it returns: `SLIDER_EASE_IMMEDIATE` (`00`), `SLIDER_EASE_LINEAR` (`02`, UI default) or `SLIDER_EASE_SIGMOID` (`04`). Other bits are 0. Byte 1: the return speed, 1–255 % of its range per second (UI default 100); without it, the return is immediate. Bytes 2–3: its home, X then Y as values, `00`–`FF` (UI default `80 80`, the centre); without them, the centre. The device uses only the home: it drives the slider's outputs there (`slider_2d_home`). See [2D sliders](#2d-sliders). |
| `01` Light | 3 | Bytes 0–2: source reference (0 = not connected). The device ignores light messages in a config: the light is drawn in the browser (see [Lights](#lights)). |
| `02` Plot | 4–20 | Bytes 0–2: source reference (0 = not connected) · byte 3: time window, 1–255 s (UI default 10) · bytes 4…: the unit, 0–16 bytes of UTF-8 text; none (the UI default) shows the source's unit. The device ignores plot messages in a config: the plot is drawn in the browser (see [Plots](#plots)). |
| `30` Core temperature | 0 | None: the chip has one sensor. See [Core temperature](#core-temperature). |
| `31` GPIO input | 2 | Byte 0: the GPIO pin, a [usable](#gpio-pins) one (UI default 3). Byte 1: options; bits 0–1 (`GPI_PULL_MASK`) the pin's pull, `GPI_PULL_NONE` (0), `GPI_PULL_UP` (1) or `GPI_PULL_DOWN` (2, UI default); bit 2 (`GPI_INVERT`): 0 = on while the pin is high (UI default), 1 = on while it is low. Other bits are 0. See [GPIO inputs](#gpio-inputs). |
| `33` DS18B20 | 2 | Byte 0: the GPIO pin of its data line (DQ), a [usable](#gpio-pins) one (UI default 15). Byte 1: options; bits 0–1 (`DS18B20_RES_MASK`) its resolution, `DS18B20_RES_9` (0) to `DS18B20_RES_12` (3, UI default); bit 2 (`DS18B20_PULLUP`): the pin's internal pull-up on (UI default off: a 4.7 kΩ resistor). Other bits are 0. See [DS18B20 temperature sensors](#ds18b20-temperature-sensors). |
| `34` Rotary encoder | 6 (`ENCODER_PAYLOAD_LEN`) | Byte 0: the GPIO of its A (CLK) pin (UI default 6); byte 1: of its B (DT) pin (UI default 7); byte 2: of its push switch (SW, UI default 8), or `ENCODER_NO_BUTTON` (`FF`) for none: [usable](#gpio-pins) pins that differ. Byte 3: options; bits 0–1 (`ENCODER_DETENT_MASK`) its pins' transitions a click, `ENCODER_DETENT_4` (0, UI default), `ENCODER_DETENT_2` (1) or `ENCODER_DETENT_1` (2); bit 2 (`ENCODER_REVERSE`): it counts the other way; bit 3 (`ENCODER_WRAP`): past its ends it goes on from the other (UI default on). Other bits are 0. Bytes 4–5: its steps, big endian, 1 to `ENCODER_STEPS_MAX` (1000; UI default 20). See [Rotary encoders](#rotary-encoders). |
| `35` Clock | 3 (`CLOCK_PAYLOAD_LEN`; or 0) | Byte 0: options; bit 0 (`CLOCK_SYNC`): internet time (UI default on). Other bits are 0. Bytes 1–2: its time zone, minutes ahead of UTC, a signed 16-bit big-endian number, `CLOCK_ZONE_MIN` (−720) to `CLOCK_ZONE_MAX` (840), a multiple of `CLOCK_ZONE_STEP` (15) (UI default 120: UTC+2). None, saved before clocks had settings, is internet time off, UTC. The device has one clock: the first clock in the config sets it. Set the time with a [command](#01-command). See [Clocks](#clocks). |
| `32` ADC | 1 | Byte 0: the ADC channel, 0 to `ADC_CHANNELS` − 1 (2), on GPIO `ADC_PIN_FIRST` (26) + channel (UI default 0). See [ADC inputs](#adc-inputs). |
| `20` LED | 3 | Bytes 0–2: source reference (0 = not connected) |
| `21` GPO | 4 | Bytes 0–2: source reference (0 = not connected) · byte 3: GPIO pin, a [usable](#gpio-pins) one (UI default 1) |
| `22` PWM | 17 | See below |
| `40` Compare | 12 | Bytes 0–2: source reference (0 = not connected) · byte 3: `COMPARE_ABOVE` (0) or `COMPARE_BELOW` (1) · bytes 4–7: threshold · bytes 8–11: hysteresis, 0 or more. Both are 32-bit IEEE 754 floats, big endian, finite, in the source's unit. See [Logic](#logic). |
| `41` AND, `42` OR | 3–12 | 1 to `LOGIC_INPUTS_MAX` (4) source references (0 = not connected). See [Logic](#logic). |
| `43` NOT | 3 | Bytes 0–2: source reference (0 = not connected) |
| `44` Delay | 11 | Bytes 0–2: source reference (0 = not connected) · bytes 3–6: on delay · bytes 7–10: off delay. Each a 32-bit big-endian number of milliseconds, 0 to `DELAY_MAX_MS` (86400000, a day). See [Logic](#logic). |
| `45` Scale | 20 | Bytes 0–2: source reference · byte 3: options, bit 0 `SCALE_CLAMP`: keep the output within its range · bytes 4–19: the input's from and to, then the output's from and to, each a 32-bit big-endian float, finite; the input's two differ. See [Logic](#logic). |
| `46` Min, `47` Max | 3–12 | 1 to `LOGIC_INPUTS_MAX` (4) source references (0 = not connected) |
| `48` Pulse | 7 | Bytes 0–2: source reference · bytes 3–6: its time on, milliseconds, 1 to `DELAY_MAX_MS` |
| `49` Blink | 11 | Bytes 0–2: source reference (0: it always blinks) · bytes 3–6: time on · bytes 7–10: time off, each milliseconds, 1 to `DELAY_MAX_MS` |
| `4A` Toggle | 3 | Bytes 0–2: source reference |
| `4B` Latch | 6 | Bytes 0–2: its set source reference · bytes 3–5: its reset source reference (each 0 = not connected) |
| `4C` Counter | 10 | Bytes 0–2: its count source reference · bytes 3–5: its reset source reference (each 0 = not connected) · bytes 6–9: its preset, a 32-bit big-endian number, 1 to `COUNTER_MAX` (1000000; UI default 3). See [Logic](#logic). |
| `4D` Sine | 19 (15) | Bytes 0–2: source reference, boolean (0: it always runs) · bytes 3–6: its period, milliseconds, `SINE_PERIOD_MIN_MS` (20) to `DELAY_MAX_MS` (UI default 2000) · bytes 7–10: its amplitude · bytes 11–14: its offset · bytes 15–18: its phase, degrees (UI: −360 to 360); each a 32-bit big-endian float, finite (UI defaults 0.5, 0.5 and 0). One of 15 bytes, saved before sines had a phase, has phase 0. See [Sine waves](#sine-waves). |
| `60` Block | 5–53 | Byte 0: its definition's ID · bytes 1–2: the ID of its first part · byte 3: its inputs, 0 to `SUB_PORTS_MAX` (8) · byte 4: its outputs, 0 to 8 · then a source reference for each input · then, for each output, the source reference it stands for. Always with `MSG_FLAG_OUTPUT`. See [Blocks](#blocks). |
| `61` Block input | 2 | In a definition only. Byte 0: its type, `SUB_TYPE_BOOL` (0) or `SUB_TYPE_FLOAT` (1) · byte 1: its index among the definition's inputs |
| `62` Block output | 5 | In a definition only. Bytes 0–2: its source reference · byte 3: its type · byte 4: its index among the definition's outputs |
| `A3` Block definition | 1–32 | Its name, UTF-8. Keyed by the definition's ID |
| `A4` Block definition message | 5–255 | One message of the definition, header and all. Keyed by the definition's ID |
| `24` Stepper | 22 | Bytes 0–2: source reference (0 = not connected) · bytes 3–6: the GPIO pins of IN1 to IN4 · byte 7: options · bytes 8–9: full steps per revolution · bytes 10–21: three floats, the scale, the top speed and the acceleration. See [Stepper motors](#stepper-motors). |
| `25` Servo | 12 | Bytes 0–2: source reference (0 = not connected) · byte 3: GPIO pin · bytes 4–5: angles at input 0 and 1 · bytes 6–9: pulses at 0° and 180° · bytes 10–11: speed limit. See [Servos](#servos). |
| `26` NeoPixels | 20–38 | Bytes 0–2: source reference · bytes 3–5: on/off source reference (each 0 = not connected) · byte 6: GPIO pin · byte 7: layout · byte 8: options · byte 9: brightness · byte 10: the bytes of its size, *k* · then its size · then its graphic: its type, the bytes of its settings, and those · then, in a chain, its data line in's source reference. See [NeoPixels](#neopixels). |
| `23` USB keyboard | 6, or 5–68 | Bytes 0–2: source reference (0 = not connected) · byte 3: `KEYBOARD_COMBO` (0) or `KEYBOARD_TEXT` (1) · then a combo's HID modifier bits and HID key, or 1–64 characters of text. See [USB keyboards](#usb-keyboards). |

Without `MSG_FLAG_OUTPUT` each [source reference](#source-references) is a byte
shorter, and so is everything after it: an LED is 2 bytes, a GPO 3, a PWM 15, a
stepper 21, a servo 11, NeoPixels 18–33, a plot 3–19 and a keyboard 5 or 4–67.
`pwm_payload_read`, `stepper_payload_read`, `servo_payload_read`,
`neopixel_payload_read` and `keyboard_payload_valid` in `src/protocol.h` read either.

#### GPIO pins

A GPO, PWM, stepper, servo or NeoPixel node may use **GPIO 0–22 and 26–28**, the pins on the Pico 2 W's header
(`PIN_USABLE_MASK`, bit n for GPIO n). GPIO 23, 24, 25 and 29 drive the WiFi chip:
changing them cuts the device off the network.

- The UI offers only the usable pins, and keeps the default pin when a config names
  another one.
- **One node per pin.** A GPO, PWM, stepper, servo, NeoPixel node or GPIO input is offered only the pins no
  other node uses, and an ADC only the channels whose pin (GPIO 26–28) no other node
  uses. A new node takes its default pin (GPO 1, PWM 2, GPIO input 3) if it is free,
  otherwise the lowest free one (a servo's default is 14, NeoPixels' 16); a new ADC the first free channel, and a new stepper
  GPIO 10–13 if all four are free, otherwise the lowest four free pins. A stepper's
  four pins differ.
- **One PWM per slice.** The pins of a PWM slice share its counter, so its frequency:
  GPIO *n* is on slice (*n* >> 1) & 7, so GPIO 0, 1, 16 and 17 are slice 0, GPIO 2, 3,
  18 and 19 slice 1, and so on. A PWM is not offered any pin on another PWM's slice,
  and a new one takes a free pin on a free slice. A servo runs its slice at 50 Hz, so
  it counts as a PWM here: no PWM or servo is offered a pin on another one's slice. A
  GPO, stepper or GPIO input may still use them: it doesn't use the slice.
- Core 1 doesn't set up a GPO, PWM, stepper, servo or NeoPixel node on another pin, and logs that it skipped
  it. A stepper missing one of its pins is skipped whole.

A PWM has two parameters, its **duty cycle** and its **frequency**. Each is constant, or
follows an input: a source element whose value maps linearly onto it, 0 → min and
1 → max (min may be above max, which inverts the input). The payload:

| Bytes | Field | Range (UI default) |
|---|---|---|
| 0–2 | Duty source reference, 0 = none | |
| 3 | GPIO pin | A [usable](#gpio-pins) one (2) |
| 4–5 | Constant frequency, Hz | `PWM_FREQ_MIN` (10)–65535 (1000) |
| 6 | Options: bit 0 `PWM_DUTY_INPUT`, bit 1 `PWM_FREQ_INPUT`: a set bit makes that parameter follow its source. Bit 2 `PWM_FREQ_LOG`: a frequency that follows its source goes from min to max logarithmically, not linearly. Other bits 0. | (0) |
| 7 | Constant duty, % | 0–`PWM_DUTY_MAX` (100) (50) |
| 8 | Duty at input 0 (min), % | 0–100 (0) |
| 9 | Duty at input 1 (max), % | 0–100 (100) |
| 10–12 | Frequency source reference, 0 = none | |
| 13–14 | Frequency at input 0 (min), Hz | 10–65535 (100) |
| 15–16 | Frequency at input 1 (max), Hz | 10–65535 (10000) |

- **A constant parameter is constant:** a PWM whose duty cycle and frequency are both
  constant runs from setup and ignores its inputs.
- **Only floating sources** the device has (a slider, a 2D slider's X or Y, or an ADC)
  can drive a PWM; see [Wire types](#wire-types).
- **A logarithmic frequency** (`PWM_FREQ_LOG`) is min × (max / min)^value: each step
  of the input multiplies it by the same factor, so a slider spends as long on
  100–1000 Hz as on 1–10 kHz. With 100 and 10000 Hz, 0.5 gives 1 kHz (5050 Hz when
  linear).
- **One source may drive both** parameters; the PWM then has one wire. The X and Y of
  one 2D slider are two sources, with a wire each.
- **Until a source sends a value** it reads 0, so a parameter from an input starts at
  its min.
- **Every field is always sent,** so switching a parameter between constant and input
  in the UI keeps its other values.

A node's label, position and incoming wire shape are separate messages (see
[Labels, positions and wires](#labels-positions-and-wires)).

### The graph

The graph editor's nodes are control elements, and its wires are **source
references**. A wire is not a message of its own:

- An output stores a [source reference](#source-references) to the input wired to it:
  an LED, GPO or plot its one source, a PWM the source of its duty cycle and of its
  frequency.
- An input (button, slider) can feed many outputs from each of its outputs; it stores
  nothing about them.

#### Wire types

Every input and output is **boolean** or **floating**, and a wire joins only the same
type:

| Type | Sources | Outputs | Values |
|---|---|---|---|
| Boolean | `10` button, `31` GPIO input, `33` DS18B20's found (output 1), `34` rotary encoder's pressed (output 1), `40`–`44` and `48`–`4B` logic, `4C` counter's done (output 0) | `20` LED, `21` GPO, `23` USB keyboard, `26` NeoPixels' on/off, `01` light, `41`–`44` and `48`–`4D` logic | 0 or 1 (`00` / `FF`) |
| NeoPixel data | `26` NeoPixels' data line out (output 0) | `26` NeoPixels' data line in (after their settings) | What its chain draws on its LEDs; see [NeoPixel chains](#neopixel-chains) |
| Floating | `12` slider, `13` 2D slider (X and Y), `32` ADC, `30` core temperature, `33` DS18B20's temperature (output 0), `34` rotary encoder's position (output 0), `35` clock's hour, minute and second (outputs 0–2), `45` scale, `46` min, `47` max, `4C` counter's count (output 1), `4D` sine | `22` PWM, `24` stepper, `25` servo, `26` NeoPixels, `02` plot, `40` compare, `45` scale, `46` min, `47` max | A slider, or a 2D slider's X or Y, 0 to 1 (`00`–`FF`); an ADC in volts, 0 to 3.3; the core temperature or a DS18B20 in °C; a count, 0 to its preset; an encoder's position, 0 to its steps; a clock's hour 0 to 23, its minute and second 0 to 59 |

A source has the same value, in the same unit, on the device (its
[signal](#signals)) and in the browser. The hardware inputs' values are on both: an
ADC or core temperature drives a PWM on the device and a plot in the browser, and a
GPIO input an LED, GPO or USB keyboard on the device and a light in the browser. A PWM
maps a floating value onto its range as a fraction of the source's full scale: 1 for
a slider or a scale, 3.3 V for an ADC, `CORE_TEMP_FULL` (100) °C for the core temperature,
`DS18B20_FULL` (100) °C for a DS18B20, its steps for a rotary encoder's position,
`CLOCK_HOURS` (24) for a clock's hour and `CLOCK_MINUTES` (60) for its minute or second,
for a min or max that of its first wired input, and for a counter's count its preset.
The UI only draws wires between matching types, and drops mismatched source references
when it restores a config (configs saved before types were checked may hold them).

When the user saves, the UI sends a `0A` config whose payload is:

1. the networking messages (`B0`–`B3`) of the last config the device sent, byte
   for byte, then
2. for each graph node, in the order the nodes were created: its control message,
   its label (`A0`), its position (`A1`) and, if a wire comes into it, the wire's
   shape (`A2`).

Graph messages in the device's previous config are dropped, so a save replaces the
stored graph rather than adding to it.

Example: a graph with button `n1` wired to LED `n2` and to GPO `n3` on pin 1,
with `n1` and `n2` renamed, then a PWM `n4` added on pin 2 at a constant 1000 Hz, and
a slider `n5` "Level" wired to the PWM's duty cycle (0 % at 0, 100 % at 1), saved
against the first-boot config below.

```hex example=save-config
0A 00 EC                                        # Config, 236 bytes
# networking, kept from the device's config
B0 00 00 00 09  00 BD FC EA C1 4E 1E 79 4D      # device 0: DID
B1 00 00 00 05  01 4C 38 53 47                  # device 0: name "L8SG"
B1 00 00 00 03  02 00 00                        # device 0: network 0
B2 00 00 00 02  00 01                           # network 0: options = join
B3 00 00 00 05  01 4C 38 53 47                  # network 0: name "L8SG"
B3 00 00 00 04  02 4C 52 58                     # network 0: SSID "LRX"
# the graph
10 00 00 01 00                                  # button n1
A0 00 00 01 05  53 74 61 72 74                  #   label "Start"
A1 00 00 01 04  FF 38 FF D8                     #   at (-200, -40)
20 01 00 02 03  00 01 00                        # LED n2 ← n1
A0 00 00 02 0C  50 6F 6D 70 65 20 C3 A0 20 65 61 75   #   label "Pompe à eau"
A1 00 00 02 04  00 64 00 28                     #   at (100, 40)
A2 00 00 02 04  F5 62 75 62                     #   wire from n1: 345.1° at n1, 165.1° at n2
21 01 00 03 04  00 01 00 01                     # GPO n3 ← n1, pin 1
A0 00 00 03 03  47 50 4F                        #   label "GPO"
A1 00 00 03 04  00 C8 00 00                     #   at (200, 0)
A2 00 00 03 04  FB F0 7B F0                     #   wire from n1: 354.3° at n1, 174.3° at n3
22 01 00 04 11  00 05 00 02 03 E8 01            # PWM n4: duty ← n5, pin 2, 1000 Hz, duty from input
                32 00 64  00 00 00 00 64 27 10  #   duty 50 %, 0–100 %; no frequency source, 100–10000 Hz
A0 00 00 04 03  50 57 4D                        #   label "PWM"
A1 00 00 04 04  00 C8 00 78                     #   at (200, 120)
A2 01 00 04 07  00 00 80 00 00 05 00            #   wire from n5: 0° at n5, 180° at n4
12 00 00 05 01  00                              # slider n5, horizontal
A0 00 00 05 05  4C 65 76 65 6C                  #   label "Level"
A1 00 00 05 04  FF 38 00 78                     #   at (-200, 120)
```

### Restoring the graph

When a config arrives, the UI rebuilds the graph from it:

- **If the config has no control messages** (a first-boot config), the UI keeps the
  graph it has, so a new device starts from the built-in demo graph: a button `n1`
  driving the onboard LED `n2`, and a momentary button `n3` driving a light `n5`
  through a copy `n4` of the [block](#blocks) "Blink 3" (its parts `n6` to `n9`).
  Inside the block is the [blink-three-times loop](#loops) of `logic-loop-config`:
  its input sets the latch, and its output is the blink. Below them, a slider `n10`
  ("Level") drives [NeoPixels](#neopixels) `n11`: a 4 × 4 array on GPIO 16 showing it
  as a green bar along its top row.
- **Otherwise the graph is replaced.** Each control message of a type the UI knows
  (`10` button, `12` slider, `13` 2D slider, `01` light, `02` plot, `20` LED, `21` GPO, `22` PWM,
  `23` USB keyboard, `24` stepper, `25` servo, `26` NeoPixels, `30` core temperature, `31` GPIO input,
  `32` ADC, `33` DS18B20, `34` rotary encoder, `35` clock, the logic `40`–`4D`
  and a block's copy `60`) becomes a node with the message's ID. Messages marked
  `MSG_FLAG_PART` are skipped: each copy of a block builds its parts again from its
  definition, which is restored first from its `A3` and `A4` messages (see
  [Blocks](#blocks)). If an ID
  appears more than once, as in configs saved by older UIs, the last message wins.
- **Position:** from the node's `A1` message. Without one, nodes are stacked: inputs
  at x = −200, outputs at x = 150, 100 apart from y = −100.
- **Settings and label:** a button takes its action (toggle or momentary), a slider its orientation, a 2D slider its home and whether and how it returns there, a plot its time window and unit, an ADC its channel, a GPIO input its pin, pull and when it is on, a DS18B20 its pin, resolution and pull-up, a rotary encoder its pins, steps and options (pins another node has keep its own), a GPO its pin, a USB keyboard its keystroke, a compare its condition, threshold and hysteresis, a delay, pulse or blink its times, a scale its ranges and clamp, and a PWM its
  pin and the settings of its duty cycle and frequency, from the message (an invalid value keeps the default), and a node takes the
  label from its `A0` message.
- **Wires:** one for each source reference of an output that names an output of a
  restored node of the output's [type](#wire-types): an LED's, GPO's, USB keyboard's or light's button, GPIO input or logic, a plot's or compare's slider, 2D slider, ADC or core temperature,
  each of an AND's, OR's, min's or max's inputs, a latch's set and reset sources, the other logic's input, and the floating sources of a PWM's parameters that follow an input. A reference without an output
  (a config saved before them) is to output 0. Each wire is shaped by its `A2` message; without one,
  the wire is drawn straight between the nodes.
- **Afterwards:** the Command view is rebuilt from the restored inputs, and new nodes
  get IDs above the highest restored one, and above every copy's parts.

### Plots

A plot (`02`) draws the value of the floating source wired to it in the Command view,
over its time window, newest on the right. Pointing at the plot reads the value at
that time.

- **Any number.** A plot isn't limited to the 0–1 of a command value: its value axis
  fits the values the window shows, with gridlines at round numbers. A value that is
  not finite leaves a gap.
- **The values come from the source in the browser.** A source hands each new
  value to the plots wired to it (`emit` in `ui/content/components/parent.js`); a
  slider does so when it sends its [command](#01-command), and a
  [core temperature](#core-temperature) when its telemetry arrives. Nothing is sent for a
  plot, and the device stores its message but leaves it out of core 1's
  [program](#the-program).
- **The plot holds each value until the next.** A slider sends a value only when it
  changes, so the trace is a step line. Wiring a plot starts it from the slider's
  current value; taking the wire out ends the trace.
- **The history lives in the page.** It is lost on reload, and kept for the largest
  window (255 s), so a wider window shows what was already received.
- **The unit** is shown after the value. By default it is the source's: V for an
  [ADC](#adc-inputs), °C for a [core temperature](#core-temperature), none for a
  slider. A unit set in the plot's settings (up to 16 bytes) overrides it, and is
  saved in the plot's message; clearing it, or setting "auto", goes back to the
  source's.

### Lights

A light (`01`) shows in the Command view whether the boolean signal wired into it, a
[button](#control-payloads), is on or off: a lamp, green while it is on, with the
label and "On" or "Off" below it.

- **On from `VALUE_HALF`,** as an LED: a button's 1 lights it, its 0 puts it out.
- **The values come from the source in the browser**, as a [plot's](#plots) do: a
  button hands each value it sends to the lights wired to it. Nothing is sent for a
  light, and the device stores its message but leaves it out of core 1's
  [program](#the-program).
- **Wiring it** shows the source's current state; without a source it shows `–`.

A momentary button `n1` wired to a light `n2`:

```hex example=light-config
0A 00 0E                                        # Config, 14 bytes
10 00 00 01 01  01                              # button n1, momentary
01 01 00 02 03  00 01 00                        # light n2 ← n1
```

Core 1's program has the button's signal, and leaves the light out:

```program example=light-config
s0 n1.0 button = 0
```

### Hardware input tiles

A hardware input (core temperature, GPIO input, ADC, DS18B20, rotary encoder, clock) or a [logic](#logic) element shows
a tile in the Command view only if its settings say so: "Command view: Show | Hide", Hide by default. A light or
plot wired to it shows its values either way. The choice is saved as `MSG_FLAG_SHOWN`
in its message's flags; a message without it, such as one saved before the choice
existed, is hidden. The device ignores the flag.

### Core temperature

A core temperature node (`30`) is a floating source whose value is the chip's
temperature. The Command view can show it on a tile (see
[Hardware input tiles](#hardware-input-tiles)), a plot wired to it draws it, and a
PWM wired to it follows it on the device.

- **Read on the device.** Core 1 reads the sensor every 100 ms, whatever the graph:
  16 reads averaged, then smoothed so each sample moves the temperature 1/8 of the
  way to it (it follows over about a second). One count of the sensor is about
  0.5 °C, and the sensor is only accurate to a few °C.
- **On the device.** Its [signal](#signals) is the temperature in °C, which changes
  when the smoothed temperature has moved 0.1 °C from it. A PWM wired to it takes it
  as a fraction of `CORE_TEMP_FULL` (100) °C, clamped: 40 °C puts a PWM's duty 40 %
  of the way from its min to its max. Several nodes may share the sensor.
- **In the browser.** Core 0 sends its signal as a `sig`
  [telemetry text frame](#telemetry-text-frames). The node hands each value to its
  plots, in °C.
- **Until the first frame** the node has no value: its tile shows `–`, and a plot
  wired to it draws nothing.

A core temperature `n1` driving a PWM's duty cycle, 0 % at 0 °C to 100 % at 100 °C,
and drawn on a plot:

```hex example=core-config
0A 00 24                                        # Config, 36 bytes
30 02 00 01 00                                  # core temperature n1, its tile shown
22 01 00 02 11  00 01 00 02 03 E8 01            # PWM n2: duty ← n1, pin 2, 1000 Hz, duty from input
                32 00 64  00 00 00 00 64 27 10  #   duty 50 %, 0–100 %; no frequency source, 100–10000 Hz
02 01 00 03 04  00 01 00 0A                     # plot n3 ← n1, 10 s
```

### GPIO inputs

A GPIO input node (`31`) is a boolean source: one of the device's header pins, read as
on or off. The Command view can show its state on a tile, On or Off (`–` until the
first reading; see [Hardware input tiles](#hardware-input-tiles)), and the
[lights](#lights) wired to it show it too.

- **Pull and polarity.** The pin's pull is none, up or down. "On when" Low
  (`GPI_INVERT`) makes it on while the pin is low: a switch to ground with the pull up.
  The default, pull down and on while high, suits a switch to 3.3 V.
- **On the device.** Core 1 reads the pin every scan (10 ms). A new state, held for 3
  reads in a row (30 ms) so a bouncing switch changes it once, becomes its
  [signal](#signals), 1 on and 0 off, as a button's: an LED, GPO or USB keyboard
  wired to it follows the pin without the browser (see [Core 1: the scan](#core-1-the-scan)).
- **In the browser.** Core 0 sends its signal as a `sig`
  [telemetry text frame](#telemetry-text-frames); the node hands it to its lights. A
  browser that connects learns the state within a second.
- **Input buffer on only to read.** Between reads core 1 turns the pin's input buffer
  off. On the RP2350's A2 stepping (erratum RP2350-E9) a pin with its input buffer on
  leaks enough to stay near mid-rail after it is let go, against the internal
  pull-down, and would read high long after; with the buffer off the pull-down brings
  it low. On that stepping an external pull-down of 8.2 kΩ or less, or a pull-up with
  a switch to ground, is the surest wiring.
- **3.3 V signals.** Drive the pin with 3.3 V, the Pico's logic level.
- **After a save.** The device reads a pin once a saved config has a GPIO input on it,
  so a new node's tile shows `–` until the graph is saved.
- **The pin is the input's.** The UI doesn't offer it to another node (see
  [GPIO pins](#gpio-pins)), and core 1 refuses a GPO, PWM, ADC or other GPIO input on
  the same pin from an older config: inputs are set up before outputs, and the first
  on a pin wins (see [Known issues](#known-issues)).

A GPIO input `n1` on GPIO 3, pulled up and on while low (a switch to ground), driving
the onboard LED `n2` and a light `n3`:

```hex example=gpi-config
0A 00 17                                        # Config, 23 bytes
31 00 00 01 02  03 05                           # GPIO input n1 on GPIO 3: pull up, on while low
20 01 00 02 03  00 01 00                        # LED n2 ← n1
01 01 00 03 03  00 01 00                        # light n3 ← n1
```

### ADC inputs

An ADC node (`32`) is a floating source: one of the device's analog inputs, ADC0 to
ADC2 on GPIO 26 to 28. Its value is the reading in volts, 0 to 3.3 (1.3 V reads 1.3),
and its unit is V. The Command view can show it on a tile (see
[Hardware input tiles](#hardware-input-tiles)), and a plot wired to it draws it.

- **On the device.** Core 1 samples each channel that has an ADC node every scan, and
  the reading in volts is the node's [signal](#signals), so a potentiometer can drive
  a PWM without the browser (see [Core 1: the scan](#core-1-the-scan)). A PWM takes it as
  a fraction of 3.3 V: 1.3 V puts its duty 39 % of the way from its min to its max.
- **In the browser.** Core 0 sends its signal as a `sig`
  [telemetry text frame](#telemetry-text-frames); the node hands it to its plots.
- **After a save.** The device samples a channel once a saved config has an ADC node
  on it, so a new node's tile shows `–` until the graph is saved.
- **ADC3** (GPIO 29) is the WiFi chip's clock on the Pico 2 W, so it isn't offered.
- **The pin is the ADC's.** The UI doesn't offer it to another node (see
  [GPIO pins](#gpio-pins)), and core 1 refuses a GPO, PWM or GPIO input on the same
  pin from an older config: inputs are set up before outputs, and the first on a pin
  wins (see [Known issues](#known-issues)).

An ADC on ADC1 (GPIO 27) driving a PWM's duty cycle, and drawn on a plot:

```hex example=adc-config
0A 00 25                                        # Config, 37 bytes
32 02 00 01 01  01                              # ADC n1 on ADC1, its tile shown
22 01 00 02 11  00 01 00 02 03 E8 01            # PWM n2: duty ← n1, pin 2, 1000 Hz, duty from input
                32 00 64  00 00 00 00 64 27 10  #   duty 50 %, 0–100 %; no frequency source, 100–10000 Hz
02 01 00 03 04  00 01 00 0A                     # plot n3 ← n1, 10 s
```

### DS18B20 temperature sensors

A DS18B20 node (`33`) reads a DS18B20 digital temperature sensor on one pin, its 1-Wire
data line. It is a source with two outputs: **its temperature** in °C (output 0,
floating, unit °C), and **found** (output 1, boolean): on while the sensor answers. The
Command view can show it on a tile (see [Hardware input tiles](#hardware-input-tiles)):
its temperature, `No sensor` while found is off, `–` until the device says.

**Wiring.** VDD (red) to 3.3 V, GND (black) to GND, DQ (yellow) to the pin, and a
4.7 kΩ resistor from DQ to 3.3 V. `DS18B20_PULLUP` turns on the pin's internal pull-up
(50–80 kΩ) instead: enough for one sensor on a short wire, not for long ones. One sensor
a pin: core 1 talks to it without its address (Skip ROM, `CC`). Parasite power, with
VDD tied to GND, is not supported: it needs a strong pull-up while it converts.

**Resolution.**

| `DS18B20_RES_` | Bits | Steps | A reading every |
|---|---|---|---|
| `_9` (0) | 9 | 0.5 °C | 94 ms (`ds18b20_conversion_ms`), plus the reading |
| `_10` (1) | 10 | 0.25 °C | 188 ms |
| `_11` (2) | 11 | 0.125 °C | 375 ms |
| `_12` (3, UI default) | 12 | 0.0625 °C | 750 ms |

The sensor reads −55 to +125 °C, to ±0.5 °C from −10 to +85 °C.

**On the device.** Core 1 drives the line open-drain: the pin's output level is 0,
and it is switched to an output to pull the line low and back to an input to let it
go. Each bit's time slot (70 µs; a reset 960 µs) is timed with interrupts off. The line
may idle between slots, so a sensor's work is spread over scans, about 2 ms a scan at
most, and the scan stays on time:

1. **Configure** (after it is found, or found again): reset (the sensor answers with a
   presence pulse), `CC`, Write Scratchpad (`4E`), TH `4B`, TL `46` (its alarm bytes,
   unused: their defaults) and its configuration register, the resolution in bits 5–6
   (`ds18b20_config`: `1F`, `3F`, `5F` or `7F`). The register isn't copied to its
   EEPROM, so the sensor is configured again whenever it is found.
2. **Convert:** reset, `CC`, Convert T (`44`).
3. **Wait** its conversion time, in scans.
4. **Read:** reset, `CC`, Read Scratchpad (`BE`); then its 9 bytes, 3 a scan.
5. **Check** (`ds18b20_celsius`): the Dallas CRC-8 (x⁸ + x⁵ + x⁴ + 1) of the 9 bytes is
   0, and they are not all `00` (the line held low) nor all `FF` (nothing answering).
   The temperature is bytes 0–1, little endian, two's complement, in 1/16 °C, with the
   bits below its resolution, which are undefined, cleared. Then back to 2.

- **Its signals.** A good reading sets its temperature and turns found on. 85 °C is
  what the sensor holds from power-on until it converts: an 85 °C reading is dropped
  once, unless the last reading was within 10 °C of it (`ds18b20_take`); the next 85 °C
  in a row is taken.
- **Lost.** No presence pulse, or a reading that fails its check, counts as a failure;
  after 3 in a row (`DS18B20_FAILS_LOST`) found goes off, its temperature keeps its last
  reading, and the sensor is looked for again every second. Core 1 logs when it finds
  and loses one.
- **A PWM, servo or NeoPixels** wired to its temperature take it as a fraction of
  `DS18B20_FULL` (100) °C, as they take the core temperature.
- **In the browser.** Core 0 sends both signals as `sig` [telemetry](#telemetry-text-frames);
  the node hands its temperature and found to the nodes wired to them.
- **After a save** it keeps its reading until the next, and is configured again.

A DS18B20 `n1` on GPIO 15 at 12 bits, its tile shown: a compare turns GPO `n3` (a fan)
on above 30 °C, back off below 29 °C, and the onboard LED shows that it is found:

```hex example=ds18b20-config
0A 00 29                                        # Config, 41 bytes
33 02 00 01 02  0F 03                           # DS18B20 n1, tile shown: GPIO 15, 12 bits
40 01 00 02 0C  00 01 00 00                     # compare n2 ← n1's temperature, above
                41 F0 00 00  3F 80 00 00        #   30 °C, hysteresis 1 °C
21 01 00 03 04  00 02 00 01                     # GPO n3 ← n2, GPIO 1
20 01 00 04 03  00 01 01                        # LED n4 ← n1's found (output 1)
```

### Rotary encoders

A rotary encoder node (`34`) reads a rotary encoder, such as a KY-040 module or a bare
EC11: a knob that turns without end, in clicks, with two contacts, A and B, that open
and close a quarter of a cycle apart, and often a push switch. It is a source with two
outputs: **its position** in clicks (output 0, floating, unit `clicks`), and
**pressed** (output 1, boolean): on while its switch is held. The Command view can
show it on a tile (see [Hardware input tiles](#hardware-input-tiles)): `5 of 20`, with
`, pressed` while it is.

**Wiring.** A (CLK) and B (DT) to two pins, the switch (SW) to a third or to none, `+`
to 3.3 V and GND to GND. Each pin's internal pull-up is on: a contact or the switch
closing pulls its pin low. A KY-040 has 10 kΩ pull-ups on A and B to its `+`, and often
none on its switch; powered from 5 V, it would pull the pins to 5 V.

**Its position** goes from 0 to its steps: each click one way (A changing first, from
rest; clockwise on a KY-040) adds 1, each the other way takes 1 off; `ENCODER_REVERSE`
swaps them. At its ends it stops, or with `ENCODER_WRAP` goes on from the other, and
runs 0 to its steps − 1: a knob of 20 clicks a turn with 20 steps comes back to 0 each
turn. It starts at 0, and **a save keeps it**, within the new steps: clamped, or wrapped.
A PWM, servo or NeoPixels wired to it take it as a fraction of its steps.

**A click.** Its pins' states, A then B, go `11 → 01 → 00 → 10 → 11` one way, the
other way back:

| `ENCODER_DETENT_` | Transitions a click | For |
|---|---|---|
| `_4` (0, UI default) | 4: a whole cycle, rest to rest (`11`) | most encoders: a KY-040, an EC11 |
| `_2` (1) | 2: half a cycle, resting at `11` or `00` | an encoder that rests at `00` too: with `_4` it counts every other click |
| `_1` (2) | 1: each change of a pin | an encoder without clicks |

With `_4` and `_2`, a click counts when the pins come to rest, after at least half a
click's transitions one way (`encoder_decode`): a bouncing contact, which goes to and
fro, nets out, and an edge it missed (both pins changed at once, which counts 0) still
clicks.

**On the device.** Its A and B pins can change faster than the 10 ms scan, so core 1
takes each of their edges, rising and falling, with an interrupt (`IO_IRQ_BANK0`, a
handler shared with core 0's): the handler acknowledges them, reads both pins and
decodes the change. Each scan adds the clicks since the last to its position. Its
switch is read each scan: a state held for 3 reads in a row, as a [GPIO
input](#gpio-inputs)'s, becomes pressed (on while the pin is low). Without a switch,
pressed stays off. A program runs at most 8 encoders (`ENCODERS_MAX`); more are skipped.

An encoder `n1` on GPIO 6 and 7, its switch on 8, 20 steps, its tile shown: its
position turns servo `n2` on GPIO 9 across its 180° in 20 clicks, and the onboard LED
shows when it is pressed:

```hex example=encoder-config
0A 00 24                                        # Config, 36 bytes
34 02 00 01 06  06 07 08 00 00 14               # encoder n1, tile shown: A GPIO 6, B 7, SW 8, 20 steps
25 01 00 02 0C  00 01 00 09 00 B4               # servo n2 ← n1's position, GPIO 9, 0° at 0, 180° at 1
                01 F4 09 C4 00 00               #   500 µs at 0°, 2500 µs at 180°, no speed limit
20 01 00 03 03  00 01 01                        # LED n3 ← n1's pressed (output 1)
```

### Clocks

The device has one **clock**, a time of day, and a clock node (`35`) reads it: a source
with three floating outputs, each a whole number, its **hour** (output 0, 0 to 23), its
**minute** (1) and its **second** (2), each 0 to 59. Every clock node in the graph reads
the same clock. The Command view can show it on a tile (see
[Hardware input tiles](#hardware-input-tiles)): `07:30:00`.

- **Keeping time.** The clock is the milliseconds the device's timer has counted since
  it powered up, plus an offset, a day round (`clock_time_of_day`): it doesn't drift with
  the scan, and keeps going through a save or a flash write. The Pico has no battery,
  so after power-up it counts from 00:00:00 until it is set: from the internet, or the
  browser.
- **Its settings** are the device's, in the first clock node's message: internet time
  (`CLOCK_SYNC`) and its **time zone**, minutes ahead of UTC. Core 0 takes them each time
  a config is applied (`timesync_config`, at startup and on each save). A new time zone
  moves a clock that was set by the change (`clock_shift`): 14:00 at UTC+2 is 13:00 at
  UTC+1. The UI keeps every clock node's settings the same.
- **Internet time.** With `CLOCK_SYNC`, core 0 asks an internet time server,
  `pool.ntp.org` (`TIMESYNC_SERVER`), for the time with lwIP's SNTP client, once: at
  startup, and when a config turns internet time on (a clock added with it, or it set
  on again). It asks only on a network (station mode): as its own access point the
  device has no internet. Until the server answers it asks again, after 15 s and then
  longer; once it does, the clock is set to its time, Unix seconds, in the time zone
  (`clock_from_unix`), and it stops. It doesn't ask again until the next startup, so a
  time set from the browser afterwards stays. Core 0 reports what it is doing as
  `clock off`, `offline`, `waiting` or `synced` [telemetry](#telemetry-text-frames).
- **Setting it by hand.** A clock [command](#01-command) sets it, at once, from the
  browser: its hour, minute and second, the time in its time zone (`clock_set` works out
  the offset). The time isn't in the config, so a save doesn't set the clock back, and
  it can be set before the graph is saved. The UI sets it from a clock node's settings,
  Hours, Minutes and Seconds, or the computer's time.
- **On the device** core 1 writes each clock node's three signals from the clock at each
  scan. A PWM, servo or NeoPixels wired to it take its hour as a fraction of
  `CLOCK_HOURS` (24), its minute or second of `CLOCK_MINUTES` (60).
- **In the browser** the node shows the device's time while the device reports it (its
  signals, as [telemetry](#telemetry-text-frames)); otherwise the browser keeps its own,
  from the computer's time or the time it last set, so a graph can be tried without a
  device.

A clock `n1`, its tile shown, set from the internet at UTC+2, turning GPO `n3` (a lamp)
on from 18:00, by its hour, and sweeping servo `n4` once a minute, by its second:

```hex example=clock-config
0A 00 33                                        # Config, 51 bytes
35 02 00 01 03  01 00 78                        # clock n1, tile shown: internet time, UTC+2
40 01 00 02 0C  00 01 00 00                     # compare n2 ← n1's hour, above
                41 88 00 00  00 00 00 00        #   17, hysteresis 0: on from 18:00
21 01 00 03 04  00 02 00 01                     # GPO n3 ← n2, GPIO 1
25 01 00 04 0C  00 01 02 09 00 B4               # servo n4 ← n1's second, GPIO 9, 0° at 0, 180° at 60 s
                01 F4 09 C4 00 00               #   500 µs at 0°, 2500 µs at 180°, no speed limit
```

### 2D sliders

A 2D slider (`13`) is a UI input with two floating outputs: **X** (output 0) and **Y**
(output 1). In the Command view it is a square pad with a puck; X runs from 0 at the
left to 1 at the right, and Y from 0 at the bottom to 1 at the top. Anything a slider
drives, either output can drive.

- **One command, both values.** Dragging the puck, or the arrow keys (Shift: ten steps),
  sends a [command](#01-command) with X then Y each time either byte changes. Core 0
  turns it into an action word for each output.
- **Wires name their output.** Each wire from a 2D slider carries its output in its
  [source reference](#source-references), and is labelled X or Y at the slider. New
  wires leave from the output picked in the slider's settings ("Wire from"); once a
  wire leaves from one, the setting turns to the other, so wiring X then Y takes no
  trip to the settings.
- **Home** (bytes 2–3, "Home X" and "Home Y" in the settings, 0 to 1): where the puck
  starts, and where it returns to. The UI default is the centre, `80 80`
  (`VALUE_HALF`, about 0.5 each).
- **Starting at home.** At boot its [signals](#signals) start at its home, so the
  outputs wired to it start there rather than at 0, as the puck does in the browser.
  A save keeps the values its signals have; a 2D slider new to the graph starts at
  home.
- **Let go** (`SLIDER_RETURN` clear), it stays where it is. With "Return home" set,
  letting go of the puck, or of an arrow key, takes it back home, like a joystick
  springing back. How it gets there is its return shape:

  | Return | Bits 1–2 | Moves |
  |---|---|---|
  | Immediate | `SLIDER_EASE_IMMEDIATE` | Straight home, in one command |
  | Linear | `SLIDER_EASE_LINEAR` | In a straight line at a steady speed |
  | Sigmoid | `SLIDER_EASE_SIGMOID` | In a straight line, speeding up then slowing down: a fraction *u* of the way through the time, it is 3*u*² − 2*u*³ of the way there |

  A linear or sigmoid return takes the distance over the return speed (byte 1, % of
  the range per second, on average): at 100 a corner takes about 0.7 s. It sends a
  command each time a value's byte changes on the way. Touching the pad, or an arrow
  key, stops the return where it is.

A 2D slider `n1` whose home is X 0.25, Y 0.75, and that returns there, driving a PWM's
duty cycle from its X and frequency from its Y:

```hex example=slider-2d-config
0A 00 1F                                        # Config, 31 bytes
13 00 00 01 04  03 64 40 BF                     # 2D slider n1, returns linearly at 100 %/s to (0.25, 0.75)
22 01 00 02 11  00 01 00 02 03 E8 03            # PWM n2: duty ← n1 X, pin 2, 1000 Hz, both from inputs
                32 00 64  00 01 01 00 64 27 10  #   duty 50 %, 0–100 %; frequency ← n1 Y, 100–10000 Hz
```

### Logic

Logic elements (`40`–`4D`) make control decisions on the device: core 1 runs them in
its scan, every 10 ms, with or without a browser. Each reads one or more sources and
is a source itself, with one output: **boolean** (an LED, GPO, USB keyboard, light or
other logic can be wired to it) or, for a scale, min, max or sine, **floating** (a PWM,
plot, compare or other floating logic can). A counter has two: done (0, boolean) and
its count (1, floating).

| Element | Reads | On while |
|---|---|---|
| `40` Compare | 1 floating source | Above: its input is above the threshold. Below: under it. See below |
| `41` AND | 1 to 4 boolean sources | Every wired input is on |
| `42` OR | 1 to 4 boolean sources | Any wired input is on |
| `43` NOT | 1 boolean source | Its input is off |
| `44` Delay | 1 boolean source | Its input, once the input has been on for the on delay; it goes off once the input has been off for the off delay |
| `48` Pulse | 1 boolean source | For its time on from each time its input goes on, however long the input stays on; going on again starts the time again |
| `49` Blink | 1 boolean source, or none | While its input is on (always, with none wired): on for its time on, then off for its time off, in turn, starting on |
| `4A` Toggle | 1 boolean source | Turns over each time its input goes on: a momentary button works as a switch |
| `4B` Latch | a set and a reset source | From when its set input goes on until its reset input does; while reset is on, off, whatever set is |
| `4C` Counter, done (output 0) | a count and a reset source | Its count is at its preset |

| Element | Reads | Value |
|---|---|---|
| `45` Scale | 1 floating source | Its input mapped from the input's from–to onto the output's from–to: 30–60 onto 0–1 turns 45 into 0.5. Either range may run downwards. With `SCALE_CLAMP` the output stays within its range |
| `46` Min, `47` Max | 1 to 4 floating sources | The least or greatest of its wired inputs, in their unit; with none wired, 0 |
| `4C` Counter, its count (output 1) | a count and a reset source | How many times its count input has gone on, up to its preset, where it stops; while its reset input is on, 0, whatever count is |
| `4D` Sine | 1 boolean source, or none | While its input is on (always, with none wired): its offset + its amplitude × sin(2π × *t* ÷ its period + its phase); off, where it starts. See [Sine waves](#sine-waves) |

- **A compare's hysteresis.** Above 40 with a hysteresis of 1 goes on when its input
  rises past 40, and off only when it falls below 39; below 40 goes on under 40 and
  off only above 41. A noisy input, such as a temperature near its threshold, doesn't
  flicker the output. The threshold and hysteresis are in the source's unit: °C for
  the core temperature, volts for an ADC, 0 to 1 for a slider.
- **Boolean inputs are on from 0.5**, as an LED's. An input wired to nothing reads 0:
  a NOT with no input is on. An AND, OR, min or max reads only its wired inputs, and
  with none is off, or 0. A blink with no input always blinks.
- **Times** count in scans: a delay of 0 follows its input in the same scan, and
  one of 1000 ms one second later; a pulse or blink is on (or off) for at least a
  scan. An input that goes back before a delay is up starts the count again, so a
  bounce shorter than the on delay never turns it on. On delay 0 and off delay 5000
  keeps a fan running 5 s after its input goes off.
- **In order.** Core 1 runs each logic element after the logic it reads, so a change
  goes through a chain of them in one scan; see [The program](#the-program).
- **Going on** is a boolean input moving from below 0.5 to 0.5 or more between two
  scans. When a program starts, a pulse, toggle or counter takes its input as it is,
  so an input that is already on doesn't count as going on.
- **When a program starts** (at boot and on each save) logic starts off, or at 0, with
  nothing counted, and takes its state at the first scan: a latch has to be set
  again, and a toggle starts off.
- **A latch's two wires** are told apart in its settings ("Set" and "Reset"), as a
  PWM's are: a wire drawn into it sets it if it has no set source yet, and otherwise
  resets it. A counter's are "Count" and "Reset", the same way. A wire leaves a
  counter from the output picked in its settings ("Wire from": Done or Count), as a
  2D slider's does.
- **In the browser: the device first, the browser as a fallback.** Each logic
  element's state reaches the browser as a `sig`
  [telemetry text frame](#telemetry-text-frames), 1 on and 0 off: its node shows it,
  a light wired to it shows it, and so can its tile (see
  [Hardware input tiles](#hardware-input-tiles)). Without a report from the device
  in the last 2.5 s (`LOGIC_DEVICE_FRESH_MS`), as with no device, a node not saved
  yet, or a device gone quiet, the browser runs the same logic itself, on the values
  its sources have in the browser: a button's, a slider's, a hardware input's last
  reading, other logic's. A dashed dot on the node marks the browser's state, a solid
  one the device's. So a graph can be tried out without hardware, and in the
  [offline demo](README.md). The browser drives no hardware: only the device drives
  the LED, GPOs, PWMs and keyboards.
- **Wiring.** In the graph a logic node is both a source and an output: a wire into it
  comes from a source, and a wire from it goes to an output. Between two logic nodes,
  a wire goes from the selected node to the one clicked. A wire may close a loop: see
  below.
- **In the browser, in order.** The browser works a change out as core 1 does: each
  logic node once the change has reached every input it reads (other than through a
  feedback wire), so a node never acts on half a change.

#### Loops

A wire that closes a loop is a **feedback wire**: the logic it goes into reads it as
the last scan left it, 10 ms late, and the logic it comes from runs after. A loop
goes round once a scan. This is how a graph repeats something: a counter, fed back,
stops what it counts.

- **Which wire.** The UI makes a wire a feedback wire when it closes a loop of
  ordinary wires as it is drawn: it is drawn dashed, and saved with
  `MSG_REF_FEEDBACK` in its [source reference](#source-references). When a wire the
  loop needs is taken out, the feedback wire becomes an ordinary one.
- **Only through a state.** The UI draws a wire that closes a loop only if the loop
  goes through logic that holds a state from scan to scan: a delay, pulse, blink,
  toggle, latch or counter. A loop of only compares, ANDs, ORs, NOTs, scales, mins
  and maxes would flip over every scan, or do nothing.
- **On the device,** core 1 orders the logic leaving the feedback wires out (see
  [The program](#the-program)). In a loop with no feedback wire (a config the UI
  didn't save), the first of its nodes in the config runs first.
- **In the browser,** a feedback wire hands a value on 10 ms (`FEEDBACK_MS`) after it
  changes, as the device's next scan reads it.

The core temperature `n1` above 40 °C, with 1 °C of hysteresis, drives GPIO 5 through
the compare `n2`, whose tile is shown:

```hex example=logic-config
0A 00 1F                                        # Config, 31 bytes
30 00 00 01 00                                  # core temperature n1
40 03 00 02 0C  00 01 00 00                     # compare n2 ← n1, above, its tile shown
                42 20 00 00  3F 80 00 00        #   threshold 40.0, hysteresis 1.0
21 01 00 03 04  00 02 00 05                     # GPO n3 ← n2, GPIO 5
```

A fan on GPIO 5 that runs while the core is above 40 °C and a switch on GPIO 3 is
on, and keeps running 5 s after either goes off; the onboard LED is lit while the
fan is off. The delay comes before the logic it reads in the config, and core 1
runs it after (see the [program](#program-examples)):

```hex example=logic-chain-config
0A 00 51                                        # Config, 81 bytes
30 00 00 01 00                                  # core temperature n1
31 00 00 02 02  03 02                           # GPIO input n2 on GPIO 3, pull down
44 01 00 05 0B  00 04 00                        # delay n5 ← n4
                00 00 00 00  00 00 13 88        #   on at once, off after 5000 ms
40 01 00 03 0C  00 01 00 00                     # compare n3 ← n1, above
                42 20 00 00  3F 80 00 00        #   threshold 40.0, hysteresis 1.0
41 01 00 04 06  00 03 00  00 02 00              # AND n4 ← n3, n2
21 01 00 06 04  00 05 00 05                     # GPO n6 ← n5, GPIO 5
43 01 00 07 03  00 05 00                        # NOT n7 ← n5
20 01 00 08 03  00 07 00                        # LED n8 ← n7
```

A fan on a PWM whose duty cycle follows the core temperature from 30 °C (0 %) to
60 °C (100 %); above 70 °C a latch sets an alarm that blinks the onboard LED until a
momentary button resets it:

```hex example=logic-more-config
0A 00 6E                                        # Config, 110 bytes
30 00 00 01 00                                  # core temperature n1
45 01 00 02 14  00 01 00 01                     # scale n2 ← n1, clamped
                41 F0 00 00  42 70 00 00        #   from 30.0 to 60.0
                00 00 00 00  3F 80 00 00        #   onto 0.0 to 1.0
22 01 00 03 11  00 02 00 02 03 E8 01            # PWM n3: duty ← n2, pin 2, 1000 Hz, duty from input
                32 00 64  00 00 00 00 64 27 10  #   duty 50 %, 0–100 %; no frequency source, 100–10000 Hz
40 01 00 04 0C  00 01 00 00                     # compare n4 ← n1, above
                42 8C 00 00  00 00 00 00        #   threshold 70.0, hysteresis 0.0
10 00 00 05 01  01                              # button n5, momentary
4B 01 00 06 06  00 04 00  00 05 00              # latch n6: set ← n4, reset ← n5
49 01 00 07 0B  00 06 00                        # blink n7 ← n6
                00 00 00 FA  00 00 00 FA        #   250 ms on, 250 ms off
20 01 00 08 03  00 07 00                        # LED n8 ← n7
```

A momentary button blinks the onboard LED three times. The latch runs the blink; the
counter counts the blinks; its done output, fed back to the latch's reset, stops
them. The counter is reset while the latch is off, so each press counts from 0:

```hex example=logic-loop-config
0A 00 40                                        # Config, 64 bytes
10 00 00 01 01  01                              # button n1, momentary
4B 01 00 02 06  00 01 00  00 04 80              # latch n2: set ← n1, reset ← n4 done, fed back
49 01 00 03 0B  00 02 00                        # blink n3 ← n2
                00 00 00 FA  00 00 00 FA        #   250 ms on, 250 ms off
4C 01 00 04 0A  00 03 00  00 05 00              # counter n4: count ← n3, reset ← n5
                00 00 00 03                     #   preset 3
43 01 00 05 03  00 02 00                        # NOT n5 ← n2
20 01 00 06 03  00 03 00                        # LED n6 ← n3
```

#### Sine waves

A sine (`4D`) makes a smooth wave: a floating value, its **offset** plus its
**amplitude** times sin(2π × *t* ÷ its **period** + its **phase**), so it swings from
offset − amplitude to offset + amplitude and back once each period. With phase 0 it
starts at its offset and rises (a negative amplitude falls first); the phase, in
degrees, moves where it starts: 90 at its top, 180 at its offset falling, 270 at its
bottom. Two sines of one period with phases 0 and 90 trace a circle. The UI's defaults, offset 0.5 and
amplitude 0.5 every 2 s, swing from 0 to 1: a PWM, servo or NeoPixels wired to it take
it as a fraction of 1, as a slider's, so it sweeps them across their whole range.

- **Its input runs it.** While its boolean input is on, or with nothing wired in, it
  runs. Off, it rests where it starts (offset + amplitude × sin(phase): its offset at
  phase 0), and when it goes on again it starts from the beginning of its wave.
- **On the device** core 1 works it out each scan (`program_logic_step`): *t* goes up
  10 ms a scan, back to 0 at its period, so a period of 20 ms (`SINE_PERIOD_MIN_MS`) is
  two scans. It starts at *t* = 0 when the program starts, at each save.
- **In the browser** the node works it out every 50 ms while it runs, so plots and
  lights move without a device; once the device reports it, its values are shown.

A sine `n1` with nothing wired in, sweeping servo `n2` on GPIO 9 from 0° to 180° and
back every 4 s:

```hex example=sine-config
0A 00 29                                        # Config, 41 bytes
4D 01 00 01 13  00 00 00                        # sine n1, no input: it always runs
                00 00 0F A0                     #   period 4000 ms
                3F 00 00 00  3F 00 00 00        #   amplitude 0.5, offset 0.5: 0 to 1
                00 00 00 00                     #   phase 0°
25 01 00 02 0C  00 01 00 09 00 B4               # servo n2 ← n1, GPIO 9, 0° at 0, 180° at 1
                01 F4 09 C4 00 00               #   500 µs at 0°, 2500 µs at 180°, no speed limit
```

### Blocks

A **block** is a graph of logic defined once and placed any number of times, like a
function or subroutine. Its **definition** is a graph of its own, edited in its own
view, with **input** and **output ports** (`61`, `62`) where its copies' wires come in
and go out; each port is on/off or a number. A **copy** (`60`) is a node in the main
graph with the definition's inputs and outputs.

- **What a definition holds:** logic (`40`–`4D`) and ports, with their labels,
  positions and wire shapes. Hardware and UI elements stay in the main graph, and a
  definition holds no copy of a block.
- **How it is saved:** the definition's name (`A3`), then each of its messages,
  whole, in an `A4` keyed by the definition's ID, with IDs of the definition's own.
  The device ignores both. Only the blocks the graph has a copy of are saved.
- **What the device runs:** each copy's **parts**. For each copy the UI writes a copy
  of the definition's logic, as ordinary logic messages marked `MSG_FLAG_PART`, with
  IDs from the copy's first part (byte 1–2 of its payload), in the definition's order.
  A part's reference to an input port is written as the copy's input's source
  reference.
- **A copy's outputs:** a reference to output k of a copy stands for the k-th
  reference in the copy's table: what that output port is wired from inside, as the
  parts read it. Core 1 follows it (`resolve_ref`), through up to 8 copies; a
  feedback wire on the way makes it a feedback reference. The copy itself is not run
  and has no signal.
- **In the browser** each copy runs its own parts, and shows the device's reports
  of them (`sig` frames for the parts' IDs). When the definition changes, every copy
  builds its parts again: a wire into or out of a port that is gone, or whose type
  changed, goes.
- **Limits:** the parts count towards the device's 64 nodes and 96 signals, and the
  definition and the parts towards the 4096-byte config. A label in a definition is
  at most 250 bytes.

A block "Flip", whose output turns over each time its input goes on, placed once
between a momentary button and the onboard LED. Its definition is an input, a toggle
and an output; the copy's part is the toggle `n10`:

```hex example=block-config
0A 00 57                                        # Config, 87 bytes
A3 00 00 01 04  46 6C 69 70                     # definition 1: "Flip"
A4 00 00 01 07  61 01 00 01 02 00 00            #   input n1: on/off, index 0
A4 00 00 01 0A  62 01 00 02 05 00 03 00 00 00   #   output n2 ← n3: on/off, index 0
A4 00 00 01 08  4A 01 00 03 03 00 01 00         #   toggle n3 ← n1
10 00 00 01 01  01                              # button n1, momentary
60 01 00 02 0B  01 00 0A 01 01                  # copy n2 of definition 1, parts from n10, 1 input, 1 output
                00 01 00  00 0A 00              #   input ← n1; output = n10
4A 05 00 0A 03  00 01 00                        # toggle n10 ← n1: n2's part
20 01 00 03 03  00 02 00                        # LED n3 ← n2's output
```

### USB keyboards

A USB keyboard node (`23`) types a keystroke to the PC the device is plugged into,
over its USB port, once each time its input goes on. Its input is boolean: a
[button](#control-payloads).

- **The USB port is two devices.** The PC sees a keyboard and, as before, the serial
  console that `printf` writes to. Both come up whatever the graph; the keyboard
  types only when a keyboard node's input goes on.
- **Once per press.** The keystroke is typed when the input goes from off to on
  (`VALUE_HALF` and up), and not again until it has gone off. A toggle button types on
  every other click, when it turns on; a momentary button types on every press, so
  it is the one to wire to a keyboard.
- **A key combo** (`KEYBOARD_COMBO`) presses one key while holding modifiers, then
  lets them all go: Ctrl+Alt+Delete, Win+L, F13. Byte 3 is the HID modifier bits,
  byte 4 the HID key (usage page 7), or 0 for the modifiers alone. The UI offers left
  Ctrl `01`, Shift `02`, Alt `04` and Win `08` (Command on a Mac, Super on Linux), and
  letters, digits, punctuation, F1–F24, Enter, Escape, Backspace, Tab, Space,
  arrows, Insert, Delete, Home, End, Page Up and Down, Print Screen, Scroll Lock,
  Pause, Caps Lock and Menu.
- **Text** (`KEYBOARD_TEXT`) types 1 to `KEYBOARD_TEXT_MAX` (64) characters, one key
  at a time: printable ASCII (`20`–`7E`), tab (`09`, Tab) and newline (`0A`, Enter).
  Each character is typed as on a US keyboard: the PC turns the key into a character
  with its own layout, so on another layout some characters come out differently. In
  the UI's settings, Enter is written `\n`, Tab `\t` and a backslash `\\`.
- **Typing speed.** The PC reads a key down or up each millisecond, so 64 characters
  take about 130 ms. Keystrokes from several presses wait their turn, up to 8; more
  are dropped.
- **No PC, no keystroke.** A press while nothing is listening on USB (unplugged, a
  charger, the PC asleep) is dropped, and unplugging drops the keystrokes waiting, so
  nothing is typed later out of context.
- **On the device.** Core 0 keeps each node's keystroke (up to 16 nodes) and types it;
  core 1 tells it when (see [Core 1: the scan](#core-1-the-scan)). A button in the
  browser reaches it as a command; a GPIO input reaches it without the browser.

A momentary button `n1` typing Ctrl+Alt+Delete, and a toggle button `n3` typing
`hello` and Enter:

```hex example=keyboard-config
0A 00 25                                        # Config, 37 bytes
10 00 00 01 01  01                              # button n1, momentary
23 01 00 02 06  00 01 00 00 05 4C               # keyboard n2 ← n1: combo, Ctrl+Alt, Delete
10 00 00 03 00                                  # button n3, toggle
23 01 00 04 0A  00 03 00 01 68 65 6C 6C 6F 0A   # keyboard n4 ← n3: text "hello\n"
```

### Stepper motors

A stepper node (`24`) turns a unipolar stepper motor, such as a 28BYJ-48, through a
ULN2003 driver board: the device drives the board's IN1 to IN4 from four GPIO pins,
and the board switches the motor's four coils. Its input is floating: a slider, an
ADC, the core temperature, a scale, a min or max, or a counter's count, taken **in its
own unit** (not as a fraction of a full scale, as a PWM takes it).

| Bytes | Field | Range (UI default) |
|---|---|---|
| 0–2 | Source reference, 0 = none | |
| 3–6 | GPIO pins of IN1, IN2, IN3, IN4 | Four [usable](#gpio-pins) pins that differ (10, 11, 12, 13) |
| 7 | Options: bits 0–1 (`STEPPER_DRIVE_MASK`) the drive, `STEPPER_WAVE` (0), `STEPPER_FULL` (1) or `STEPPER_HALF` (2); bit 2 `STEPPER_SPEED`: 0 = its input sets where it is, 1 = how fast it turns; bit 3 `STEPPER_REVERSE`: it turns the other way; bit 4 `STEPPER_RELEASE`: its coils are off while it is still. Other bits 0. | (`02`: half, position, hold) |
| 8–9 | Full steps per revolution | `STEPPER_STEPS_MIN` (4)–65535 (2048) |
| 10–13 | Scale: revolutions per unit of its input, or with `STEPPER_SPEED` RPM per unit | Any finite float (1) |
| 14–17 | Top speed, RPM | Above 0, up to `STEPPER_RPM_MAX` (300) (15) |
| 18–21 | Acceleration, RPM gained or lost each second; 0 = at once | 0 or more (30) |

The floats are 32-bit IEEE 754, big endian, as a [compare's](#logic).

- **Position** (no `STEPPER_SPEED`): it turns to its input × scale revolutions from
  where it was when the device started, rounded to a step. A slider with a scale of 1
  turns it up to one revolution; a counter's count with 0.25 a quarter turn a count.
  Below 0 is the other way. Nothing wired in reads 0, so it stays where it is.
- **Speed** (`STEPPER_SPEED`): it turns at its input × scale RPM, backwards below 0,
  and stops at 0. With a 2D slider's X, centred at 0.5, a [scale](#logic) from 0–1 to
  −1–1 makes the middle stop it.
- **The drive.** Wave turns one coil on at a time, 4 steps a cycle: the least current
  and torque. Full turns two on at a time, 4 steps a cycle: the most torque. Half
  turns one, then two, in turn: 8 steps a cycle, so twice the steps of a revolution
  (4096 for a 28BYJ-48), the smoothest and quietest. The steps per revolution are
  always full steps. A 28BYJ-48 has 32 a turn of its rotor and a 64:1 gearbox, so
  2048 (its gearbox is nearer 63.68:1, so it gains about half a degree a turn).
- **Motion.** The device speeds it up and slows it down at its acceleration, never
  past its top speed, and in Position slows down in time to stop at its target
  without going past it. A target that changes while it moves is taken at once. Its
  top speed is held to `STEPPER_RATE_MAX` (2000) steps a second; a 28BYJ-48 does
  about 15 RPM at 5 V (512 full or 1024 half steps a second), and stalls, buzzing,
  much past that.
- **Hold or release.** Without `STEPPER_RELEASE` its coils hold it from setup on, so
  it keeps its place under load; with it they are off while it is still, so the motor
  runs cooler (a held 28BYJ-48 draws about 240 mA) but can be turned by hand or slip,
  and then lose its place.
- **Where it is** is counted in steps, from 0 at power-on: the device has no sensor
  to find its home. A save keeps it, in revolutions (so changing the drive or the
  steps per revolution doesn't move it); a reboot starts it at 0 where it is.
- **Power.** Power the ULN2003 board and the motor from 5 V (VBUS or VSYS), not from a
  GPIO pin or 3.3 V; join its ground to the Pico's. The board's inputs work from the
  Pico's 3.3 V outputs.
- **On the device.** Core 1 steps it between scans, not only every 10 ms (see
  [Stepper outputs](#stepper-outputs)).

A slider `n1` setting where a half-stepped 28BYJ-48 on GPIO 10–13 is, one revolution
across the slider, at up to 15 RPM and 30 RPM a second:

```hex example=stepper-config
0A 00 21                                        # Config, 33 bytes
12 00 00 01 01  00                              # slider n1, horizontal
24 01 00 02 16  00 01 00 0A 0B 0C 0D 02 08 00   # stepper n2 ← n1, IN1–IN4 on GPIO 10–13, half, 2048 steps
                3F 80 00 00  41 70 00 00  41 F0 00 00   #   1 revolution a unit, 15 RPM, 30 RPM/s
```

### Servos

A servo node (`25`) turns a hobby servo, such as an SG90, to an angle: every 20 ms
(`SERVO_FREQ`, 50 Hz) its pin sends a pulse whose width sets the angle. Its input is
floating, and taken as a PWM takes it: as a fraction of its source's full scale (1 for
a slider or a scale, 3.3 V for an ADC, 100 °C for the core temperature, a counter's
preset), clamped to 0–1.

| Bytes | Field | Range (UI default) |
|---|---|---|
| 0–2 | Source reference, 0 = none | |
| 3 | GPIO pin | A [usable](#gpio-pins) one, on no other PWM's or servo's slice (14) |
| 4 | Angle at input 0, degrees | 0–`SERVO_ANGLE_MAX` (180) (0) |
| 5 | Angle at input 1 (full scale), degrees | 0–180 (180) |
| 6–7 | Pulse at 0°, µs | `SERVO_PULSE_MIN` (100)–`SERVO_PULSE_MAX` (3000) (500) |
| 8–9 | Pulse at 180°, µs | 100–3000 (2400) |
| 10–11 | Speed limit, degrees a second; 0 = none | 0–`SERVO_SPEED_MAX` (3600) (0) |

- **Its angle** is angle at 0 + (angle at 1 − angle at 0) × the fraction; angle at 0
  may be the larger, which turns the input around. Nothing wired in reads 0, so it
  goes to its angle at 0.
- **Its pulse** is pulse at 0° + (pulse at 180° − pulse at 0°) × angle ÷ 180. The
  pulse at 0° may be the longer, which turns the servo around. An SG90 turns about
  180° over 500–2400 µs (its datasheet says 1000–2000 µs for 90°); one that buzzes or
  strains at an end is past its stop, and wants that end's pulse brought in.
- **The speed limit** moves its angle towards the one it wants by at most that many
  degrees a second (core 1 moves it each 10 ms scan), for a slow, smooth sweep. A servo
  turns no faster than it can anyway: an SG90 about 600 °/s.
- **At setup** it goes straight to its angle: the device can't tell where it was.
- **It takes its pin's PWM slice,** at 50 Hz, so no PWM or other servo may be on that
  slice (see [GPIO pins](#gpio-pins)).
- **Power.** Power the servo from 5 V (VBUS or VSYS), not 3.3 V, and join its ground
  to the Pico's; its signal wire goes straight to the pin. A servo starting or under
  load draws hundreds of mA, which can brown out a Pico on a weak USB supply.

A slider `n1` turning an SG90 on GPIO 14 across its 180°, at up to 90° a second:

```hex example=servo-config
0A 00 17                                        # Config, 23 bytes
12 00 00 01 01  00                              # slider n1, horizontal
25 01 00 02 0C  00 01 00 0E 00 B4               # servo n2 ← n1, GPIO 14, 0° at 0, 180° at 1
                01 F4 09 60 00 5A               #   500 µs at 0°, 2400 µs at 180°, 90 °/s
```

### NeoPixels

A NeoPixel node (`26`) drives a chain of WS2812 (or WS2812B, SK6812 RGB) LEDs on one
pin, laid out as an array, a circle or a doughnut, and draws its input on them as a
**graphic**. Its input is floating, and taken as a servo takes it: as a fraction of its
source's full scale (1 for a slider or a scale, 3.3 V for an ADC, 100 °C for the core
temperature, a counter's preset), clamped to 0–1. A second, boolean input switches it
on and off: while it is off (below 0.5) every LED is off. Not wired, it is always on.
NeoPixel nodes can also be [chained](#neopixel-chains) on their data lines, so several
graphics show on one set of LEDs.

| Bytes | Field | Range (UI default) |
|---|---|---|
| 0–2 | Source reference, 0 = none | A floating source |
| 3–5 | On/off source reference, 0 = none: always on | A boolean source |
| 6 | GPIO pin, the LEDs' data in (DIN) | A [usable](#gpio-pins) one (16) |
| 7 | Layout | `NEOPIXEL_ARRAY` (0), `NEOPIXEL_CIRCLE` (1) or `NEOPIXEL_DOUGHNUT` (2) (array) |
| 8 | Options | Bit 0, `NEOPIXEL_ZIGZAG`: an array's every other row runs back (0) |
| 9 | Brightness | 0–255: each colour is scaled by it ÷ 255 (40) |
| 10 | *k*, the bytes of its size | An array 2; a circle 1–`NEOPIXEL_RINGS_MAX` (8); a doughnut 1 |
| 11 … 10 + *k* | Size | An array: its rows, then its columns (4, 4). A circle: each ring's LEDs, in the order they are wired (24, 16, 12, 8, 1). A doughnut: its LEDs (24). Each 1 or more, and at most `NEOPIXEL_LEDS_MAX` (256) LEDs in all |
| 11 + *k* | Graphic | `NEOPIXEL_GFX_BAR` (0), a bar level indicator; `NEOPIXEL_GFX_POINT` (1), a moving point; `NEOPIXEL_GFX_PATCH` (2), a patch (bar) |
| 12 + *k* | *g*, the bytes of its settings | A bar `NEOPIXEL_BAR_LEN` (6), a point `NEOPIXEL_POINT_LEN` (8), a patch `NEOPIXEL_PATCH_LEN` (8); 3 more with `NEOPIXEL_GFX_COLOUR_INPUT` |
| 13 + *k* … | Its settings | See below |
| After its settings | Data line in source reference: none, or 3 bytes | Another NeoPixel node's output 0, its data line out: see [NeoPixel chains](#neopixel-chains) (none) |

**The layouts.** LED 0 is the first on the chain, nearest the pin.

- **An array** is rows of columns: LED 0 at the top left, along the top row, then the
  next row down. With `NEOPIXEL_ZIGZAG` every other row (the 2nd, 4th …) runs back,
  right to left, as on many 8 × 8 and 16 × 16 panels. One row is a strip.
- **A circle** is rings, one inside another, as on a NeoPixel disc: its size lists each
  ring's LEDs in the order they are wired, so the first ring is LEDs 0 to its count − 1.
  "From the centre out" takes them smallest first (in wiring order among rings of one
  size), however they are wired.
- **A doughnut** is one ring.

**The graphic's options and colour.** Each graphic's settings end with its options and
its colour:

- options bit 0, `NEOPIXEL_GFX_REVERSE`: it runs the other way;
- options bit 1, `NEOPIXEL_GFX_COLOUR_INPUT`: its colour follows its input. It is
  followed by two colours, red, green, blue each: the colour at 0 and the colour at 1,
  and at a fraction *f* each of red, green and blue is that far from the one to the
  other (green at 0 and red at 1 is yellow half way). Without it, one colour, the
  same whatever the input.

**A bar** (`NEOPIXEL_GFX_BAR`) lights its steps in its colour as far as its input
reaches: a fraction *f* lights *f* × its steps, and the step it ends in as much as it
is reached (2.5 of 5 lights two, and the third at half), so it moves smoothly. Its
settings:

| Byte | Field | Values (UI default) |
|---|---|---|
| 0 | Along | `NEOPIXEL_BAR_ROW` (0) or `NEOPIXEL_BAR_COLUMN` (1) (row) |
| 1 | Position, from 0 | Which row, column or ring; a doughnut's first LED (0) |
| 2 | Options | `NEOPIXEL_GFX_REVERSE`, `NEOPIXEL_GFX_COLOUR_INPUT` (0) |
| 3–5 | Colour; at 0 with `NEOPIXEL_GFX_COLOUR_INPUT` | Red, green, blue, 0–255 each (0, 255, 0) |
| 6–8 | With `NEOPIXEL_GFX_COLOUR_INPUT`: the colour at 1 | (255, 0, 0) |

| Layout | Along `ROW` (0) | Along `COLUMN` (1) |
|---|---|---|
| Array | Along row *position*, from the left; reversed, from the right. Its steps are the LEDs of the row | Up column *position*, from the bottom; reversed, down from the top |
| Circle | Around ring *position* from its LED 0, in wiring order; reversed, from its LED 0 the other way | Across the rings, from the centre out, each ring a step lit whole; reversed, from the outside in. Position is 0 |
| Doughnut | Around from LED *position*, in wiring order; reversed, the other way | Not used |

**A point** (`NEOPIXEL_GFX_POINT`) moves from its start, at 0, to its end, at 1,
gliding from LED to LED: between two LEDs of its path it lights both, each as much as
it is near it, and on one LED that one alone. Its settings:

| Byte | Field | Values (UI default) |
|---|---|---|
| 0–1 | Its start | An array: a row and a column. A circle: a ring (in wiring order) and an LED of it, from 0 in wiring order. A doughnut: 0 and an LED (0, 0) |
| 2–3 | Its end | The same (an array's last row and column: 3, 3) |
| 4 | Options | `NEOPIXEL_GFX_REVERSE`, `NEOPIXEL_GFX_COLOUR_INPUT` (0) |
| 5–7 | Colour; at 0 with `NEOPIXEL_GFX_COLOUR_INPUT` | (0, 255, 0) |
| 8–10 | With `NEOPIXEL_GFX_COLOUR_INPUT`: the colour at 1 | (255, 0, 0) |

- **On an array** it goes in a straight line, so in any direction: along a row, up or
  down a column, or on a slant. At a fraction *f* its row is start row + (end row −
  start row) × *f*, and its column the same, each rounded to the nearest (a half up).
- **On a circle or doughnut** it goes from its start ring to its end ring, and around
  from its start angle to its end angle. An LED's angle is its place on its ring ÷ the
  ring's LEDs, in turns from LED 0; the point goes around in wiring order, or reversed
  the other way, so it can go the short or the long way. Its ring is the one at start
  rank + (end rank − start rank) × *f*, ranking the rings from the centre out, rounded;
  its LED the nearest to its angle on that ring. With the same ring and angle at both
  ends it goes once all the way round; with the same angle on two rings, straight out
  or in.
- **Its path** is a number of steps (`neopixel_point_steps`), each from one LED to the
  next: on an array the rows or the columns it crosses, whichever is more; on a circle
  or doughnut the rings it crosses or the LEDs it goes round on the bigger of its two
  rings (its sweep × that ring's LEDs, rounded), whichever is more. Step *i* of *S* is on
  the LED nearest the point at *i* ÷ *S* (as above). At a fraction *f*, the point is
  *f* × *S* steps along: between step *i* and *i* + 1 it lights the first by 1 − the
  part of the step it has gone, and the second by that part (a quarter of the way
  lights them three quarters and a quarter). Two steps on one LED light it whole; a
  path of no steps (the same LED at both ends of an array) lights its one LED.

**A patch** (`NEOPIXEL_GFX_PATCH`) lights every LED from its start to its end, and
stays there: its input doesn't move it or dim it. With `NEOPIXEL_GFX_COLOUR_INPUT` its
input sets its colour, from its colour at 0 to its colour at 1; without, it is one
colour whatever its input. Its settings are a point's (its start, its end, options and
colour), and it covers:

- **on an array** the rectangle with its start and end at opposite corners (either way
  round), the rows and columns of both included: one row of it is a strip of LEDs, one
  LED a single LED;
- **on a circle or doughnut**, on each ring from its start ring to its end ring (by
  rank from the centre out, both included), the LED nearest its start angle and the
  ones after it, round to the one nearest its end angle: in wiring order, or reversed
  the other way round. The LEDs past the first are its sweep (as a point's, but 0 when
  start and end have one angle) × the ring's LEDs, rounded, and at most all of them
  (`neopixel_patch_span`). The same angle at both ends lights one LED a ring; to light
  a doughnut of 24 whole, go from LED 0 to LED 23.

**Sending.** Each node that drives its LEDs (in a [chain](#neopixel-chains), only the last) takes a PIO state machine, running the Pico SDK's WS2812
program (`src/ws2812.pio`) at 800 kHz, and a DMA channel that feeds it, so core 1 doesn't
wait for the LEDs: 24 bits, 30 µs, an LED, 7.7 ms for 256. Core 1 sends the colours,
green, red, blue, most significant bit first, again when its input's fraction or its
on/off input changes (in a chain, any node's), once the last send has gone out and the line has been low 300
µs, which latches it. All the program's NeoPixel nodes may have 1024 LEDs between
them; the node that would go over, or that finds no state machine or DMA channel free,
is skipped, and logged. On a save the LEDs keep what they show until the new program
sends; a NeoPixel node taken out, or moved to another pin, has its LEDs turned off.

- **Power.** A WS2812 draws up to 60 mA at full white. Power more than a handful from
  5 V (VBUS, or a supply with its ground joined to the Pico's), not from 3.3 V, and
  keep the brightness down: at 40 of 255 a white LED draws about 10 mA. The data line
  goes to the pin; most WS2812Bs take the Pico's 3.3 V on it, and a level shifter
  (or a first LED powered a diode drop below 5 V) makes sure.

An 8 × 8 array on GPIO 16 showing a slider `n1` as a green bar along its top row, at
brightness 40, always on:

```hex example=neopixel-config
0A 00 20                                        # Config, 32 bytes
12 00 00 01 01  00                              # slider n1, horizontal
26 01 00 02 15  00 01 00 00 00 00               # NeoPixels n2 ← n1, no on/off: always on
                10 00 00 28                     #   GPIO 16, array, brightness 40
                02 08 08                        #   8 rows of 8
                00 06 00 00 00 00 FF 00         #   a bar along row 0, green
```

A 24-LED doughnut on GPIO 16 with a point going once round from LED 0 as slider `n1`
goes from 0 to 1, from green to red, while button `n3` is on:

```hex example=neopixel-point-config
0A 00 29                                        # Config, 41 bytes
12 00 00 01 01  00                              # slider n1, horizontal
10 00 00 03 00                                  # button n3
26 01 00 02 19  00 01 00 00 03 00               # NeoPixels n2 ← n1, on/off ← n3
                10 02 00 28                     #   GPIO 16, doughnut, brightness 40
                01 18                           #   24 LEDs
                01 0B 00 00 00 00 02            #   a point from LED 0 to LED 0: once round; colour from input
                00 FF 00  FF 00 00              #   green at 0, red at 1
```

An 8 × 8 array whose top two rows are lit, blue with slider `n1` at 0, red at 1:

```hex example=neopixel-patch-config
0A 00 25                                        # Config, 37 bytes
12 00 00 01 01  00                              # slider n1, horizontal
26 01 00 02 1A  00 01 00 00 00 00               # NeoPixels n2 ← n1, always on
                10 00 00 28                     #   GPIO 16, array, brightness 40
                02 08 08                        #   8 rows of 8
                02 0B 00 00 01 07 02            #   a patch from row 0 column 0 to row 1 column 7; colour from input
                00 00 FF  FF 00 00              #   blue at 0, red at 1
```

#### NeoPixel chains

A NeoPixel node has a data line out, its output 0, and may have a data line in: a
source reference after its settings, to another NeoPixel node's data line out. Joined so
they make a **chain**, which shows several graphics on one set of LEDs: a bar with a
point riding on it, a patch for a background, a marker at a set level.

- **Drawing.** The node with nothing on its data line in is the first. Each node draws
  its graphic over what the ones before it drew (`neopixel_draw`): an LED it lights takes
  its colour, an LED it lights only partly (a bar's end, a point between two LEDs) goes
  that much of the way from the colour under it to its own, and an LED it doesn't light
  keeps its colour. A node whose on/off input is off draws nothing: the chain shows
  through. So later nodes override earlier ones.
- **The last node**, whose data line out goes nowhere, drives the LEDs on its pin, with
  the state machine and DMA channel a NeoPixel node takes, and sends them again when
  what any node of its chain shows changes. The others take no pin.
- **One set of LEDs.** A chain's nodes have the same layout, size and wiring (and the
  UI writes one pin for all of them). Core 1 doesn't join a data line in that names no
  NeoPixel node, one with other LEDs, one that already feeds another (the first in
  program order wins), or one that would lead back round to itself; such a node starts
  from all off, and drives its own pin. The UI draws only a single line: a second
  data line out of a node replaces the first, and a line that would close a loop isn't
  drawn.
- **In the UI** the chain shares its LEDs' settings: the pin, layout, size and wiring
  set on any of its nodes are set on all. Joining two chains, the joined chain takes
  the settings of its last node; a data line taken out leaves the node it came from
  driving its own LEDs, on a free pin if another node has its pin. Each node's preview
  shows its chain drawn as far as it, each node at its own preview level.

A 12-LED doughnut on GPIO 16, all of it dim blue (`n2`), with a red point going once
round over it as slider `n1` goes from 0 to 1 (`n3`, over `n2`):

```hex example=neopixel-chain-config
0A 00 3F                                        # Config, 63 bytes
12 00 00 01 01  00                              # slider n1, horizontal
26 01 00 02 16  00 00 00 00 00 00               # NeoPixels n2, no input: always on
                10 02 00 28                     #   GPIO 16, doughnut, brightness 40
                01 0C                           #   12 LEDs
                02 08 00 00 00 0B 00            #   a patch from LED 0 to LED 11
                00 00 FF                        #   blue
26 01 00 03 19  00 01 00 00 00 00               # NeoPixels n3 ← n1, always on
                10 02 00 28                     #   GPIO 16, doughnut, brightness 40
                01 0C                           #   12 LEDs
                01 08 00 00 00 00 00            #   a point from LED 0 to LED 0: once round
                FF 00 00                        #   red
                00 02 00                        #   its data line in ← n2's data line out
```

### Labels, positions and wires

A node's label, position and incoming wire are each a message whose element ID is
the ID of the node it describes. The device stores them with the rest of the config and sends them
back on connect. Core 1's [program](#the-program) leaves them out.

| Message | Payload |
|---|---|
| `A0` label | 1–255 bytes of UTF-8 text, not null-terminated |
| `A1` position | 4 bytes: x then y, each a signed 16-bit big-endian number in graph (SVG) units, rounded |
| `A2` wire | 4 bytes: the handle angle at the source end, then at the output end, each an unsigned 16-bit big-endian fraction of a full turn (1/65536), counter-clockwise on screen from pointing right. For a wire into a PWM, 7 bytes: the same, then the source reference, with `MSG_FLAG_OUTPUT` set (6 bytes in configs saved without outputs). |

- The UI saves a label and a position for every node, including unchanged defaults,
  and a wire shape for every wire. An LED or GPO has at most one incoming wire, so the
  output's ID identifies it; a PWM has up to two, told apart by their source references.
- The Settings view rejects a label longer than 255 bytes.
- Each node adds about 15 bytes plus its label to the config, and each wire 9 more.
  A config is at most 4096 bytes (see [Limits](#limits)): about 100 wired nodes with
  short labels, fewer with long ones.

### Networking

Four element types describe devices and networks. The payload of each starts with a
**field ID**, followed by the field's value:

| Byte | Field |
|---|---|
| 0 | Field ID |
| 1… | Field value (payload length − 1 bytes) |

- `B0` declares a **device** and `B2` declares a **network**. Their element ID is the
  device's or network's ID, and they carry one field.
- `B1` and `B3` set further fields on the device or network with the same element ID.
  Each message sets one field.

| Element | Field | Constant | Value | Store |
|---|---|---|---|---|
| `B0`/`B1` device | `00` DID | `FIELD_DEVICE_DID` | 8 bytes | config |
| | `01` name | `FIELD_DEVICE_NAME` | ASCII, up to 31 bytes | config |
| | `02` network | `FIELD_DEVICE_NETWORK` | 2-byte network ID | config |
| `B2`/`B3` network | `00` options | `FIELD_NETWORK_OPTIONS` | 1 byte, see below | config |
| | `01` name | `FIELD_NETWORK_NAME` | ASCII, up to 31 bytes | config |
| | `02` SSID | `FIELD_NETWORK_SSID` | ASCII, up to 31 bytes | config |
| | `03` password | `FIELD_NETWORK_PASSWORD` | ASCII, up to 31 bytes | **secrets** |
| | `04` access point address | `FIELD_NETWORK_AP_ADDRESS` | 4 bytes, IPv4 | config |
| | `05` fixed address | `FIELD_NETWORK_STATIC_ADDRESS` | 4 bytes, IPv4 | config |
| | `06` gateway | `FIELD_NETWORK_GATEWAY` | 4 bytes, IPv4 | config |

Network options, bit 0 (`NET_OPT_JOIN`): `0` = start an access point (with a DHCP
server) using the SSID and password; `1` = join an existing network. Other bits are
unused.

#### Network addresses

Address fields hold an IPv4 address as 4 bytes, most significant first
(`C0 A8 04 01` is 192.168.4.1). A network without the field, or with `00 00 00 00`,
hasn't set it. The UI shows the field for the mode the network is in; each mode keeps
its own.

- **Access point** (`04`): the device's address on its own network, and the gateway
  and DNS server its DHCP server hands out. The network is the address's /24; the
  DHCP server leases hosts `NET_LEASE_FIRST` (16) to 23 of it, so the access point
  can't take one of those, or .0 or .255. Not set: 192.168.4.1. Phones that join
  the access point open `http://<address>`.
- **Joining, fixed address** (`05`): the device joins with DHCP as before, then
  releases the lease and takes this address, keeping the lease's netmask. Not set:
  it keeps the DHCP address. The address must be free on that network: the device
  doesn't check.
- **Joining, gateway** (`06`): with a fixed address, the network's gateway. Not set:
  the one DHCP gave. The UI shows it only when a fixed address is set.
- **Usable addresses** (`net_address_usable` in `src/protocol.h`, `addressUsable` in
  the UI): unicast, not loopback (127), and not .0 or .255. The device ignores a field
  that isn't usable, and the UI refuses it.

At boot, the device finds the `B0` whose DID matches its own, reads that device's
fields, then reads the fields of the network it points to. It reads the config store
first and then the secrets store. The UI does the same with the DID from `0D`, and
does not read the password.

### Storage

The config and the secrets are each stored as a complete `0A` packet, header
included, at the end of flash:

| Region | Offset from end of flash | Size | Holds |
|---|---|---|---|
| Config | −4096 | 4096 bytes | Everything except passwords; sent to the UI on connect |
| Secrets | −4352 | 256 bytes | `B3` password fields; never sent to the UI |

On boot, if the config region does not start with `0A`, the device writes a default
config and default secrets:

- **Device 0**, with the board's DID. Its name is 4 characters derived from the DID:
  a letter, then three letters or digits.
- **Network 0**, with the same name, options = join, and SSID `LRX`.
- **Secrets**: a `B3` password field for network 0 with a fixed 8-character password
  (`build_secrets` in `src/config.c`).

```hex example=default-config
0A 00 3A                                        # Config, 58 bytes
B0 00 00 00 09  00 BD FC EA C1 4E 1E 79 4D      # device 0: DID
B1 00 00 00 05  01 4C 38 53 47                  # device 0: name "L8SG"
B1 00 00 00 03  02 00 00                        # device 0: network 0
B2 00 00 00 02  00 01                           # network 0: options = join
B3 00 00 00 05  01 4C 38 53 47                  # network 0: name "L8SG"
B3 00 00 00 04  02 4C 52 58                     # network 0: SSID "LRX"
```

---

## Part 3: Core 0 ↔ core 1

The RP2350 has two cores:

- **Core 0** runs networking and flash, and drives the onboard LED. The LED is
  attached to the WiFi chip, which only core 0 talks to. It also runs USB: the
  serial console and the [USB keyboards](#usb-keyboards).
- **Core 1** runs the graph: it owns the GPIO pins, the PWM slices and the ADC, and
  drives the outputs from the inputs every 10 ms, without the browser.

```mermaid
flowchart LR
    CFG[("0A config")] -- "core 0 compiles<br/>(program_compile)" --> P["program<br/>signals + nodes<br/>(shared RAM)"]
    P -- "core 1 runs it,<br/>every 10 ms" --> HW["pins, PWM, ADC"]
    CMD["01 command"] -- "action word (FIFO)" --> P
    P -- "core 0 reads the signals" --> TEL["sig telemetry"]
```

They share two things:

- **The program**, in RAM both cores see: the config's graph as core 1 runs it,
  compiled by core 0 (`src/program.h`). It replaces the setup words the FIFO used
  to carry.
- **The inter-core FIFOs**, one in each direction, which carry 32-bit words: the
  browser's commands to core 1, and the LED, keyboard and uptime to core 0.

### The program

A program is a list of **signals** and a list of **nodes** (`hw_program` in
`src/program.h`). Core 0 compiles it from a config's payload (`program_compile` in
`src/program.c`), at boot and on each save.

#### Signals

A signal is the value of one output of a source: a `float`, in the source's own unit.
Every source in the config has its signals, wired or not:

| Source | Signals | Value | Starts at |
|---|---|---|---|
| `10` button | 1 | 0 off, 1 on | 0 |
| `12` slider | 1 | 0 to 1 | 0 |
| `13` 2D slider | 2: X (output 0), Y (output 1) | 0 to 1 each | its [home](#2d-sliders) |
| `30` core temperature | 1 | °C | the latest temperature |
| `40`–`4D` [logic](#logic) | 1; a counter 2: done (output 0), its count (output 1) | 0 off, 1 on; a scale, min, max or sine its value; a counter's count | 0, then its state at the first scan |
| `31` GPIO input | 1 | 0 off, 1 on | the pin when it is set up |
| `32` ADC | 1 | volts, 0 to 3.3 | 0, then its first reading |
| `33` DS18B20 | 2: its temperature (output 0), found (output 1) | °C; 0 or 1 | 0, then its first reading; as in the program core 1 ran |
| `34` rotary encoder | 2: its position (output 0), pressed (output 1) | clicks, 0 to its steps; 0 or 1 | its position as in the program core 1 ran, within its steps, or 0; pressed 0 |
| `35` clock | 3, in a row: hour (output 0), minute (1), second (2) | whole numbers, 0 to 23; 0 to 59 | 0, then the clock at the first scan |

- **The browser sets a UI source's** (button, slider, 2D slider) with a
  [command](#01-command): each byte becomes byte ÷ 255.
- **Core 1 sets a hardware input's** from the hardware, and a logic element's from
  its inputs, each scan.
- **A new program keeps the UI sources' values,** a DS18B20's and a rotary encoder's
  position: a signal of the same element and output in the program core 1 ran takes its
  value, so saving the graph doesn't move a slider's outputs or a knob's position, or
  drop a reading until the next. A source new to the graph starts as in the table.
- **Core 0 reads every signal** for telemetry (see
  [Telemetry text frames](#telemetry-text-frames)).

#### Nodes

A node is an element core 1 runs. A program lists the **inputs** first, in the order
of the config; then the **logic**, each after the logic it reads; then the
**outputs**, in the order of the config:

| Node | Reads | Writes | Does |
|---|---|---|---|
| `32` ADC | its channel | its signal | See [ADC inputs](#adc-inputs) |
| `31` GPIO input | its pin | its signal | See [GPIO inputs](#gpio-inputs) |
| `30` core temperature | the chip's sensor | its signal | See [Core temperature](#core-temperature) |
| `33` DS18B20 | its pin, 1-Wire | its two signals: its temperature, found | See [DS18B20 temperature sensors](#ds18b20-temperature-sensors) |
| `34` rotary encoder | its A, B and switch pins | its two signals: its position, pressed | See [Rotary encoders](#rotary-encoders) |
| `35` clock | the device's clock | its three signals: hour, minute, second | See [Clocks](#clocks) |
| `40` compare | 1 signal | its signal | See [Logic](#logic) |
| `41` AND, `42` OR | 1 to 4 signals | its signal | See [Logic](#logic) |
| `43` NOT, `44` delay, `48` pulse, `49` blink, `4A` toggle | 1 signal | its signal | See [Logic](#logic) |
| `45` scale | 1 signal | its signal | See [Logic](#logic) |
| `46` min, `47` max | 1 to 4 signals | its signal | See [Logic](#logic) |
| `4B` latch | 2 signals: set, reset | its signal | See [Logic](#logic) |
| `4C` counter | 2 signals: count, reset | its two signals: done, its count | See [Logic](#logic) |
| `4D` sine | 1 signal | its signal | See [Sine waves](#sine-waves) |
| `20` LED | 1 signal | the onboard LED, through core 0 | On from 0.5 |
| `21` GPO | 1 signal | its pin | High from 0.5 |
| `22` PWM | 2 signals: duty, frequency | its pin | See [PWM outputs](#pwm-outputs) |
| `23` USB keyboard | 1 signal | a keystroke, through core 0 | Types when its signal goes from below 0.5 to 0.5 or up |
| `24` stepper | 1 signal | its four pins | See [Stepper outputs](#stepper-outputs) |
| `25` servo | 1 signal | its pin, a 50 Hz pulse | See [Servos](#servos) |
| `26` NeoPixels | 2 signals: its value, on/off; in a chain, the node on its data line in | its pin, through a PIO state machine; in a chain, only the last | See [NeoPixels](#neopixels) |

- **An output reads the signals its source references name.** A reference to an
  element that isn't in the program, or to nothing (ID 0), reads 0. A reference to
  an output of a [block's](#blocks) copy reads what the copy's table says it stands for.
- **Logic in order.** Of the logic nodes, the first in the config that reads no
  logic still to be placed comes next; so each comes after the logic it reads, and
  otherwise they keep the config's order. A [feedback wire](#loops) doesn't count:
  the logic it goes into comes first, and reads it as the last scan left it. In a
  loop with no feedback wire, the first of its nodes left comes first and reads the
  others' signals as the last scan left them.
- **Left out:** the elements core 1 doesn't run (plots, lights, labels, positions,
  wires, networking), messages of the wrong length (the [payloads](#control-payloads)
  give the lengths), an ADC channel over 2, a DS18B20 on a pin that isn't usable or
  with an unknown option, a rotary encoder `encoder_payload_valid` refuses (see
  [Rotary encoders](#rotary-encoders)), a compare with a threshold or hysteresis
  that isn't a finite number or a hysteresis below 0, a delay, pulse or blink time out
  of its range, a scale whose ranges aren't finite numbers or whose input range is
  empty, a counter preset out of its range, a stepper `stepper_payload_read` refuses
  (see [Stepper motors](#stepper-motors)), a servo `servo_payload_read` refuses
  (see [Servos](#servos)) or NeoPixel node `neopixel_payload_read` refuses (see
  [NeoPixels](#neopixels)), and anything over the limits:
  `PROG_SIGNALS_MAX` (96) signals and `PROG_NODES_MAX` (64) nodes.
- **A repeated element ID**, as in configs from older UIs, keeps one signal, whose
  last message wins.

#### Program examples

A program can be written as text, one signal or node a line (`program_describe`):
`sN` is signal N, `->` the signal an input writes, `<-` the signal an output reads,
`-` none. A PWM's `/ F` is the value of that signal that gives its max (see
[PWM outputs](#pwm-outputs)). A delay's delays are in milliseconds. An input marked
`'` comes through a [feedback wire](#loops). Signals show the value they start at.

The program for the [save-config example](#the-graph). The button and slider are
signals only; the LED and GPO read the button's, the PWM's duty the slider's:

```program example=save-config
s0 n1.0 button = 0
s1 n5.0 slider = 0
LED n2 <- s0
GPO n3 pin 1 <- s0
PWM n4 pin 2 options 01 duty 50 0-100 <- s1 / 1 freq 1000 100-10000 <- - / 1
```

For the [adc-config example](#adc-inputs): the plot is left out, and the PWM takes
the ADC's volts as a fraction of 3.3:

```program example=adc-config
s0 n1.0 adc = 0
ADC n1 channel 1 -> s0
PWM n2 pin 2 options 01 duty 50 0-100 <- s0 / 3.3 freq 1000 100-10000 <- - / 1
```

For the [core-config example](#core-temperature): the PWM takes the temperature as a
fraction of 100 °C:

```program example=core-config
s0 n1.0 core = 0
CORE n1 -> s0
PWM n2 pin 2 options 01 duty 50 0-100 <- s0 / 100 freq 1000 100-10000 <- - / 1
```

For the [gpi-config example](#gpio-inputs): GPIO 3, options `05`, pull up and on
while low; the light is left out:

```program example=gpi-config
s0 n1.0 gpi = 0
GPI n1 pin 3 options 05 -> s0
LED n2 <- s0
```

For the [ds18b20-config example](#ds18b20-temperature-sensors): its pin and
resolution (and ` pull-up` with `DS18B20_PULLUP`), its temperature's signal, and
`found` its found's:

```program example=ds18b20-config
s0 n1.0 ds18b20 = 0
s1 n1.1 ds18b20 = 0
s2 n2.0 compare = 0
DS18B20 n1 pin 15 12-bit -> s0 found s1
COMPARE n2 above 30 hysteresis 1 <- s0 -> s2
GPO n3 pin 1 <- s2
LED n4 <- s1
```

For the [encoder-config example](#rotary-encoders): its A and B pins, its switch's
(`no button` without one), its transitions a click, its steps (and ` reverse`, ` wrap`
with those options), its position's signal, and `pressed` its pressed's. The servo
takes its position as a fraction of its 20 steps:

```program example=encoder-config
s0 n1.0 encoder = 0
s1 n1.1 encoder = 0
ENCODER n1 pins 6 7 button 8 4/click steps 20 -> s0 pressed s1
SERVO n2 pin 9 angle 0-180 pulse 500-2500 us speed 0 <- s0 / 20
LED n3 <- s1
```

For the [slider-2d-config example](#2d-sliders): X and Y start at the slider's home,
`40` and `BF` (0.251 and 0.749), and drive the PWM's duty and frequency:

```program example=slider-2d-config
s0 n1.0 slider-2d = 0.251
s1 n1.1 slider-2d = 0.749
PWM n2 pin 2 options 03 duty 50 0-100 <- s0 / 1 freq 1000 100-10000 <- s1 / 1
```

For the [keyboard-config example](#usb-keyboards): core 0 keeps the keystrokes; each
keyboard node reads its button:

```program example=keyboard-config
s0 n1.0 button = 0
s1 n3.0 button = 0
KEYBOARD n2 <- s0
KEYBOARD n4 <- s1
```

For the [stepper-config example](#stepper-motors): its pins, drive, full steps a
revolution, mode and scale, top speed and acceleration, then `reverse` or `release`
for those options:

```program example=stepper-config
s0 n1.0 slider = 0
STEPPER n2 pins 10 11 12 13 half 2048 steps position 1 rev/unit max 15 rpm accel 30 <- s0
```

For the [servo-config example](#servos): its pin, angles, pulses and speed limit, and
`/ F` the value of its signal that gives its angle at 1, as a PWM's:

```program example=servo-config
s0 n1.0 slider = 0
SERVO n2 pin 14 angle 0-180 pulse 500-2400 us speed 90 <- s0 / 1
```

For the [NeoPixel examples](#neopixels): its pin, layout and size, brightness, its
graphic (a bar's row, column or ring, or a point's or patch's start and end, from 0 as in the
payload), its colour as red, green, blue in hex (and its colour at 1, if it follows its
input), `/ F` the value of its signal that gives a full bar, and the signal that
switches it on and off:

```program example=neopixel-config
s0 n1.0 slider = 0
NEOPIXEL n2 pin 16 array 8x8 brightness 40 bar row 0 colour 00FF00 <- s0 / 1 on <- -
```

```program example=neopixel-point-config
s0 n1.0 slider = 0
s1 n3.0 button = 0
NEOPIXEL n2 pin 16 doughnut 24 brightness 40 point 0,0-0,0 colour 00FF00-FF0000 <- s0 / 1 on <- s1
```

```program example=neopixel-patch-config
s0 n1.0 slider = 0
NEOPIXEL n2 pin 16 array 8x8 brightness 40 patch 0,0-1,7 colour 0000FF-FF0000 <- s0 / 1 on <- -
```

In a [chain](#neopixel-chains), ` over nI` names the node it draws over, and ` feeds`
marks one whose data line out goes to another, which draws over it: it doesn't drive
its pin.

```program example=neopixel-chain-config
s0 n1.0 slider = 0
NEOPIXEL n2 pin 16 doughnut 12 brightness 40 patch 0,0-0,11 colour 0000FF <- - / 1 on <- - feeds
NEOPIXEL n3 pin 16 doughnut 12 brightness 40 point 0,0-0,0 colour FF0000 <- s0 / 1 on <- - over n2
```

For the [logic-config example](#logic): the compare reads the temperature and writes
its own signal, which the GPO reads:

```program example=logic-config
s0 n1.0 core = 0
s1 n2.0 compare = 0
CORE n1 -> s0
COMPARE n2 above 40 hysteresis 1 <- s0 -> s1
GPO n3 pin 5 <- s1
```

For the [logic-chain-config example](#logic): the compare, AND and delay run in the
order they read each other, not the config's, then the NOT:

```program example=logic-chain-config
s0 n1.0 core = 0
s1 n2.0 gpi = 0
s2 n5.0 delay = 0
s3 n3.0 compare = 0
s4 n4.0 and = 0
s5 n7.0 not = 0
CORE n1 -> s0
GPI n2 pin 3 options 02 -> s1
COMPARE n3 above 40 hysteresis 1 <- s0 -> s3
AND n4 <- s3 s1 -> s4
DELAY n5 on 0 off 5000 <- s4 -> s2
NOT n7 <- s2 -> s5
GPO n6 pin 5 <- s2
LED n8 <- s5
```

For the [logic-more-config example](#logic): the scale, compare, latch and blink, then
the PWM, which takes the scale's output as a fraction of 1, and the LED:

```program example=logic-more-config
s0 n1.0 core = 0
s1 n2.0 scale = 0
s2 n4.0 compare = 0
s3 n5.0 button = 0
s4 n6.0 latch = 0
s5 n7.0 blink = 0
CORE n1 -> s0
SCALE n2 30-60 to 0-1 clamp <- s0 -> s1
COMPARE n4 above 70 hysteresis 0 <- s0 -> s2
LATCH n6 set s2 reset s3 -> s4
BLINK n7 on 250 off 250 <- s4 -> s5
PWM n3 pin 2 options 01 duty 50 0-100 <- s1 / 1 freq 1000 100-10000 <- - / 1
LED n8 <- s5
```

For the [logic-loop-config example](#loops): the latch comes first, and reads the
counter's done (`s3'`) as the last scan left it; the counter, which reads the blink
and the NOT, comes last and writes its done and its count:

```program example=logic-loop-config
s0 n1.0 button = 0
s1 n2.0 latch = 0
s2 n3.0 blink = 0
s3 n4.0 counter = 0
s4 n4.1 counter = 0
s5 n5.0 not = 0
LATCH n2 set s0 reset s3' -> s1
BLINK n3 on 250 off 250 <- s1 -> s2
NOT n5 <- s1 -> s5
COUNTER n4 preset 3 count s2 reset s5 -> s3 s4
LED n6 <- s2
```

For the [clock-config example](#clocks): its three signals, in a row, its hour, minute
and second; the servo takes its second as a fraction of 60:

```program example=clock-config
s0 n1.0 clock = 0
s1 n1.1 clock = 0
s2 n1.2 clock = 0
s3 n2.0 compare = 0
CLOCK n1 -> s0 s1 s2
COMPARE n2 above 17 hysteresis 0 <- s0 -> s3
GPO n3 pin 1 <- s3
SERVO n4 pin 9 angle 0-180 pulse 500-2500 us speed 0 <- s2 / 60
```

For the [sine-config example](#sine-waves): its period, amplitude, offset and phase, and the
signal that runs it (`-`: none, so it always runs):

```program example=sine-config
s0 n1.0 sine = 0
SINE n1 period 4000 amplitude 0.5 offset 0.5 phase 0 <- - -> s0
SERVO n2 pin 9 angle 0-180 pulse 500-2500 us speed 0 <- s0 / 1
```

For the [block-config example](#blocks): the definition and the copy are left out;
the part runs, and the LED, wired to the copy's output, reads it:

```program example=block-config
s0 n1.0 button = 0
s1 n10.0 toggle = 0
TOGGLE n10 <- s0 -> s1
LED n3 <- s1
```

For the [legacy-config example](#source-references): a reference saved without its
output reads output 0:

```program example=legacy-config
s0 n1.0 button = 0
LED n2 <- s0
```

### Handing core 1 a program

Core 0 keeps two program buffers with core 1 (`src/hwman.c`): core 1 runs one, and
core 0 compiles the next into the other.

```mermaid
sequenceDiagram
    participant C0 as Core 0
    participant C1 as Core 1
    Note over C0: program_next: waits until core 1<br/>has taken the last program
    Note over C0: compiles the config into<br/>the buffer core 1 doesn't run
    C0->>C1: program_commit: program_sent + 1 (shared RAM)
    Note over C1: at the top of its next scan
    Note over C1: carries over the UI sources' values,<br/>switches buffers
    C1->>C0: program_taken = program_sent
    Note over C1: sets up each node's hardware
```

- **Counters, not the FIFO.** `program_sent` and `program_taken` are in RAM; core 1
  looks at them at the top of its loop, so no word in the FIFO can be lost or misread.
- **One program in flight.** Core 0 compiles only once core 1 has taken the last
  program, so it never writes the buffer core 1 runs. While it waits it takes core 1's
  messages (see [Neither core waits](#core-1--core-0)). If core 1 hasn't taken the last
  one within 1 s, the config isn't run and core 0 logs
  `core 1 has not taken the last program: config not run`; it is still stored.
- **Core 0 logs each program** it compiles, as text, on the serial console.
- **USB keyboards:** core 0 keeps each keyboard node's keystroke as it compiles; core 1
  says when to type it.

### Core 1: the scan

Core 1 runs a scan every 10 ms (`SCAN_MS`), and between scans takes core 0's
[action words](#core-0--core-1-action-words).

1. **A new program,** if core 0 has committed one: core 1 takes it (above), turns off
   every pin the last program set up, making them inputs again, and sets up each
   node's hardware in program order, inputs first:
   - an ADC takes its channel's pin, GPIO 26 + channel (several ADC nodes may share a
     channel);
   - a GPIO input takes its pin, sets its pull, and reads it for its signal;
   - a DS18B20 takes its pin as an input with its output level 0, its pull-up on or
     off, and is configured at its first scan;
   - a rotary encoder takes its two or three pins, all or none, as inputs with their
     pull-ups on, and core 1 takes the edges of A and B with an interrupt;
   - a GPO takes its pin as an output; a PWM takes its pin, and starts its slice at
     the first scan; a servo takes its pin, and starts its slice at 50 Hz at the
     first scan; a stepper takes its four pins as outputs, and holds its coils
     from the first scan (unless it releases them); NeoPixels take their pin, a PIO
     state machine and a DMA channel (see [NeoPixels](#neopixels)), except a NeoPixel
     node whose data line out feeds another, which takes nothing;
   - an LED, keyboard, core temperature, clock or logic takes no pin.

   A node whose pin isn't [usable](#gpio-pins), or that an earlier node took, is
   skipped, and logged. Other pins are left alone: on the Pico 2 W, GPIO 23, 24, 25
   and 29 drive the WiFi chip.
2. **The inputs**, in program order, write their signals:
   - **ADC:** each channel with an ADC node is read 16 times and averaged. Its value
     changes when the reading has moved 12 counts (about 10 mV) from it, so noise
     doesn't flicker the outputs; each of its ADC nodes' signals is that reading in
     volts, reading × 3.3 / 4095.
   - **GPIO input:** its pin is read, inverted with `GPI_INVERT`, with its input
     buffer on only for the read (erratum RP2350-E9, see [GPIO inputs](#gpio-inputs)).
     A state that differs for 3 reads in a row becomes its signal.
   - **Core temperature:** every 10th scan (100 ms) the sensor is read and smoothed
     (see [Core temperature](#core-temperature)); each core temperature signal is the
     temperature once it has moved 0.1 °C from it.
   - **DS18B20:** one step of reading it, about 2 ms at most: a command, a wait, or 3
     bytes of its scratchpad; a reading checked good becomes its signals (see
     [DS18B20 temperature sensors](#ds18b20-temperature-sensors)).
   - **Clock:** its three signals are the hour, minute and second of the device's
     clock (see [Clocks](#clocks)).
   - **Rotary encoder:** the clicks its interrupt counted since the last scan move its
     position; its switch is read, and a state that differs for 3 reads in a row
     becomes pressed (see [Rotary encoders](#rotary-encoders)).
3. **The logic**, in program order, writes its signals from the signals it reads
   (see [Logic](#logic)).
4. **The outputs**, in program order, each from the signals it reads, when the state
   it would take changes, and at its first scan:
   - **LED:** on from 0.5; core 1 sends core 0 an LED tag (below).
   - **GPO:** high from 0.5.
   - **USB keyboard:** when its signal goes from below 0.5 to 0.5 or up, core 1 sends
     core 0 a keyboard tag (below). A program starts it with its signal as it is, so a
     button left on across a save doesn't type.
   - **PWM:** see below.
   - **Servo:** its angle moves towards the one its signal asks for, by at most its
     speed limit × 10 ms, and its pulse follows (see [Servos](#servos)). The pulse is
     set as the slice's level: its counter runs 20 ms a period, so the level is
     about 0.3 µs a step.
   - **Stepper:** takes its signal as its target or speed; it moves between scans
     (below).
   - **NeoPixels:** their graphic is drawn for their signal's fraction of full scale
     (all off while their on/off signal is below 0.5),
     and DMA sends it out; a node whose data line out feeds another does nothing, and
     the last node of a [chain](#neopixel-chains) draws each node of it in turn, from the
     first, over the last (see [NeoPixels](#neopixels)); if the last send isn't out
     yet, a later scan sends it.

Inputs come before logic, and logic before outputs, so an output follows its input,
through any logic, in the same scan: within 10 ms of the pin, reading or command.

#### PWM outputs

A PWM's duty cycle and frequency are each constant, or follow a signal (its
`PWM_DUTY_INPUT` and `PWM_FREQ_INPUT` options; see [Control payloads](#control-payloads)):

- **The fraction.** A parameter that follows a signal takes it as a fraction of the
  source's full scale, clamped to 0–1: the signal ÷ 1 for a button, slider or 2D
  slider, ÷ 3.3 for an ADC's volts, ÷ `CORE_TEMP_FULL` (100) for the core
  temperature's °C. A signal wired to nothing reads 0, so the parameter is at its min.
- **Duty:** min + (max − min) × fraction, in percent; min may be above max, which
  inverts the input.
- **Frequency:** min + (max − min) × fraction, or with `PWM_FREQ_LOG` min × (max /
  min)^fraction, rounded to a whole Hz.
- **The clock:** the clock divider is the smallest whole number that fits one period
  in the 16-bit counter, so the duty cycle has at least ~2300 steps (at 65535 Hz) and
  the slowest frequency is ~9 Hz. A change of frequency reprograms the slice and keeps
  the duty cycle's percentage.

#### Stepper outputs

A stepper steps faster than the scan: a half-stepped 28BYJ-48 at 15 RPM takes a step
about every millisecond. So the scan only hands each stepper its signal, and core 1
moves it in its loop (`steppers_run` in `src/hwman.c`; the motion is `src/stepper.c`):

- **When it is due.** While a stepper moves, core 1 waits for core 0's words only
  until its next step is due, or 1 ms (`STEPPER_TICK_S`) if that is sooner, instead of
  until the next scan. A still stepper costs nothing between scans.
- **The motion.** At each call its speed moves towards the one it wants by its
  acceleration × the time since the last call (at once without one), and its
  progress towards the next step by its speed × that time; a whole step steps it,
  and its coils take the next state of the drive's cycle. A late call takes one step,
  not several. In Position it wants its top speed towards its target, but no more
  than √(2 × acceleration × steps left), so it slows down to stop there. It is still
  once it is at its target (Position) or wants no speed (Speed), and slow enough to
  stop at once: then core 1 stops calling, and turns its coils off with
  `STEPPER_RELEASE`.
- **A new program** keeps each stepper's place, in revolutions, for a stepper with
  the same ID: its new position is that many revolutions in its new drive and steps
  per revolution. It starts still. Its pins are let go and set up again, so it may
  lose a step if it was moving.
- **A pause for a flash write** stops it where it is; once core 1 resumes, it carries
  on as if the pause had been 50 ms.

### Pausing core 1 for flash writes

Core 1 executes from flash, so it must not run while core 0 erases or programs flash
(saving a config, `0B`/`0C`). Every flash write goes through `program_region`, which
pauses core 1 first:

```mermaid
sequenceDiagram
    participant C0 as Core 0
    participant C1 as Core 1
    C0->>C1: park_request = true (shared RAM)
    Note over C1: at the top of its loop, or while waiting<br/>for room in core 0's FIFO
    C1->>C0: parked = true
    Note over C1: core1_park: runs from RAM,<br/>interrupts off, waits (WFE)
    Note over C0: interrupts off, erase + program, interrupts on
    C0->>C1: park_request = false, SEV
    C1->>C0: parked = false
```

- The handshake uses two flags in RAM, not the FIFO. The SDK's
  `flash_safe_execute` pauses the other core through the FIFO, and its handler
  would consume the protocol's words.
- Core 1 checks for a request at the top of its loop, where it holds no locks.
  It also checks while it waits for room in core 0's FIFO: core 0 doesn't drain that
  FIFO while it waits, so without this check the two cores could deadlock.
- If core 1 hasn't parked within 1 s, nothing is written and core 0 logs
  `core 1 did not pause, flash not written`.
- Before core 1 is launched, for example when the first-boot config is written, no
  pause is needed.
- After a pause core 1's scans start again from now: the outputs hold their state
  while it is parked.

### Core 0 → core 1: action words

Core 0 sends core 1 one kind of word, the **action word**: the new value of one output
of a UI source, from a [command](#01-command).

```text
 31 30 29 28       24 23                      8 7             0
+-----+--+-----------+-------------------------+---------------+
| op  |--|  output   |       element ID        |     value     |
+-----+--+-----------+-------------------------+---------------+
```

- **op** is 1 (`FIFO_OP_CONTROL_ACTION`); 2 and 3, the setup and clean setup words
  of earlier firmware, are retired.
- **output** is the element's output, 0 to `FIFO_OUTPUTS_MAX` − 1 (31); **value**,
  `00`–`FF`, its new value. `fifo_action_word`, `fifo_op_output`, `fifo_op_id` and
  `fifo_op_value` in `src/protocol.h` build and read it.
- **Core 1** sets that output's [signal](#signals) to value ÷ 255, if the program it
  runs has it and it is a UI source's (a button's, slider's or 2D slider's); it logs
  any other.

The action word for the [command-button example](#01-command):

```fifo example=command-button
400001FF                    # action, output 0 of element n1, value FF
```

for the [command-slider example](#01-command):

```fifo example=command-slider
400004BF                    # action, output 0 of element n4, value BF
```

and for the [command-slider-2d example](#01-command), one per output:

```fifo example=command-slider-2d
400006FF                    # action, output 0 (X) of element n6, value FF
41000680                    # action, output 1 (Y) of element n6, value 80
```

### Core 1 → core 0

Each message is two words: a **tag**, then a **value**.

| Tag | Constant | Value | Core 0 does |
|---|---|---|---|
| 0 | `FIFO_TAG_UPTIME` | Uptime in 1.2 s ticks | Sends text frame `uptime N` |
| 2 | `FIFO_TAG_LED` | 0 or 1 | Sets the onboard LED |
| 4 | `FIFO_TAG_KEYBOARD` | The keyboard's element ID | Types its keystroke (see [USB keyboards](#usb-keyboards)) |

Tags 1, 3 and 5, the temperature, ADC and GPIO input reports of earlier firmware, are
retired: those values are signals, which core 0 reads from the program.

Uptime is first sent at tick 5, then every 5 ticks. Core 0 takes every waiting
message each pass of its main loop, which waits at most 10 ms for network work.

**Neither core waits on the other forever.** Each FIFO holds only a few words, and a
core sending to a full one waits for room. If core 0 waited to send an action word
while core 1 waited to send an LED tag, neither would take the other's words. So:

- **Core 1**, while it waits for room in core 0's FIFO, answers a request to pause
  for a flash write (see [Pausing core 1](#pausing-core-1-for-flash-writes)).
- **Core 0** sends every word through `push_to_core1` (`src/hwman.c`). While core
  1's FIFO is full, or while it waits for core 1 to take a program, it takes core 1's
  messages and keeps them, in order, for its main loop (`take_from_core1`), so core 1
  can go on. It keeps up to 64 messages; more are dropped and logged.

---

## Known issues

Current behaviour that is probably not intended. Each one is a candidate for its own fix.

**Transport**

1. **Only the latest connection is tracked.** Every new TCP connection takes
   `clients[0]`, so only the most recent client receives telemetry.
2. **`0B`/`0C` fail silently** when nothing can be changed (see
   [`0B`](#0b-update-config-field-0c-update-secret-field)), and the UI has no way to tell.

**Config and graph**

3. **Unknown control types are lost.** Control types the UI doesn't know are skipped
   on restore and dropped on the next save.
4. **The button config payload holds only its action.** The earlier reference
   described a 7-byte payload (label ID, X, Y, style) that was never implemented.
5. **Network IDs above 255 are misread in the UI.** `unmarshalConfigs` uses `||`
   instead of `|` to combine the ID bytes.
6. **Field values are copied without a length check.** The device copies names, SSIDs
   and passwords into 32-byte buffers using the message's length.
7. **The first-boot defaults are hardcoded:** SSID `LRX`, a fixed password, and
    options = join, so a fresh board tries to join that network rather than start
    its own access point.

**Flash**

8. **The secrets region is not sector aligned.** `flash_range_erase` works on whole
    4096-byte sectors at aligned offsets, but the secrets region is 256 bytes at
    4352 bytes from the end of flash, so erasing it is outside the SDK's contract.

**Core 0 ↔ core 1**

9. **Core 1's messages have no framing.** A tag and its value are told apart only by
    order, so one lost or extra word desyncs everything after it. (Core 0's action
    words are one word each; the program needs no words.)
10. **Saving the graph glitches its outputs.** Core 1 turns off every pin the last
    program set up before it sets up the new one, so a GPO that stays high goes low
    for up to one scan (10 ms), and a PWM restarts.
11. **Uptime counts 1.2 s ticks**, not seconds.

**Hardware**

12. **A config can still put two PWMs on one slice.** The UI offers a PWM no pin on
    another PWM's slice (see [GPIO pins](#gpio-pins)), but a config saved before it did
    is restored as it was. Both PWMs then run at the frequency of the one set up last,
    and two on the same channel (GPIO 0 and 16) at the same duty cycle too.
13. **A config can still give two nodes one pin.** The UI offers each node only free
    pins, but a config saved before it did is restored as it was. Core 1 sets up the
    inputs, then the outputs, and the first node on a pin wins; it skips the others
    (it logs each), and the UI doesn't flag them; picking another pin for one fixes it.
14. **ADC readings carry power-supply noise.** The Pico 2 W's switching regulator
    runs in its power-saving mode, which adds ripple to the ADC's 3.3 V reference.
    Its quieter PWM mode is set through the WiFi chip (`WL_GPIO1`, the regulator's PS
    pin), and the firmware doesn't set it. Core 1's averaging and hysteresis hide most
    of the noise, at the cost of resolution.

**USB keyboard**

15. **Text is typed for a US layout.** The device sends keys, not characters, so a PC
    set to another layout types some characters differently (`y` and `z` on German,
    most punctuation on French).
16. **Caps Lock inverts typed letters.** Text is typed with Shift for capitals, so
    with Caps Lock on the PC, the case of each letter is swapped.
17. **The USB IDs are TinyUSB's test IDs** (vendor `CAFE`, product `4005`). They are
    fine on your own PC; a board for others needs IDs of its own.


**Control**

18. **A browser that connects doesn't see the device's control state.** The device
    keeps its outputs as they are when a browser connects, but the UI starts its
    controls from the config: a toggle button off, a slider at 0, a 2D slider at its
    home. An output driven by a control shows the control's state again once the
    control is operated. Core 0 sends the controls' signals (`sig`), but the UI
    doesn't use them yet.

---

## Keeping this document honest

- **Constants** live in [`src/protocol.h`](src/protocol.h) and
  [`ui/content/protocol.js`](ui/content/protocol.js). `make test` fails if a `PKT_`,
  `MSG_`, `FIELD_`, `NET_OPT_`, `GPI_` or other mirrored constant is missing from either
  file or has a different value.
- **Examples.** `make test` extracts every `hex example=<name>`,
  `fifo example=<name>` and `program example=<name>` block from this file into
  `tests/protocol_examples.h` (`make/examples.awk`). `tests/test_protocol.c` then
  checks:
  - every packet example is well formed (known packet type, the length field matches,
    the messages fill the payload exactly, flags are 0 or `MSG_FLAG_OUTPUT`);
  - the specific contents of the examples above;
  - each `fifo` example matches the action words `src/protocol.h` builds from the
    command example of the same name;
  - each `program` example is what `program_compile` (`src/program.c`) makes of the
    config example of the same name, and every config example core 1 would run
    something for has one.

  A new packet example gets the structural checks automatically.
- **The UI's real packets.** `make ui-test` loads the page the firmware serves in a
  headless browser, with a fake device in place of the WebSocket. It connects, edits
  and saves the graph, presses a button, drags a 2D slider, edits every network field and reconnects,
  builds and saves a graph of logic and restores it,
  then checks every packet the UI sent with the firmware's own code
  (`tests/check_packets.c`): the length check, the message iterator, the lengths
  `unmarshal_controls` accepts, and the field replacement behind `0B`/`0C`.

# neoGraph Nodes

Every node you can add to the graph: where it is in the add menu, what it does, and
its settings. [PROTOCOL.md](PROTOCOL.md) has the bytes behind each one.

## Finding a node

The **+** button in the Graph view opens the six groups. A group opens its nodes
under it. In this document, a node's place is written as `GROUP-BUTTON`: `HWI-TMP` is
the TMP button in the HWI group.

| Group | Category | Nodes |
|---|---|---|
| **UII** | UI inputs: controls you operate in the browser | BTN, SLD, XY |
| **HWO** | Hardware outputs: what the device drives | LED, GPO, PWM |
| **HWI** | Hardware inputs: what the device reads | TMP, ADC, GPI, LM35 |
| **UIO** | UI outputs: what the browser shows | PLT, LGT |
| **USB** | USB: the device acting as a USB device | KEY |
| **LGC** | Logic: decisions the device makes | CMP, AND, OR, NOT, DLY, SCL, MIN, MAX, PLS, BLK, TGL, LCH |

## Things every node shares

- **Label.** Every node has a Label setting. Its default is listed with each node
  below. It names the node in the graph and in the Command view.
- **ID.** Each node shows its ID (`n5`) under its label.
- **Wires carry one of two kinds of value:**
  - **On/off** (boolean): off is 0, on is 1. An input counts as on from 0.5.
  - **Number** (floating): a value in its own unit, such as °C, volts, or 0–1 for a
    slider.

  A wire only joins outputs and inputs of the same kind.
- **Command view tile.** A hardware input or logic node has a "Command view: Show |
  Hide" setting, Hide by default. Show puts a tile with its live value in the Command
  view.
- **The device runs the graph.** Once you save, the device runs everything except the
  UI outputs every 10 ms, with or without a browser connected. It also runs the
  stored graph from power-on.
- **Logic also runs in the browser** whenever the device isn't reporting a logic node:
  with no device connected, before you save, or if the device goes quiet. A
  **dashed** dot on a logic node means the browser worked its state out; a **solid**
  dot means the device did. The browser never drives hardware outputs.

---

## UII: UI inputs

Controls in the Command view. You operate them in the browser; each change is sent
to the device, which acts on it.

### Button: `UII-BTN`

| | |
|---|---|
| Category | UI input |
| Output | On/off |
| Default label | BUTTON |

A button tile in the Command view. Click, tap, or press Space or Enter on it.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Action | Toggle, Momentary | Toggle | **Toggle:** each click turns it on or off. **Momentary:** on only while held down. Use Momentary for a USB keyboard, a pulse, a toggle node or a latch reset. |

### Slider: `UII-SLD`

| | |
|---|---|
| Category | UI input |
| Output | Number, 0 to 1 |
| Default label | SLIDER |

A slider in the Command view. It sends its value each time it moves.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Orientation | Horizontal, Vertical | Horizontal | How it is drawn in the Command view. |

### 2D slider: `UII-XY`

| | |
|---|---|
| Category | UI input |
| Outputs | Two numbers, 0 to 1: **X** (left to right) and **Y** (bottom to top) |
| Default label | 2D slider |

A square pad with a puck, like a joystick. Drag the puck, or use the arrow keys
(Shift moves ten steps). Each wire leaves from X or Y and is labelled with it.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Wire from | X, Y | X | Which output the next wire you draw leaves from. Once a wire leaves one output, this switches to the other. |
| Home X, Home Y | 0 to 1 | 0.5, 0.5 | Where the puck starts, and where it returns to. Its outputs start here at power-on too. |
| When let go | Stay, Return home | Stay | Whether the puck springs back home when released. |
| Return | Immediate, Linear, Sigmoid | Linear | With Return home: jump straight home, glide at a steady speed, or speed up then slow down. |
| Speed (%/s) | 1 to 255 | 100 | With a Linear or Sigmoid return: how fast, in % of the pad per second. |

---

## HWO: Hardware outputs

Things the device drives. Only the device drives them; the browser never does.

### Onboard LED: `HWO-LED`

| | |
|---|---|
| Category | Hardware output |
| Input | On/off |
| Default label | Onboard LED |

The Pico 2 W's onboard LED, on while its input is on.

No settings beyond Label.

### GPIO output: `HWO-GPO`

| | |
|---|---|
| Category | Hardware output |
| Input | On/off |
| Default label | GPO |

A header pin, high (3.3 V) while its input is on and low while it is off.

| Setting | Values | Default | What it does |
|---|---|---|---|
| GPIO pin | GPIO 0–22, 26–28, not used by another node | 1, or the lowest free pin | The pin it drives. |

### PWM output: `HWO-PWM`

| | |
|---|---|
| Category | Hardware output |
| Inputs | Up to two numbers: one for the duty cycle, one for the frequency |
| Default label | PWM |

A pulse-width-modulated pin, for a motor, fan, dimmable LED or buzzer. Its duty cycle
and its frequency are each either constant or follow an input.

When a parameter follows an input, the input's value is taken as a fraction of the
source's full scale: 1 for a slider or scale, 3.3 V for an ADC, 100 °C for the core
temperature, and for a min or max, the full scale of its first input. A fraction of 0
gives the "at 0" value and 1 gives the "at 1" value. The first wire you draw in sets
the duty cycle; the second sets the frequency.

| Setting | Values | Default | What it does |
|---|---|---|---|
| GPIO pin | GPIO 0–22, 26–28; not on another PWM's slice | 2, or a free pin on a free slice | The pin it drives. Pins sharing a PWM slice share its frequency, so only one PWM is allowed per slice. |
| Frequency | Constant, From input | Constant | Whether the frequency is fixed or follows an input. |
| Frequency (Hz) | 10–65535 | 1000 | With Constant: the frequency. |
| Input | A number source, or None | None | With From input: the source the frequency follows. Picking one draws its wire. |
| Frequency at 0 (Hz), at 1 (Hz) | 10–65535 each | 100, 10000 | With From input: the frequency at the bottom and top of the input. "At 0" may be above "at 1", which turns the input around. |
| Scale | Linear, Logarithmic | Linear | With From input: Logarithmic gives each step of the input the same ratio, so 100–1000 Hz takes as much of a slider as 1–10 kHz. |
| Duty cycle | Constant, From input | Constant | Whether the duty cycle is fixed or follows an input. |
| Duty cycle (%) | 0–100 | 50 | With Constant: the duty cycle. |
| Input | A number source, or None | None | With From input: the source the duty cycle follows. |
| Duty cycle at 0 (%), at 1 (%) | 0–100 each | 0, 100 | With From input: the duty cycle at the bottom and top of the input. |

---

## HWI: Hardware inputs

What the device reads. The device uses the values directly, and also sends them to
the browser.

### Core temperature: `HWI-TMP`

| | |
|---|---|
| Category | Hardware input |
| Output | Number, °C |
| Default label | Core temperature |

The chip's own temperature sensor, read every 100 ms and smoothed over about a
second. It is accurate to a few °C. The offline demo simulates a temperature drifting
around 30 °C.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Command view | Show, Hide | Hide | A tile with the temperature. |

### ADC input: `HWI-ADC`

| | |
|---|---|
| Category | Hardware input |
| Output | Number, volts (0 to 3.3) |
| Default label | ADC |

An analog input pin, for a potentiometer or an analog sensor, read every 10 ms. Small
noise is filtered out so the outputs it drives don't flicker. The offline demo
simulates a slow wave between about 0.3 and 3 V.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Channel | ADC0 · GPIO 26, ADC1 · GPIO 27, ADC2 · GPIO 28 (those whose pin no other node uses) | ADC0, or the first free | The pin it reads. To use one reading in several places, wire one ADC node to all of them. |
| Command view | Show, Hide | Hide | A tile with the reading in volts. |

### GPIO input: `HWI-GPI`

| | |
|---|---|
| Category | Hardware input |
| Output | On/off |
| Default label | GPIO input |

A header pin read as on or off every 10 ms, for a switch or a digital sensor. A
change counts once it has held for 30 ms, so a bouncing switch changes it only once.
Drive the pin with 3.3 V at most. Not simulated in the offline demo.

| Setting | Values | Default | What it does |
|---|---|---|---|
| GPIO pin | GPIO 0–22, 26–28, not used by another node | 3, or the lowest free pin | The pin it reads. |
| Pull | None, Up, Down | Down | The pin's internal pull resistor. Down suits a switch to 3.3 V; Up suits a switch to ground. |
| On when | High, Low | High | Whether it is on while the pin is high or while it is low. Use Low with Pull Up and a switch to ground. |
| Command view | Show, Hide | Hide | A tile showing On or Off. |

### LM35 temperature sensor: `HWI-LM35`

| | |
|---|---|
| Category | Hardware input |
| Output | Number, °C |
| Default label | LM35 |

An LM35 (such as an LM35DZ) temperature sensor on an analog input pin. It puts out
10 mV per °C, which the device turns into °C. It is read every 100 ms and smoothed
over about a second, and is accurate to about ±1 °C. A PWM it drives takes 0 to
100 °C, the LM35DZ's range, as its input's 0 to full. The offline demo simulates a
room temperature drifting around 22 °C.

Wiring: the LM35's **+Vs** to **VBUS** (pin 40, 5 V from USB) or **VSYS** (pin 39) —
it needs 4 V or more, so not the 3.3 V pin — **GND** to a ground pin, and **Vout** to
the channel's GPIO. Its output is under 1.5 V, safe for the pin. Wired this way it
reads from about 2 °C up.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Channel | ADC0 · GPIO 26, ADC1 · GPIO 27, ADC2 · GPIO 28 (those whose pin no other node uses) | ADC0, or the first free | The pin its Vout is wired to. One LM35 node to a channel. |
| Command view | Show, Hide | Hide | A tile with the temperature. |

---

## UIO: UI outputs

What the browser shows. They are drawn in the Command view; the device doesn't run
them.

### Plot: `UIO-PLT`

| | |
|---|---|
| Category | UI output |
| Input | Number |
| Default label | Plot |

A live chart of its input over time, newest on the right. Point at it to read a
value. Its history lives in the page and is lost on reload.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Window (s) | 1–255 | 10 | How many seconds it shows. |
| Unit | Text, up to 16 bytes, or empty for Auto | Auto | The unit shown with values. Auto uses the source's unit (V, °C, or none for a slider). |

### Light: `UIO-LGT`

| | |
|---|---|
| Category | UI output |
| Input | On/off |
| Default label | Light |

A lamp in the Command view, green while its input is on, with On or Off under it. It
shows `–` while nothing is wired in.

No settings beyond Label.

---

## USB

### USB keyboard: `USB-KEY`

| | |
|---|---|
| Category | Hardware output (USB) |
| Input | On/off |
| Default label | Keyboard |

Plugged into a PC, the device is also a USB keyboard. This node types its keystroke
once each time its input goes on. Wire a momentary button, a GPIO input or logic to
it. Text is typed as on a US keyboard layout.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Types | Key, Text | Key | Whether it types a key combination or a line of text. |
| Hold | Ctrl, Shift, Alt, Win (each on or off) | None | With Key: the modifiers held while the key is pressed. |
| Key | Letters, digits, punctuation, F1–F24, Enter, Escape, arrows and more | Enter | With Key: the key pressed. |
| Text | Up to 64 characters; `\n` is Enter, `\t` is Tab | Hello | With Text: what it types. |

---

## LGC: Logic

Logic makes decisions on the device, every 10 ms, without the browser. Each logic
node reads one or more sources and is a source itself. Most output on/off; Scale,
Min and Max output numbers. A logic node is both: wires come into it, and leave it
for outputs or other logic. Between two logic nodes, a wire goes from the node you
picked first to the one you clicked. A wire that would make a loop isn't offered.

Every logic node has the **Command view** setting (Show, Hide; Hide by default),
which puts a tile with its state or value in the Command view. The tables below list
only each node's own settings.

When a graph starts (at power-on, and on each save), logic starts off or at 0: a
latch has to be set again, and a toggle starts off.

### Compare: `LGC-CMP`

| | |
|---|---|
| Category | Logic |
| Input | One number |
| Output | On/off |
| Default label | Compare |

On while its input is above (or below) a threshold. **Hysteresis** stops it
flickering near the threshold: "above 40, hysteresis 1" turns on above 40 and off
only below 39. The node shows its condition, for example `> 40`.

| Setting | Values | Default | What it does |
|---|---|---|---|
| On while | Above, Below | Above | Which side of the threshold turns it on. |
| Threshold | Any number, in the input's unit (shown in the label, such as °C) | 0.5 | Where it switches. |
| Hysteresis | 0 or more, in the input's unit | 0 | How far back past the threshold the input must go to switch it back. |

Example: core temperature → Compare (above 40, hysteresis 1) → GPO.

### AND: `LGC-AND`

| | |
|---|---|
| Category | Logic |
| Inputs | 1 to 4, on/off |
| Output | On/off |
| Default label | AND |

On while every wired input is on. With nothing wired in, off.

No settings of its own.

### OR: `LGC-OR`

| | |
|---|---|
| Category | Logic |
| Inputs | 1 to 4, on/off |
| Output | On/off |
| Default label | OR |

On while any wired input is on. With nothing wired in, off.

No settings of its own.

### NOT: `LGC-NOT`

| | |
|---|---|
| Category | Logic |
| Input | One, on/off |
| Output | On/off |
| Default label | NOT |

On while its input is off. With nothing wired in, on.

No settings of its own.

### Delay: `LGC-DLY`

| | |
|---|---|
| Category | Logic |
| Input | One, on/off |
| Output | On/off |
| Default label | Delay |

Follows its input, but only once the input has stayed in its new state for the on
delay (to turn on) or the off delay (to turn off). If the input changes back before
then, the count starts again, so short blips never get through. Use it to debounce,
or to keep a fan running for a while after its input goes off.

| Setting | Values | Default | What it does |
|---|---|---|---|
| On delay (s) | 0 to 86400, to the ms | 0 | How long the input must stay on before it turns on. 0 turns on at once. |
| Off delay (s) | 0 to 86400, to the ms | 0 | How long the input must stay off before it turns off. 0 turns off at once. |

### Scale: `LGC-SCL`

| | |
|---|---|
| Category | Logic |
| Input | One number |
| Output | Number |
| Default label | Scale |

Maps its input from one range onto another. With 30–60 onto 0–1, 45 °C gives 0.5.
Either range may run downwards, for example 0–1 onto 100–0. A PWM takes a scale's
output as a fraction of 1, so set the output range to 0–1 to drive a PWM.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Input from, Input to | Any numbers, in the input's unit; not equal | 0, 1 | The input range. |
| Output from, Output to | Any numbers | 0, 1 | The output range: "Input from" maps to "Output from", "Input to" to "Output to". |
| Clamp | Yes, No | Yes | Yes keeps the output within its range when the input goes outside its range. |

Example: core temperature → Scale (30–60 onto 0–1) → PWM duty: a fan that runs
faster as the chip gets hotter.

### Min: `LGC-MIN`

| | |
|---|---|
| Category | Logic |
| Inputs | 1 to 4 numbers |
| Output | Number, in the unit of its first input |
| Default label | Min |

The lowest of its wired inputs. With nothing wired in, 0.

No settings of its own.

### Max: `LGC-MAX`

| | |
|---|---|
| Category | Logic |
| Inputs | 1 to 4 numbers |
| Output | Number, in the unit of its first input |
| Default label | Max |

The highest of its wired inputs, such as the hottest of several readings. With
nothing wired in, 0.

No settings of its own.

### Pulse: `LGC-PLS`

| | |
|---|---|
| Category | Logic |
| Input | One, on/off |
| Output | On/off |
| Default label | Pulse |

Turns on for a set time each time its input goes on, however long the input stays
on. Going on again while it is on starts the time again. Use it to give a relay or
buzzer a fixed-length kick from a button press. An input that is already on when the
graph starts doesn't count as going on.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Time on (s) | 0.001 to 86400 | 1 | How long it stays on. |

### Blink: `LGC-BLK`

| | |
|---|---|
| Category | Logic |
| Input | One, on/off, or none |
| Output | On/off |
| Default label | Blink |

Flashes while its input is on: on for the time on, off for the time off, in turn,
starting on. When the input goes off, it stops and stays off. With nothing wired in,
it blinks all the time.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Time on (s) | 0.001 to 86400 | 0.5 | How long each flash is on. |
| Time off (s) | 0.001 to 86400 | 0.5 | How long it is off between flashes. |

### Toggle: `LGC-TGL`

| | |
|---|---|
| Category | Logic |
| Input | One, on/off |
| Output | On/off |
| Default label | Toggle |

Turns over (off to on, or on to off) each time its input goes on. Use it with a
momentary button or a push switch on a GPIO input to make a latching on/off switch.
An input that is already on when the graph starts doesn't count as going on.

No settings of its own.

### Latch: `LGC-LCH`

| | |
|---|---|
| Category | Logic |
| Inputs | Two, on/off: **Set** and **Reset** |
| Output | On/off |
| Default label | Latch |

Turns on when Set goes on, and stays on until Reset goes on. While Reset is on, it
is off whatever Set does. Use it for an alarm that stays on after its cause has gone
until someone resets it.

The first wire drawn in becomes Set and the second Reset. Pick them in the settings
to change that.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Set | An on/off source, or None | The first wire in | The source that turns it on. Picking one draws its wire. |
| Reset | An on/off source, or None | The second wire in | The source that turns it off. |

Example: core temperature → Compare (above 70) → Latch Set; momentary button →
Latch Reset; Latch → Blink → onboard LED. The LED flashes after an over-temperature
until the button is pressed.

---

## Which node takes which

| From ↓ / into → | LED, GPO, KEY, LGT | PWM, PLT | CMP, SCL, MIN, MAX | AND, OR, NOT, DLY, PLS, BLK, TGL, LCH |
|---|---|---|---|---|
| BTN, GPI | ✓ | | | ✓ |
| SLD, XY (X or Y), ADC, TMP, LM35 | | ✓ | ✓ | |
| CMP, AND, OR, NOT, DLY, PLS, BLK, TGL, LCH | ✓ | | | ✓ |
| SCL, MIN, MAX | | ✓ | ✓ | |

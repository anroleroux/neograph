# neoGraph Nodes

Every node you can add to the graph: where it is in the add menu, what it does, and
its settings. [PROTOCOL.md](PROTOCOL.md) has the bytes behind each one.

## Finding a node

The **+** button in the Graph view opens the eight groups. A group opens its nodes
under it. In this document, a node's place is written as `GROUP-BUTTON`: `HWI-TMP` is
the TMP button in the HWI group.

| Group | Category | Nodes |
|---|---|---|
| **UII** | UI inputs: controls you operate in the browser | BTN, SLD, XY |
| **HWO** | Hardware outputs: what the device drives | LED, GPO, PWM |
| **HWI** | Hardware inputs: what the device reads | TMP, ADC, GPI |
| **UIO** | UI outputs: what the browser shows | PLT, LGT |
| **USB** | USB: the device acting as a USB device | KEY |
| **TM** | Time: logic that waits, or runs for a set time | DLY, PLS, BLK |
| **LGC** | Logic: decisions the device makes | CMP, AND, OR, NOT, SCL, MIN, MAX, TGL, LCH, CNT |
| **SUB** | Blocks: logic you define once and place many times | NEW, then one per block; in a block, IN, OUT |

## Things every node shares

- **How it works.** Under the heading of a node's settings, **How it works** unfolds a
  short version of what this document says about it. It is folded at first, and stays
  as you last left it.
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
Min and Max output numbers; a Counter has one of each. A logic node is both: wires
come into it, and leave it for outputs or other logic. Between two logic nodes, a
wire goes from the node you picked first to the one you clicked. A wire may close a
loop, to repeat something: see [Loops](#loops).

Every logic node has the **Command view** setting (Show, Hide; Hide by default),
which puts a tile with its state or value in the Command view. The tables below list
only each node's own settings.

When a graph starts (at power-on, and on each save), logic starts off or at 0: a
latch has to be set again, a toggle starts off, and a counter starts at 0.

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

### Counter: `LGC-CNT`

| | |
|---|---|
| Category | Logic |
| Inputs | Two, on/off: **Count** and **Reset** |
| Outputs | **Done** (on/off) and **Count** (a number, 0 to the preset) |
| Default label | Counter |

Counts each time Count goes on, up to its preset, and stops there. Done is on once
the count reaches the preset. While Reset is on, the count is 0 whatever Count does.
Use it to do something a set number of times (see [Loops](#loops)), or to act on the
fifth press of a button. The node shows its count and preset, for example `2/3`. An
input that is already on when the graph starts doesn't count.

The first wire drawn in becomes Count and the second Reset. Pick them in the
settings to change that. Each wire leaving it is labelled Done or Count.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Wire from | Done, Count | Done | Which output the next wire you draw leaves from. Count is a number: wire it to a compare, scale, plot or PWM (which takes it as a fraction of the preset). |
| Preset | 1 to 1000000 | 3 | The count it stops at, and where Done turns on. |
| Count | An on/off source, or None | The first wire in | The source it counts. Picking one draws its wire. |
| Reset | An on/off source, or None | The second wire in | The source that sets it back to 0. |

---

## TM: Time

Logic that works with time: it waits, or runs for a set time. These are logic nodes,
run by the device every 10 ms like those in [LGC](#lgc-logic), with the same
Command view setting; they have a group of their own in the add menu. Times are in
seconds, to the millisecond, up to a day.

### Delay: `TM-DLY`

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

### Pulse: `TM-PLS`

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

### Blink: `TM-BLK`

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

---

## SUB: Blocks

A block is a piece of logic you build once and use in several places, like a
function. It has inputs and outputs of its own. Inside, it is a small graph of logic
and time nodes; outside, each copy of it is one node, wired like any other.

- **Make one:** **SUB → NEW** makes "Block 1", with one input and one output, and puts
  a copy of it in the graph.
- **Edit it:** double-click a copy, or press **Edit block** in its settings. The graph
  shows the block's inside, with its name in a bar at the bottom. Add logic from LGC
  and TM, and inputs and outputs from **SUB → IN** and **SUB → OUT**, and wire them up;
  the groups a block can't hold are red and can't be opened. Press **‹ Graph** in the
  bar to go back: every copy takes the change.
- **Use it again:** in the main graph, **SUB** has a button for each block (its first
  three letters; point at it for its name). Each press adds another copy.
- **Each copy is separate:** it has its own state (a toggle in one copy doesn't turn
  over another) and its own wires. On the device each copy costs as many nodes as
  the logic inside the block.

A block can hold logic and time nodes, and its inputs and outputs. Buttons, sliders,
pins, plots and other blocks stay in the main graph and are wired to the copy.

### Block copy

| | |
|---|---|
| Category | Logic, run by the device as the logic inside it |
| Inputs | One per IN in the block, each on/off or a number |
| Outputs | One per OUT in the block, each on/off or a number |
| Default label | The block's name |

The node shows the block's name. A wire drawn in goes to the first input of its type
with nothing wired yet. Each wire leaving it is labelled with the output's label.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Block | 1 to 32 characters | Block N | The block's name, shared by every copy. Copies still labelled with the old name take the new one. |
| Definition | Edit block | | Opens the block's inside. |
| Wire from | The block's outputs | The first | With more than one output: which one the next wire you draw leaves from. |
| One per input | A source of its type, or None | | What each input is wired from. Picking one draws its wire. |

### Block input: `SUB-IN` and output: `SUB-OUT`

Only inside a block. An input is a source inside the block: whatever is wired into
that input of a copy. An output is wired from inside the block: a copy hands it on
from that output. They are numbered in the order you add them, and a copy shows
their labels ("In 1", "Out 1" by default).

| Setting | Values | Default | What it does |
|---|---|---|---|
| Type | On/off, Number | On/off | What the input or output carries. Changing it takes its wires off, inside the block and on every copy. |

Taking an input or output out of a block takes it, and its wires, off every copy.

Example: a block "Flip" with IN → Toggle → OUT. Two copies: a button → Flip → Flip →
light. The first copy turns over on each press, the second on every other press.

---

## Loops

A wire may go back up the graph, from a logic node into logic that drives it. That
closes a loop, which goes round once every 10 ms: this is how a graph repeats
something, and stops.

- **The wire that closes a loop is a feedback wire**, drawn dashed. The node it goes
  into reads it 10 ms late, as it was at the last scan, so the loop never runs away.
  Delete a wire the loop needs, and the feedback wire becomes an ordinary one.
- **A loop must go through a node that remembers**: a delay, pulse, blink, toggle,
  latch or counter. A wire that would close a loop of only compares, ANDs, ORs, NOTs,
  scales, mins and maxes isn't offered: such a loop would only flip on and off every
  scan.
- **In the browser** a loop runs the same way, so you can try it before saving.

Example: blink the LED three times for each press of a button.

1. Momentary button → Latch Set.
2. Latch → Blink → onboard LED.
3. Blink → Counter Count, with the preset at 3.
4. Latch → NOT → Counter Reset: the count starts from 0 for each press.
5. Counter (Done) → Latch Reset. This wire closes the loop and is drawn dashed.

Pressing the button sets the latch, which runs the blink. The counter counts the
blinks; at the third, Done resets the latch, which stops the blink and resets the
counter, ready for the next press.

The demo graph, which a new device starts from, has this loop inside a block,
"Blink 3": a momentary button **Blink 3 times** → the block → a light in the Command
view. The block's input, Start, sets the latch; its output, Blink, is the blink.
Double-click the block to see the loop.

Another loop: **a tick every 5 s.** NOT → Delay (on delay 5 s) → back into the NOT.
The delay turns on 5 s after the NOT does, which turns the NOT off, which turns the
delay off again: the delay's output is on for 10 ms every 5 s. Wire it to a Pulse
for a longer kick, or to a Counter to count the ticks.

---

## Which node takes which

| From ↓ / into → | LED, GPO, KEY, LGT | PWM, PLT | CMP, SCL, MIN, MAX | AND, OR, NOT, DLY, PLS, BLK, TGL, LCH, CNT |
|---|---|---|---|---|
| BTN, GPI | ✓ | | | ✓ |
| SLD, XY (X or Y), ADC, TMP | | ✓ | ✓ | |
| CMP, AND, OR, NOT, DLY, PLS, BLK, TGL, LCH, CNT (Done) | ✓ | | | ✓ |
| SCL, MIN, MAX, CNT (Count) | | ✓ | ✓ | |

A block's copy takes, on each input, and gives, on each output, what the block's IN or
OUT says: on/off like a button, or a number like a slider.

# neoGraph Nodes

Every node you can add to the graph: where it is in the add menu, what it does, and
its settings. [PROTOCOL.md](PROTOCOL.md) has the bytes behind each one.

## Finding a node

The **+** button in the Graph view opens the nine groups. A group opens its nodes
under it. In this document, a node's place is written as `GROUP-BUTTON`: `HWI-TMP` is
the TMP button in the HWI group.

| Group | Category | Nodes |
|---|---|---|
| **UII** | UI inputs: controls you operate in the browser | BTN, SLD, XY |
| **HWO** | Hardware outputs: what the device drives | LED, GPO, PWM, STP, SRV, NPX |
| **HWI** | Hardware inputs: what the device reads | TMP, ADC, GPI, DS, ENC |
| **UIO** | UI outputs: what the browser shows | PLT, LGT |
| **USB** | USB: the device acting as a USB device | KEY |
| **TM** | Time: logic that waits, or runs for a set time; the clock | DLY, PLS, BLK, CLK |
| **ANL** | Analog: logic that works on numbers | CMP, SCL, MIN, MAX, SIN |
| **LGC** | Logic: decisions the device makes | AND, OR, NOT, TGL, LCH, CNT |
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

### Servo: `HWO-SRV`

| | |
|---|---|
| Category | Hardware output |
| Input | Number, as a fraction of its source's full scale |
| Default label | SERVO |

A hobby servo, such as an SG90, on one pin. The device sends it a pulse every 20 ms,
and the pulse's width sets the servo's angle. As with a PWM, the input is taken as a
fraction of its source's full scale: 1 for a slider or scale, 3.3 V for an ADC, 100 °C
for the core temperature, a counter's preset. 0 gives Angle at 0 and 1 gives Angle
at 1.

| Setting | Values | Default | What it does |
|---|---|---|---|
| GPIO pin | GPIO 0–22, 26–28; not on a PWM's or another servo's slice | 14, or a free pin on a free slice | The servo's signal wire (orange on an SG90). |
| Angle at 0 (°), Angle at 1 (°) | 0–180 each | 0, 180 | The angle at the bottom and top of the input. Angle at 0 may be the larger, which turns the input around. 45 and 135 limit it to the middle 90°. |
| Pulse at 0° (µs), Pulse at 180° (µs) | 100–3000 each | 500, 2400 | Calibration: the pulse that puts this servo at 0° and at 180°. An SG90 is near 500 and 2400. If it buzzes or strains at an end, it is pushing against its stop: bring that pulse in towards 1450. Swapping them turns it around. |
| Speed limit (°/s) | 0–3600 | 0 | Turns it no faster than this, for a slow sweep; 0 lets it go as fast as it can (an SG90 does about 600 °/s). |

Things to know:

- **It takes its pin's PWM slice.** It runs the slice at 50 Hz, so a PWM or another
  servo can't use the other pin on that slice.
- **At power-on or a save** it goes straight to its angle: the device can't tell where
  the servo was.
- **Power it from 5 V,** red to VBUS (or VSYS) and brown to GND. A servo starting or
  under load draws a lot, and can reset the Pico on a weak USB supply; a separate 5 V
  supply with its ground joined to the Pico's is safer for more than one.

### NeoPixels: `HWO-NPX`

| | |
|---|---|
| Category | Hardware output |
| Inputs | A number, as a fraction of its source's full scale; an on/off; and a data line from another NeoPixels node |
| Outputs | **Data**: its LEDs, for another NeoPixels node to draw over |
| Default label | NEOPIXEL |

A chain of NeoPixels (WS2812 LEDs) on one pin, showing a graphic of its input. As with
a servo, the number is taken as a fraction of its source's full scale: 1 for a slider or
scale, 3.3 V for an ADC, 100 °C for the core temperature, a counter's preset.

It takes two wires, told apart by their type: a **number**, which the graphic shows,
and an **on/off** (a button, a GPIO input or logic). While the on/off is off every LED
is off; with none wired it is always on. A new wire of either type replaces the old one
of that type.

The node's settings say how the LEDs are laid out, and which graphic they show:

| Setting | Values | Default | What it does |
|---|---|---|---|
| GPIO pin | GPIO 0–22, 26–28, not used by another node | 16, or the lowest free pin | The LEDs' data in (DIN). |
| Layout | Array, Circle, Doughnut | Array | **Array:** rows of columns, LED 1 at the top left, along the top row. **Circle:** rings one inside another, as on a NeoPixel disc. **Doughnut:** one ring. |
| Rows, Columns | 1–255 each, at most 256 LEDs | 4, 4 | An array's size. One row is a strip. |
| Wiring | Row by row, Zigzag | Row by row | An array: Zigzag when every other row runs back, right to left, as on many panels. |
| Rings (LEDs, as wired) | 1–8 rings, at most 256 LEDs | 24, 16, 12, 8, 1 | A circle: each ring's LEDs, in the order they are wired. |
| LEDs | 1–255 | 24 | A doughnut's size. |
| Brightness (0-255) | 0–255 | 40 | Scales every colour. |
| Graphic | Bar level, Moving point, Patch | Bar level | What it shows. |
| Colour | Fixed, From input | Fixed | **Fixed:** one colour, picked with the colour picker. **From input:** a Colour at 0 and a Colour at 1, and in between a blend of the two as the input goes from 0 to 1 (green to red is yellow half way). |

The **Bar level** graphic lights a bar in its colour as far as the input reaches; the
LED it ends in is lit part way, so it moves smoothly. Its settings depend on the layout:

| Layout | Along | Position | Direction |
|---|---|---|---|
| Array | Row or Column | Which row (from the top) or column (from the left), from 1 | Along a row: Left to right, or Right to left. Up a column: Bottom up, or Top down. |
| Circle | Around a ring, or Centre out | Around a ring: which ring, from 1 in wiring order. Centre out lights whole rings, the smallest first. | Around a ring: As wired, or Reversed. Centre out, or Outside in. |
| Doughnut | Around | Starts at LED, from 1 | As wired, or Reversed. |

The **Moving point** graphic is a dot that moves from its start at 0 to its end at 1. It
glides from LED to LED: between two LEDs it lights both, each as much as it is near
it, so a slow input moves it smoothly, not in jumps.

| Layout | Start and end | Direction |
|---|---|---|
| Array | A row and a column each, from 1. It goes in a straight line between them: along a row, up or down a column, or on a slant | (none) |
| Circle | A ring (in wiring order) and an LED of that ring each, from 1. It moves from ring to ring, and around from the start's angle to the end's | As wired, or Reversed: which way round it goes |
| Doughnut | An LED each, from 1. The same LED at both ends goes once all the way round | As wired, or Reversed |

The **Patch** graphic lights a fixed area: every LED from its start to its end. The
number wired in doesn't move or resize it; with Colour From input it sets the patch's
colour, fading from the Colour at 0 to the Colour at 1. With a Fixed colour it is a
static patch, which the on/off input can still switch.

| Layout | Start and end | Direction |
|---|---|---|
| Array | A row and a column each, from 1: opposite corners of a rectangle, both included. The same row at both ends gives a strip of one row; the same LED, one LED | (none) |
| Circle | A ring (in wiring order) and an LED of it each, from 1. It lights the rings from the start's to the end's, each from the start's angle round to the end's | As wired, or Reversed: which way round it goes |
| Doughnut | An LED each, from 1: an arc from one round to the other. LED 1 to the last lights the whole ring | As wired, or Reversed |

Under the settings a **preview**
draws the LEDs with the graphic at the level of its slider, at full brightness, so a
dim setting still shows; point at an LED for its number.

**Chaining: several graphics on one set of LEDs.** Wire one NeoPixels node to another
(select the first, then the second, in wire mode): the wire, labelled Data, carries
what the first draws into the second, which draws its own graphic over it. Chain as
many as you like, one after another:

- **Later nodes override earlier ones.** Each LED a later node lights takes its
  colour; one it lights only partly (the end of a bar, a point between two LEDs) blends
  from the colour underneath; the LEDs it doesn't light show what was drawn before. A
  node whose on/off is off draws nothing, so what is under it shows.
- **The last node drives the LEDs:** the one whose Data output goes nowhere. The others
  only draw.
- **One set of LEDs.** The nodes of a chain share the pin, layout, size and wiring:
  change them on any node and they change on all. Joining two chains, they take the
  settings of the last node. Brightness, graphic and colour are each node's own.
- **The preview** of a node shows its chain drawn as far as that node, each node at
  its own preview level.
- **Taking the Data wire out** splits the chain: the node it came from drives its own
  LEDs again, on a free pin if the chain kept its pin.

For example: a dim blue Patch over the whole ring as a background, a green Bar level
over it for a tank's level, and a red Moving point last, marking the level to fill to.

Things to know:

- **Power them from 5 V.** A NeoPixel draws up to 60 mA at full white: more than a few
  want their own 5 V supply, with its ground joined to the Pico's. Keep the brightness
  down: at 40 a white LED draws about 10 mA.
- **The data line** goes straight to the pin. Most WS2812Bs take 3.3 V on it; if yours
  flicker or show the wrong colours, a level shifter to 5 V fixes it.
- **The on/off** is read on the device: a button in the Command view, a GPIO input or
  logic turns the LEDs on and off within one 10 ms scan.
- **Up to 256 LEDs a node, and 1024 in all.** Each node that drives LEDs (the last
  of a chain) takes one of the chip's PIO state machines and a DMA channel; a node
  that finds none free is skipped.
- **On a save** the LEDs keep what they show until the device draws them again; taken
  out of the graph, or moved to another pin, they turn off.

### Stepper motor: `HWO-STP`

| | |
|---|---|
| Category | Hardware output |
| Input | Number, in its source's own unit |
| Default label | STEPPER |

A unipolar stepper motor, such as a 28BYJ-48, through a ULN2003 driver board. The
device drives the board's IN1 to IN4 from four pins. Its input either sets **where it
is** (in revolutions) or **how fast it turns** (in RPM). It speeds up and slows down
smoothly, and in Position it slows down in time to stop on its target.

Its input is taken in the source's own unit, not as a fraction of a full scale as a
PWM takes it: 1.65 V from an ADC with 2 revolutions per V is 3.3 revolutions.

| Setting | Values | Default | What it does |
|---|---|---|---|
| IN1 pin … IN4 pin | GPIO 0–22, 26–28; four that differ, not used by another node | 10, 11, 12, 13, or the lowest four free | The pins wired to the board's IN1 to IN4. If it turns the wrong way, use Direction rather than swapping wires. |
| Drive | Wave, Full, Half | Half | **Wave:** one coil on at a time, the least current. **Full:** two at a time, the most torque. **Half:** one then two in turn, twice the steps a revolution: the smoothest and quietest. |
| Steps per revolution | 4–65535 | 2048 | Full steps for one turn of the shaft: 2048 for a 28BYJ-48 (32 steps × its 64:1 gearbox); 200 for a 1.8° motor. Half drive doubles them by itself. |
| Input sets | Position, Speed | Position | **Position:** the input × Revolutions per unit is where it turns to, from where it was at power-on. **Speed:** the input × RPM per unit is how fast it turns, backwards below 0; 0 stops it. |
| Revolutions per unit (Position), RPM per unit (Speed) | Any number; below 0 turns it around | 1 | How far, or how fast, one unit of the input turns it. "Unit" reads V, °C and so on once a source is wired in. |
| Top speed (RPM) | Above 0, up to 300 | 15 | It never turns faster. A 28BYJ-48 manages about 15 RPM at 5 V and stalls, buzzing, past about 20. The device steps at most 2000 times a second. |
| Acceleration (RPM/s) | 0 or more | 30 | How quickly it speeds up and slows down. 30 takes it to 15 RPM in half a second. 0 starts and stops at once, which may skip steps under load. |
| Direction | Normal, Reversed | Normal | Which way positive values turn it. |
| When stopped | Hold, Release | Hold | **Hold:** its coils stay on, so it keeps its place under load (a 28BYJ-48 draws about 240 mA and gets warm). **Release:** they turn off, so it runs cool but can be turned by hand, and then loses its place. |

Things to know:

- **It has no home sensor.** Where it is starts at 0 at power-on, wherever the shaft
  is. Saving the graph keeps its place; a reboot doesn't.
- **Power it from 5 V,** the board's + and − to VBUS (or VSYS) and GND, never from a
  GPIO pin; only IN1 to IN4 go to the Pico.
- **Examples.** A slider with Revolutions per unit 1 turns it up to one turn. A
  counter's Count with 0.25 turns it a quarter turn a count. In Speed, a 2D slider's X
  through a scale from 0–1 to −1–1 with RPM per unit 15 makes a joystick: centre is
  stop, left and right turn it either way.

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

### DS18B20 temperature sensor: `HWI-DS`

| | |
|---|---|
| Category | Hardware input |
| Outputs | **°C**: number, the temperature; **Found**: on/off, on while the sensor answers |
| Default label | DS18B20 |

A DS18B20 digital temperature sensor (the common waterproof probe, or the TO-92
part) on one pin. The device reads it over and over: at 12 bits a new reading every
0.75 s, in 0.0625 °C steps, good to ±0.5 °C from −10 to +85 °C. It reads −55 to
+125 °C. The offline demo simulates a room around 22 °C.

It has two outputs. Pick the one the next wire leaves from in **Wire from**, and each
wire is labelled with it:

- **°C** goes to anything that takes a number: a compare (a thermostat: on above
  30 °C), a plot, a scale, or a PWM, servo or NeoPixels, which take it as a fraction of
  100 °C.
- **Found** goes to anything that takes on/off: an LED, a light, or logic that
  sounds an alarm when the sensor is unplugged or its wire breaks.

| Setting | Values | Default | What it does |
|---|---|---|---|
| GPIO pin | GPIO 0–22, 26–28, not used by another node | 15, or the lowest free pin | The pin its data wire (DQ, yellow) is on. |
| Resolution | 9, 10, 11, 12 bits | 12 | 9: 0.5 °C steps, a reading every 94 ms; 10: 0.25 °C, 188 ms; 11: 0.125 °C, 375 ms; 12: 0.0625 °C, 750 ms. The settings say which under the choice. |
| Pull-up | 4.7 kΩ resistor, Internal | 4.7 kΩ resistor | The data line needs a pull-up to 3.3 V. Internal uses the pin's own, weaker one: enough for one sensor on a short wire. |
| Wire from | °C, Found | °C | Which output the next wire leaves from. |
| Command view | Show, Hide | Hide | A tile with the temperature, or "No sensor". |

Things to know:

- **Wiring:** red (VDD) to 3.3 V, black (GND) to GND, yellow (DQ) to the pin, and a
  4.7 kΩ resistor between DQ and 3.3 V. Parasite power (two wires, VDD to GND) is not
  supported.
- **One sensor per pin.** For two sensors, use two pins and two nodes.
- **If it stops answering,** Found goes off after three failed reads (a second or two),
  the temperature keeps its last reading, and the device keeps looking for it.
- **After a save** it keeps its reading until the next one.

### Rotary encoder: `HWI-ENC`

| | |
|---|---|
| Category | Hardware input |
| Outputs | **Position**: number, in clicks, 0 to its Steps; **Pressed**: on/off, on while the knob is pushed in |
| Default label | Encoder |

A rotary encoder, such as a KY-040 module or an EC11: a knob that turns without end, in
clicks, and pushes in as a switch. Each click one way adds 1 to its position, each the
other way takes 1 off. The device counts every click, however fast it is turned. The
offline demo turns it slowly to and fro.

It has two outputs. Pick the one the next wire leaves from in **Wire from**, and each
wire is labelled with it:

- **Position** goes to anything that takes a number: a servo, PWM or NeoPixels, which
  take it as a fraction of Steps (with 20 steps, 20 clicks sweep a servo across), a
  compare, a scale or a plot.
- **Pressed** goes to anything that takes on/off: a toggle (push to turn something on
  and off), a counter, an LED or a light.

| Setting | Values | Default | What it does |
|---|---|---|---|
| A (CLK) pin | GPIO 0–22, 26–28, not used by another node | 6, or the lowest free pin | The pin its A contact (CLK) is on. |
| B (DT) pin | as A, not A's | 7 | The pin its B contact (DT) is on. |
| Switch (SW) pin | None, or as A, not A's or B's | 8 | The pin its push switch (SW) is on. With None, Pressed stays off. |
| Steps | 1–1000 | 20 | The top of its position. |
| A click is | Full cycle, Half cycle, Each edge | Full cycle | How many changes of its contacts make a click. Full cycle suits most encoders (a KY-040, an EC11); if it counts only every other click, pick Half cycle; Each edge for an encoder without clicks. |
| At the ends | Wrap, Stop | Wrap | Wrap goes on from the other end, so it runs 0 to Steps − 1: a 20-click knob with 20 steps comes back to 0 each turn. Stop holds it at 0 and at Steps. |
| Direction | Normal, Reversed | Normal | Normal counts up clockwise on a KY-040. Reversed counts the other way. |
| Wire from | Position, Pressed | Position | Which output the next wire leaves from. |
| Command view | Show, Hide | Hide | A tile with its position, such as "5 of 20", and ", pressed" while it is. |

Things to know:

- **Wiring:** CLK and DT to the A and B pins, SW to the switch pin, `+` to 3.3 V and GND
  to GND. Don't power it from 5 V: a KY-040's resistors would pull the pins to 5 V. The
  device turns on each pin's pull-up, so a bare encoder needs no resistors: its common
  pin, and the switch's other side, go to GND.
- **It starts at 0,** and a save keeps its position (within the new Steps).
- **Up to 8 encoders** in a graph.

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
node reads one or more sources and is a source itself. Most output on/off; the
[analog](#anl-analog) Scale, Min and Max output numbers; a Counter has one of each. A logic node is both: wires
come into it, and leave it for outputs or other logic. Between two logic nodes, a
wire goes from the node you picked first to the one you clicked. A wire may close a
loop, to repeat something: see [Loops](#loops).

The time and analog nodes ([TM](#tm-time), [ANL](#anl-analog)) are logic too, in
groups of their own. Every logic node has the **Command view** setting (Show, Hide; Hide by default),
which puts a tile with its state or value in the Command view. The tables below list
only each node's own settings.

When a graph starts (at power-on, and on each save), logic starts off or at 0: a
latch has to be set again, a toggle starts off, and a counter starts at 0.

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
seconds, to the millisecond, up to a day. The group also has the device's clock, the
time of day.

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

### Clock: `TM-CLK`

| | |
|---|---|
| Category | Hardware input: the device's clock |
| Outputs | **Hour**: number, 0–23; **Minute**: number, 0–59; **Second**: number, 0–59. Each a whole number |
| Default label | Clock |

The device's time of day. Pick the output the next wire leaves from in **Wire from**,
and each wire is labelled with it. Wire the hour into a compare to switch a lamp on in
the evening (above 17: from 18:00), the minute into a plot, or the second into a servo
to sweep it once a minute. A servo, PWM or NeoPixels take the hour as a fraction of 24,
a minute or second as a fraction of 60.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Hours, Minutes, Seconds | 0–23, 0–59, 0–59 | The time now | Type a part of the time to set it; the other parts stay as they are. |
| Set to this computer's time | (a button) | | Sets the clock to the time on the computer, to the second. |
| Wire from | Hour, Minute, Second | Hour | Which output the next wire leaves from. |
| Command view | Show, Hide | Hide | A tile with the time, such as 07:30:00. |

Things to know:

- **One clock.** The device has a single clock; every clock node shows the same time,
  and setting any of them sets it.
- **Setting it takes effect at once** on the device, without saving the graph, and
  saving doesn't change it: the time isn't saved with the graph.
- **No battery.** When the device powers up, its clock starts at 00:00:00 and counts
  from there until you set it.
- **Without a device** (or before it reports) the node keeps time in the browser, from
  the computer's clock or the time you last set, and the node shows it.

---

## ANL: Analog

Logic that works on numbers: a slider's 0 to 1, an ADC's volts, the core
temperature's °C, a counter's count, or another analog node's output. A compare turns
a number into on/off; a scale, min and max give a number, and a sine makes one. These are logic nodes, run
by the device every 10 ms like those in [LGC](#lgc-logic), with the same Command view
setting.

### Compare: `ANL-CMP`

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

### Scale: `ANL-SCL`

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

### Min: `ANL-MIN`

| | |
|---|---|
| Category | Logic |
| Inputs | 1 to 4 numbers |
| Output | Number, in the unit of its first input |
| Default label | Min |

The lowest of its wired inputs. With nothing wired in, 0.

No settings of its own.

### Max: `ANL-MAX`

| | |
|---|---|
| Category | Logic |
| Inputs | 1 to 4 numbers |
| Output | Number, in the unit of its first input |
| Default label | Max |

The highest of its wired inputs, such as the hottest of several readings. With
nothing wired in, 0.

No settings of its own.

### Sine: `ANL-SIN`

| | |
|---|---|
| Category | Logic |
| Input | One on/off, or none |
| Output | Number |
| Default label | Sine |

A smooth wave: it swings from its offset − amplitude up to offset + amplitude and back
once every period, starting at its offset and rising. The defaults swing from 0 to 1
every 2 s, which sweeps a servo or PWM from end to end, or makes NeoPixels breathe
(with Colour From input, or a bar). The node runs in the browser too, so a plot wired to
it moves without a device.

It runs while its on/off input is on, or all the time with nothing wired in. When the
input goes off it rests where it starts (its offset, with phase 0), and starts from the
beginning of its wave when it comes back on.

| Setting | Values | Default | What it does |
|---|---|---|---|
| Period (s) | 0.02 s to a day | 2 | How long one wave takes. |
| Amplitude | Any number | 0.5 | How far it swings either side of its offset. Below 0, it falls first. |
| Offset | Any number | 0.5 | The middle of the wave: where it starts, and rests while off, with phase 0. |
| Phase (°) | −360 to 360 | 0 | Where on the wave it starts, and rests while off: 90 at its top, 180 on its way down through its offset, 270 at its bottom. Two sines with one period, at 0 and 90, trace a circle. |
| Command view | Show, Hide | Hide | A tile with its value. |

Under the settings it says what that makes, for example "From 0 to 1 and back every
2 s", and with a phase where it starts. The device works it out every 10 ms scan, so a period under a few tenths of a
second comes out in coarse steps.

---

## SUB: Blocks

A block is a piece of logic you build once and use in several places, like a
function. It has inputs and outputs of its own. Inside, it is a small graph of logic,
time and analog nodes; outside, each copy of it is one node, wired like any other.

- **Make one:** **SUB → NEW** makes "Block 1", with one input and one output, and puts
  a copy of it in the graph.
- **Edit it:** double-click a copy, or press **Edit block** in its settings. The graph
  shows the block's inside, with its name in a bar at the bottom. Add logic from LGC,
  TM and ANL, and inputs and outputs from **SUB → IN** and **SUB → OUT**, and wire them up;
  the groups a block can't hold are red and can't be opened. Press **‹ Graph** in the
  bar to go back: every copy takes the change.
- **Use it again:** in the main graph, **SUB** has a button for each block (its first
  three letters; point at it for its name). Each press adds another copy.
- **Each copy is separate:** it has its own state (a toggle in one copy doesn't turn
  over another) and its own wires. On the device each copy costs as many nodes as
  the logic inside the block.

A block can hold logic, time and analog nodes, and its inputs and outputs. Buttons, sliders,
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
| Type | On/off, Number | On/off; with no wire, an input takes the type of the first wire drawn from it, and an output of the first wire drawn into it | What the input or output carries. Changing it takes its wires off, inside the block and on every copy. |

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
Double-click the block to see the loop. Below it, a slider **Level** drives a
NeoPixel node: a 4 × 4 array on GPIO 16 showing the slider as a green bar along its
top row.

Another loop: **a tick every 5 s.** NOT → Delay (on delay 5 s) → back into the NOT.
The delay turns on 5 s after the NOT does, which turns the NOT off, which turns the
delay off again: the delay's output is on for 10 ms every 5 s. Wire it to a Pulse
for a longer kick, or to a Counter to count the ticks.

---

## Which node takes which

| From ↓ / into → | LED, GPO, KEY, LGT | PWM, PLT | CMP, SCL, MIN, MAX | AND, OR, NOT, DLY, PLS, BLK, TGL, LCH, CNT, SIN |
|---|---|---|---|---|
| BTN, GPI, DS (Found), ENC (Pressed) | ✓ | | | ✓ |
| SLD, XY (X or Y), ADC, TMP, DS (°C), ENC (Position) | | ✓ | ✓ | |
| CMP, AND, OR, NOT, DLY, PLS, BLK, TGL, LCH, CNT (Done) | ✓ | | | ✓ |
| SCL, MIN, MAX, SIN, CNT (Count), CLK (Hour, Minute or Second) | | ✓ | ✓ | |

A block's copy takes, on each input, and gives, on each output, what the block's IN or
OUT says: on/off like a button, or a number like a slider.

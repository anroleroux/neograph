// How each node works, by its type: shown, folded, under the heading of its
// settings (see helpSetting). Each entry is a list of paragraphs. NODES.md says
// the same at more length.
const NODE_HELP = {
    button: [
        "A button in the Command view. Click or tap it, or press Space or Enter on it. Each press is sent to the device, which acts on it.",
        "Toggle: each press turns it on or off. Momentary: on only while held. Use Momentary to start something, such as a pulse, a toggle, a keyboard or a block.",
        "Wire it to anything that takes on/off: the LED, a GPO, a light, a keyboard, or logic.",
    ],
    slider: [
        "A slider in the Command view. Its value, 0 to 1, is sent to the device each time it moves.",
        "Wire it to anything that takes a number: a PWM, a plot, a compare or a scale.",
    ],
    slider2d: [
        "A square pad with a puck, like a joystick, in the Command view. It has two outputs, X (left to right) and Y (bottom to top), each 0 to 1.",
        "Pick the output the next wire leaves from in Wire from; each wire is labelled X or Y. Home is where the puck starts, and where it springs back to with Return home.",
    ],
    led: [
        "The Pico 2 W's onboard LED: on while its input is on.",
        "Wire one on/off source into it: a button, a GPIO input, or logic. The device drives it, with or without the browser.",
    ],
    gpo: [
        "A header pin driven by the device: high (3.3 V) while its input is on, low while it is off.",
        "Wire one on/off source into it, and pick the pin. Each pin can be used by one node only.",
    ],
    pwm: [
        "A pulse-width-modulated pin, for a motor, fan, dimmable LED or buzzer.",
        "Its duty cycle and its frequency are each constant, or follow an input. The first wire drawn in sets the duty cycle; the second the frequency.",
        "An input is taken as a fraction of its source's full scale (1 for a slider, 3.3 V for an ADC, 100 °C for the core temperature, a counter's preset): 0 gives the \"at 0\" value and 1 the \"at 1\" value.",
        "Pins that share a PWM slice share its frequency, so only one PWM is allowed per slice.",
    ],
    stepper: [
        "A stepper motor on four pins, through a ULN2003 board (IN1 to IN4), such as a 28BYJ-48. Wire one number into it.",
        "Input sets Position: the input × Revolutions per unit is where it turns to, from where it was at power-on. Speed: the input × RPM per unit is how fast it turns; below 0 it turns the other way.",
        "It speeds up and slows down at its acceleration, and never goes past its top speed (a 28BYJ-48 manages about 15 RPM at 5 V). Steps per revolution counts full steps: 2048 for a 28BYJ-48. Half drive is smoothest; Full has the most torque; Wave uses the least current.",
        "Hold keeps its coils on when it stops, so it holds its place; Release turns them off, so it runs cooler but can be turned by hand. Power the motor from 5 V, not from a GPIO pin.",
    ],
    servo: [
        "A hobby servo, such as an SG90, on one pin. Wire one number into it: its input is taken as a fraction of its source's full scale (1 for a slider, 3.3 V for an ADC), and 0 gives Angle at 0, 1 gives Angle at 1.",
        "Every 20 ms the device sends it a pulse whose width sets its angle, from Pulse at 0° to Pulse at 180°. An SG90 takes about 500 to 2400 µs; if it buzzes or strains at an end, bring that pulse in towards 1450.",
        "Speed limit slows it to that many degrees a second; 0 lets it turn as fast as it can. Power it from 5 V, not 3.3 V; its signal wire can go straight to the pin.",
        "A servo uses its pin's PWM slice, so a PWM or another servo can't use the other pin on that slice.",
    ],
    neopixel: [
        "NeoPixels (WS2812 LEDs) on one pin, showing a graphic of the number wired in. The number is taken as a fraction of its source's full scale (1 for a slider, 3.3 V for an ADC), as a servo takes it.",
        "It also takes an on/off wire (a button, GPIO input or logic): while it is off, every LED is off. Not wired, it is always on. A new wire replaces the one of its type.",
        "Layout: an Array is rows of columns, LED 1 at the top left along the top row (Zigzag: every other row runs back). A Circle is rings one inside another, listed in the order they are wired. A Doughnut is one ring.",
        "Bar level lights a bar as far as the input reaches; the LED it ends in is lit part way. On an array it runs along a row or up a column; on a circle around one ring, or across the rings from the centre out; on a doughnut around from the LED it starts at.",
        "Point is a dot moving from its start at 0 to its end at 1, gliding between LEDs (it lights the two it is between, each as much as it is near it): on an array in a straight line, any way across; on a circle or doughnut around (and from ring to ring), the way Direction says. The same LED at both ends goes once round.",
        "Patch lights a fixed area from its start to its end: a rectangle on an array (start and end are opposite corners), an arc on a doughnut, a sector on a circle. The number doesn't move it; with Colour From input it sets its colour.",
        "Colour: Fixed, or From input, which blends from Colour at 0 to Colour at 1 as the input rises. Brightness scales every colour. Each LED draws up to 60 mA at full white, so power more than a few from 5 V, with the grounds joined. The preview shows the graphic at the level you set, at full brightness.",
        "Chain NeoPixels nodes to show several graphics on one set of LEDs: wire one node to another, and the second draws over what the first drew (the Data wire). Later nodes override earlier ones; a node that is off draws nothing, so what is under it shows. The last node, whose Data goes nowhere, drives the LEDs. The chain shares its pin, layout and size: set them on any node.",
    ],
    ds18b20: [
        "A DS18B20 digital temperature sensor on one pin, read by the device about every 0.1 to 0.75 s. It has two outputs: °C, the temperature (a number), and Found (on/off), on while the sensor answers. Pick the one the next wire leaves from in Wire from.",
        "Wire it: red (VDD) to 3.3 V, black (GND) to GND, yellow (DQ, data) to the pin, and a 4.7 kΩ resistor from DQ to 3.3 V. For a short wire and one sensor, Internal uses the pin's own weak pull-up in place of the resistor; if it reads No sensor, fit the resistor. One sensor per pin; parasite power (two wires) is not supported.",
        "Resolution: 9 bits gives 0.5 °C steps, a reading every 94 ms; 12 bits 0.0625 °C every 750 ms. Its temperature is good from −55 to +125 °C. Wired into a PWM or servo, it counts as a fraction of 100 °C.",
        "If the sensor stops answering, Found goes off after three failed reads, and the temperature keeps its last reading.",
    ],
    encoder: [
        "A rotary encoder, such as a KY-040 or an EC11: a knob that turns without end, in clicks, and often pushes in as a switch. It has two outputs: Position (a number, 0 to its Steps), and Pressed (on/off), on while the knob is pushed in. Pick the one the next wire leaves from in Wire from.",
        "Wire it: CLK (A) and DT (B) to two pins, SW to a third (or None), + to 3.3 V and GND to GND. Don't power a KY-040 from 5 V: its pins would then be pulled to 5 V. The device turns on each pin's pull-up.",
        "Each click moves Position by 1, up to Steps; At the ends Wrap (the default) goes round from Steps − 1 back to 0, as the knob turns freely; Stop holds it at 0 and at Steps. Wired into a PWM, servo or NeoPixels, it counts as a fraction of Steps: 20 steps sweeps a servo across in 20 clicks. If it counts the wrong way, set Direction to Reversed.",
        "A click is: Full cycle for most encoders (a KY-040, an EC11); if it counts only every other click, Half cycle; Each edge for an encoder without clicks. A save keeps its position.",
    ],
    plot: [
        "A live chart of a number in the Command view, newest on the right. Point at it to read a value.",
        "Wire one number into it: a slider, an ADC, the core temperature, a scale, or a counter's Count. Its history is lost when the page reloads.",
    ],
    light: [
        "A lamp in the Command view: green while its input is on. It shows – while nothing is wired in.",
        "Wire one on/off source into it. Unlike the LED, it is drawn in the browser, not on the device.",
    ],
    core: [
        "The chip's own temperature sensor, in °C, read every 100 ms and smoothed over about a second. It is accurate to a few °C.",
        "Wire it to a compare (to act above or below a temperature), a scale, a PWM or a plot. Command view: Show puts a tile with the reading there.",
    ],
    adc: [
        "An analog input pin, read in volts (0 to 3.3) every 10 ms, for a potentiometer or an analog sensor.",
        "Wire it to a compare, a scale, a PWM or a plot. To use one reading in several places, wire one ADC node to all of them.",
    ],
    gpi: [
        "A header pin read as on or off every 10 ms, for a switch or a digital sensor. A change counts once it has held for 30 ms, so a bouncing switch changes it once.",
        "Pull Down suits a switch to 3.3 V; Pull Up with On when Low suits a switch to ground. Drive the pin with 3.3 V at most.",
    ],
    keyboard: [
        "Plugged into a PC, the device is also a USB keyboard. This node types its key combination, or its line of text, each time its input goes on.",
        "Wire a momentary button, a GPIO input or logic into it. Text is typed as on a US keyboard; \\n is Enter and \\t is Tab.",
    ],
    compare: [
        "On while its number input is above (or below) the threshold. The threshold is in the input's unit: °C, volts, or 0 to 1 for a slider.",
        "Hysteresis stops it flickering near the threshold: above 40 with a hysteresis of 1 turns on above 40, and off only below 39.",
        "Example: core temperature → compare (above 40) → GPO drives a fan while the chip is hot.",
    ],
    and: [
        "On while every wired input is on. Up to 4 on/off inputs; with none wired, off.",
    ],
    or: [
        "On while any wired input is on. Up to 4 on/off inputs; with none wired, off.",
    ],
    not: [
        "On while its input is off, and off while it is on. With nothing wired in, on.",
        "Wired back into a delay that feeds it, it makes a loop that ticks (see a delay's help).",
    ],
    delay: [
        "Follows its input, but only once the input has stayed in its new state for the on delay (to turn on) or the off delay (to turn off). A blip shorter than that never gets through.",
        "Use it to debounce a switch, or to keep a fan running for a while after its input goes off (on delay 0, off delay 5).",
        "In a loop: NOT → delay (on delay 5 s) → back into the NOT turns the delay on for 10 ms every 5 s.",
    ],
    scale: [
        "Maps its number input from one range onto another: 30 to 60 onto 0 to 1 turns 45 into 0.5. Either range may run downwards.",
        "Clamp keeps the output within its range when the input goes outside its own. Set the output range to 0 to 1 to drive a PWM.",
    ],
    min: [
        "The lowest of up to 4 number inputs, in the unit of the first. With none wired, 0.",
    ],
    max: [
        "The highest of up to 4 number inputs, such as the hottest of several readings, in the unit of the first. With none wired, 0.",
    ],
    sine: [
        "A smooth wave, a number: its offset plus its amplitude times the sine of the time, going round once each period. With offset 0.5 and amplitude 0.5 it swings from 0 to 1, which sweeps a servo or PWM from end to end, or breathes NeoPixels.",
        "It runs while its on/off input is on, or always with nothing wired in. When the input goes off it rests at its offset, and starts from the beginning when it comes on again. The device runs it every 10 ms scan, so the shortest period is 0.02 s.",
    ],
    pulse: [
        "Turns on for its time each time its input goes on, however long the input stays on. Going on again while on starts the time again.",
        "Use it to give a relay or buzzer a fixed-length kick from a button press. An input already on when the graph starts doesn't count.",
    ],
    blink: [
        "Flashes while its input is on: on for its time on, off for its time off, in turn, starting on. When the input goes off it stops, off. With nothing wired in it blinks all the time.",
    ],
    toggle: [
        "Turns over (off to on, or on to off) each time its input goes on. With a momentary button or a push switch, it makes an on/off switch.",
    ],
    latch: [
        "Turns on when Set goes on, and stays on until Reset goes on. While Reset is on it is off, whatever Set does.",
        "The first wire drawn in is Set, the second Reset; pick them below to change that. Use it for an alarm that stays on until someone resets it.",
    ],
    counter: [
        "Counts each time Count goes on, up to its preset, and stops there. Done is on once the count reaches the preset. While Reset is on, the count is 0.",
        "It has two outputs: Done (on/off) and Count (a number). Pick the one the next wire leaves from in Wire from.",
        "To do something N times: let it count the something, and wire Done back to what starts it. The wire that closes a loop is drawn dashed: it is read 10 ms late, as the last scan left it. A loop has to go through a node that remembers, such as a latch, delay or counter.",
    ],
    block: [
        "A copy of a block: logic you define once and place many times, like a function. Its inputs and outputs are the block's IN and OUT nodes.",
        "Double-click it, or press Edit block, to open the block's inside. Changes there reach every copy when you go back with ‹ Graph. Each copy has its own state.",
        "A wire drawn in goes to the first free input of its type; pick each input's source below. With several outputs, pick the one the next wire leaves from in Wire from.",
        "On the device each copy costs as many nodes as the logic inside the block.",
    ],
    input: [
        "An input of the block: inside, a source carrying whatever is wired into that input on each copy.",
        "It takes its type from the first wire drawn from it: wired into a NOT it is On/off, into a compare a Number. Type sets it too: it decides what a copy's input takes. Changing it takes its wires off.",
    ],
    output: [
        "An output of the block: wire into it what each copy hands on from that output.",
        "It takes its type from the first wire drawn into it: from a NOT it is On/off, from a scale a Number. Type sets it too: it decides what a copy's output gives. Changing it takes its wires off.",
    ],
};

// Whether the help is unfolded: folded at first, then as it was last left.
let helpOpen = false;

// A node type's help, folded under "How it works"; null if it has none.
function helpSetting(type) {
    const help = NODE_HELP[type];
    if (!help) return null;
    const details = document.createElement('details');
    details.className = 'help';
    details.open = helpOpen;
    details.addEventListener('toggle', () => helpOpen = details.open);
    const summary = document.createElement('summary');
    summary.textContent = 'How it works';
    details.appendChild(summary);
    for (const text of help) {
        const p = document.createElement('p');
        p.textContent = text;
        details.appendChild(p);
    }
    return details;
}

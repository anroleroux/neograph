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
        "Type: On/off or Number decides what a copy's input takes. Changing it takes its wires off.",
    ],
    output: [
        "An output of the block: wire into it what each copy hands on from that output.",
        "Type: On/off or Number decides what a copy's output gives. Changing it takes its wires off.",
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

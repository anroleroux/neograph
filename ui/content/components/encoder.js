// A rotary encoder, such as a KY-040 or an EC11, on two GPIO pins (A and B), with its
// push switch on a third, or none. A source with two outputs: its position, in clicks
// from 0 to its steps (a number), and Pressed (on/off), on while the switch is held.
// The device counts its clicks; a PWM, servo or NeoPixels wired to its position take it
// as a fraction of its steps. Its readings also reach the browser as telemetry, for its
// tile and the nodes wired to it.
class uiEncoder extends uiComponents {
    constructor(x = 0, y = 0) {
        super("encoder", x, y);
        this.label = "Encoder";

        this.tc = MSG_HW_ENCODER;

        this.outputs.push(
            { type: "float", name: "Position", conns: [] },
            { type: "bool", name: "Pressed", conns: [] },
        );
        this.actDir = "outputs";
        this.actNum = 0;            // the output the next wire leaves from

        this.pins = uiEncoder.freePins(this);   // A (CLK), B (DT), its switch (SW) or ENCODER_NO_BUTTON
        this.steps = 20;            // its position's top: a turn of a 20-click knob
        this.detent = ENCODER_DETENT_4;
        this.reverse = false;
        this.wrap = false;          // past its ends it goes on from the other
        this.value = NaN;           // its position; none yet
        this.pressed = null;        // whether its switch is held; null before the device says
        this.unit = 'clicks';
        this.shown = false;         // a tile in the Command view

        this.buildGraphElement();
    }

    // Three pins no other node uses: GPIO 6, 7 and 8 if they are free, otherwise the
    // lowest three free ones.
    static freePins(node) {
        const used = pinsInUse(node);
        const preferred = [6, 7, 8];
        if (preferred.every(pin => !used.has(pin))) return preferred;
        const free = USABLE_PINS.filter(pin => !used.has(pin));
        return free.length >= 3 ? free.slice(0, 3) : preferred;
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">ENC</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // Each setter returns false, and keeps what it had, for a value it doesn't take.

    // Pin k (0 A, 1 B, 2 the switch): a usable one its other pins don't use, from a
    // number or its text; the switch may also be 'none' (or ENCODER_NO_BUTTON). Other
    // pins' readings belong to other nodes, so the old ones are dropped.
    setPin(k, pin) {
        if (k === 2 && (pin === 'none' || pin === ENCODER_NO_BUTTON)) pin = ENCODER_NO_BUTTON;
        else pin = parsePin(pin);
        if (pin === null || !Number.isInteger(k) || k < 0 || k > 2
            || (pin !== ENCODER_NO_BUTTON && this.pins.some((p, j) => j !== k && p === pin))) return false;
        if (pin === this.pins[k]) return;
        this.pins[k] = pin;
        this.signal(0, NaN);
        this.signal(1, NaN);
    }

    // A whole number from 1 to ENCODER_STEPS_MAX.
    setSteps(v) {
        v = String(v).trim();
        if (!/^\d+$/.test(v) || Number(v) < 1 || Number(v) > ENCODER_STEPS_MAX) return false;
        this.steps = Number(v);
    }

    // ENCODER_DETENT_4, _2 or _1.
    setDetent(detent) {
        detent = Number(detent);
        if (![ENCODER_DETENT_4, ENCODER_DETENT_2, ENCODER_DETENT_1].includes(detent)) return false;
        this.detent = detent;
    }

    setWireOutput(output) {
        if (output !== 0 && output !== 1) return false;
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    usedPins() {
        return this.pins.filter(pin => pin !== ENCODER_NO_BUTTON);
    }

    outputValue(output) {
        if (output === 1) return this.pressed == null ? null : Number(this.pressed);
        return this.value;
    }

    // Its signals from the device (PROTOCOL.md, "Telemetry text frames"): its position
    // (output 0), and whether its switch is pressed (output 1).
    signal(output, value) {
        if (output === 1) this.pressed = Number.isNaN(value) ? null : value >= 0.5;
        else this.value = value;
        this.emit(this.outputValue(output), output);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        if (!Number.isFinite(this.value)) return '–';
        return `${this.value} of ${this.steps}${this.pressed ? ", pressed" : ""}`;
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');

        // Each pin is offered the pins no other node, and none of its other pins, uses.
        const used = pinsInUse(this);
        ['A (CLK) pin', 'B (DT) pin', 'Switch (SW) pin'].forEach((label, k) => {
            const own = this.pins[k];
            const pins = USABLE_PINS.filter(pin => pin === own
                || (!used.has(pin) && !this.pins.some((p, j) => j !== k && p === pin)));
            const options = pins.map(pin => [pin, `GPIO ${pin}`]);
            if (k === 2) options.unshift(['none', 'None']);
            settings.appendChild(selectSetting(label, options,
                () => this.pins[k] === ENCODER_NO_BUTTON ? 'none' : this.pins[k],
                (pin) => { this.setPin(k, pin); this.createSettings(); }));
        });

        settings.appendChild(editableSetting('Steps', () => String(this.steps),
            (v) => this.setSteps(v), {type: 'number', min: 1, max: ENCODER_STEPS_MAX, step: 1}));
        settings.appendChild(choiceSetting('A click is',
            {[ENCODER_DETENT_4]: 'Full cycle', [ENCODER_DETENT_2]: 'Half cycle', [ENCODER_DETENT_1]: 'Each edge'},
            () => String(this.detent), (v) => this.setDetent(v)));
        settings.appendChild(choiceSetting('At the ends', {stop: 'Stop', wrap: 'Wrap'},
            () => this.wrap ? 'wrap' : 'stop', (v) => this.wrap = v === 'wrap'));
        settings.appendChild(choiceSetting('Direction', {normal: 'Normal', reverse: 'Reversed'},
            () => this.reverse ? 'reverse' : 'normal', (v) => this.reverse = v === 'reverse'));
        settings.appendChild(choiceSetting('Wire from', {0: 'Position', 1: 'Pressed'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(shownSetting(this));
    }

    // In the Command view, if shown: a tile with the label and the latest position.
    buildUI(commandInterface) {
        if (!this.shown) return;
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = `${this.label}, GPIO ${this.pins[0]} and ${this.pins[1]}`;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    options() {
        return this.detent | (this.reverse ? ENCODER_REVERSE : 0) | (this.wrap ? ENCODER_WRAP : 0);
    }

    // Its pins, its options, then its steps.
    marshal() {
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, ENCODER_PAYLOAD_LEN,
            ...this.pins, this.options(), (this.steps >> 8) & 0xFF, this.steps & 0xFF];
    }

    // Its pins and settings; a payload the device wouldn't run keeps the defaults, and
    // pins another node has keep its own.
    restore(pay, r, len) {
        if (len !== ENCODER_PAYLOAD_LEN) return;
        const pins = [pay[0], pay[1], pay[2]], options = pay[3], steps = pay[4] << 8 | pay[5];
        const button = pins[2] !== ENCODER_NO_BUTTON;
        if (!pinUsable(pins[0]) || !pinUsable(pins[1]) || pins[0] === pins[1]
            || (button && (!pinUsable(pins[2]) || pins[2] === pins[0] || pins[2] === pins[1]))
            || options & ~(ENCODER_DETENT_MASK | ENCODER_REVERSE | ENCODER_WRAP)
            || (options & ENCODER_DETENT_MASK) > ENCODER_DETENT_1 || steps < 1 || steps > ENCODER_STEPS_MAX) return;
        const used = pinsInUse(this);
        if (!pins.some(pin => used.has(pin))) this.pins = pins;
        this.detent = options & ENCODER_DETENT_MASK;
        this.reverse = !!(options & ENCODER_REVERSE);
        this.wrap = !!(options & ENCODER_WRAP);
        this.steps = steps;
    }
}

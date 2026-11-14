// An ultrasonic ranger whose trigger and echo share one pin, such as Seeed's Grove
// Ultrasonic Ranger. A source with two outputs: the distance to the nearest thing in
// cm (a number; its Range when nothing is nearer), and Echo (on/off), on while
// something is within its Range. On the device it drives the outputs wired to it
// directly, its distance as a fraction of its Range; its readings also reach the
// browser as telemetry, for its tile and the nodes wired to it.
class uiUltrasonic extends uiComponents {
    constructor(x = 0, y = 0) {
        super("ultrasonic", x, y);
        this.label = "Ultrasonic";

        this.tc = MSG_HW_ULTRASONIC;

        this.outputs.push(
            { type: "float", name: "cm", conns: [] },
            { type: "bool", name: "Echo", conns: [] },
        );
        this.actDir = "outputs";
        this.actNum = 0;            // the output the next wire leaves from

        this.pin = freePin(this, 16);   // 16, unless another node uses it
        this.range = ULTRASONIC_RANGE_MAX;  // cm: as far as it looks, and its full scale
        this.value = NaN;           // cm; no reading yet
        this.echo = null;           // whether something is within its range; null before the device says
        this.unit = 'cm';
        this.shown = false;         // a tile in the Command view

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">US</text>
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

    // A usable pin, from a number or its text. Another pin's readings belong to other
    // nodes, so the old reading is dropped.
    setPin(pin) {
        pin = parsePin(pin);
        if (pin === null) return false;
        if (pin === this.pin) return;
        this.pin = pin;
        this.signal(0, NaN);
        this.signal(1, NaN);
    }

    // A whole number of cm from ULTRASONIC_RANGE_MIN to ULTRASONIC_RANGE_MAX.
    setRange(v) {
        v = String(v).trim();
        if (!/^\d+$/.test(v) || Number(v) < ULTRASONIC_RANGE_MIN || Number(v) > ULTRASONIC_RANGE_MAX) return false;
        this.range = Number(v);
    }

    setWireOutput(output) {
        if (output !== 0 && output !== 1) return false;
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    usedPins() {
        return [this.pin];
    }

    outputValue(output) {
        if (output === 1) return this.echo == null ? null : Number(this.echo);
        return this.value;
    }

    // Its signals from the device (PROTOCOL.md, "Telemetry text frames"): its distance
    // in cm (output 0), and whether something is within its range (output 1).
    signal(output, value) {
        if (output === 1) this.echo = Number.isNaN(value) ? null : value >= 0.5;
        else this.value = value;
        this.emit(this.outputValue(output), output);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        if (this.echo === false) return `Nothing within ${this.range} cm`;
        return Number.isFinite(this.value) && this.echo ? `${this.value.toFixed(1)} cm` : '–';
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(pinSetting(this, () => this.pin, (pin) => this.setPin(pin)));
        settings.appendChild(editableSetting('Range (cm)', () => String(this.range), (v) => this.setRange(v),
            {type: 'number', min: ULTRASONIC_RANGE_MIN, max: ULTRASONIC_RANGE_MAX, step: 1}));
        settings.appendChild(choiceSetting('Wire from', {0: 'cm', 1: 'Echo'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(shownSetting(this));
    }

    // In the Command view, if shown: a tile with the label and the latest distance.
    buildUI(commandInterface) {
        if (!this.shown) return;
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = `${this.label}, GPIO ${this.pin}`;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    // The pin, then its range.
    marshal() {
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, ULTRASONIC_PAYLOAD_LEN,
            this.pin, (this.range >> 8) & 0xFF, this.range & 0xFF];
    }

    // Its pin and range; a payload the device wouldn't run keeps the defaults, and a
    // pin another node has keeps its own.
    restore(pay, r, len) {
        if (len !== ULTRASONIC_PAYLOAD_LEN) return;
        const range = pay[1] << 8 | pay[2];
        if (range < ULTRASONIC_RANGE_MIN || range > ULTRASONIC_RANGE_MAX) return;
        if (pinUsable(pay[0]) && !pinsInUse(this).has(pay[0])) this.pin = pay[0];
        this.range = range;
    }
}

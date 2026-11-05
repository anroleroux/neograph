// A DS18B20 digital temperature sensor on one GPIO pin, the 1-Wire data line. A
// source with two outputs: its temperature in °C (a number), and Found (on/off), on
// while the sensor answers. On the device it drives the outputs wired to it directly,
// a temperature from 0 °C (0) to DS18B20_FULL °C (1), as the core temperature does;
// its readings also reach the browser as telemetry, for its tile and the nodes wired
// to it.
class uiDS18B20 extends uiComponents {
    constructor(x = 0, y = 0) {
        super("ds18b20", x, y);
        this.label = "DS18B20";

        this.tc = MSG_HW_DS18B20;

        this.outputs.push(
            { type: "float", name: "°C", conns: [] },
            { type: "bool", name: "Found", conns: [] },
        );
        this.actDir = "outputs";
        this.actNum = 0;            // the output the next wire leaves from

        this.pin = freePin(this, 15);   // 15, unless another node uses it
        this.resolution = DS18B20_RES_12;
        this.pullup = false;        // the pin's own pull-up, in place of the 4.7 kΩ resistor
        this.value = NaN;           // °C; no reading yet
        this.found = null;          // whether it answers; null before the device says
        this.unit = '°C';
        this.shown = false;         // a tile in the Command view

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">DS</text>
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

    // DS18B20_RES_9 to DS18B20_RES_12.
    setResolution(res) {
        res = Number(res);
        if (![DS18B20_RES_9, DS18B20_RES_10, DS18B20_RES_11, DS18B20_RES_12].includes(res)) return false;
        this.resolution = res;
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
        if (output === 1) return this.found == null ? null : Number(this.found);
        return this.value;
    }

    // Its signals from the device (PROTOCOL.md, "Telemetry text frames"): its
    // temperature in °C (output 0), and whether the sensor answers (output 1).
    signal(output, value) {
        if (output === 1) this.found = Number.isNaN(value) ? null : value >= 0.5;
        else this.value = value;
        this.emit(this.outputValue(output), output);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        if (this.found === false) return 'No sensor';
        return Number.isFinite(this.value) && this.found ? `${this.value.toFixed(this.resolution >= DS18B20_RES_11 ? 2 : 1)} °C` : '–';
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(pinSetting(this, () => this.pin, (pin) => this.setPin(pin)));
        settings.appendChild(choiceSetting('Resolution',
            {[DS18B20_RES_9]: '9', [DS18B20_RES_10]: '10', [DS18B20_RES_11]: '11', [DS18B20_RES_12]: '12'},
            () => String(this.resolution), (r) => { this.setResolution(r); this.createSettings(); }));
        const note = document.createElement('p');
        note.className = 'setting-note';
        note.textContent = `${9 + this.resolution} bits: ${[0.5, 0.25, 0.125, 0.0625][this.resolution]} °C steps, a reading every `
            + `${[94, 188, 375, 750][this.resolution]} ms`;
        settings.appendChild(note);
        settings.appendChild(choiceSetting('Pull-up', {external: '4.7 kΩ resistor', internal: 'Internal'},
            () => this.pullup ? 'internal' : 'external', (v) => this.pullup = v === 'internal'));
        settings.appendChild(choiceSetting('Wire from', {0: '°C', 1: 'Found'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(shownSetting(this));
    }

    // In the Command view, if shown: a tile with the label and the latest temperature.
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

    // The pin, then its resolution and pull-up.
    marshal() {
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x02,
            this.pin, this.resolution | (this.pullup ? DS18B20_PULLUP : 0)];
    }

    // Its pin and options; a payload the device wouldn't run keeps the defaults, and a
    // pin another node has keeps its own.
    restore(pay, r, len) {
        if (len !== 2 || pay[1] & ~(DS18B20_RES_MASK | DS18B20_PULLUP)) return;
        if (pinUsable(pay[0]) && !pinsInUse(this).has(pay[0])) this.pin = pay[0];
        this.resolution = pay[1] & DS18B20_RES_MASK;
        this.pullup = !!(pay[1] & DS18B20_PULLUP);
    }
}

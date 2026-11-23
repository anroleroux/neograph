// Auto-ranging moves to a less sensitive range above this reading (LIGHT_RAW_HIGH in
// src/light.h); the offline demo reads on the most sensitive range under it.
const LIGHT_RAW_HIGH = 3900;

// A light sensor: a phototransistor (such as a BPT-BP0334) from 3.3 V into a sense node
// on an ADC pin, with load resistors from the sense node: one to GND that is always on
// (or none), and up to LIGHT_LOADS more, each to a GPIO the device drives low to switch
// it in. A range is the load the photocurrent flows through: range 0 the always-on load
// alone, range k load k in parallel with it. With Reflections on, it turns its LED output
// on and off, a scan each, and measures how much of that LED's light something in front
// of it sends back. A source with five outputs: its background light in µA (with its LED
// off), the reflected photocurrent in µA, its ADC reading (0 to ADC_RAW_FULL) and the load
// of the range it read on, in kΩ, all numbers; and its LED, on/off, for a GPO or
// NeoPixels to light the LED with. Its readings reach the browser as telemetry, for its
// tile and the nodes wired to it.
class uiLight extends uiComponents {
    constructor(x = 0, y = 0) {
        super("light", x, y);
        this.label = "Light";

        this.tc = MSG_HW_LIGHT;

        this.outputs.push(
            { type: "float", name: "µA", conns: [] },
            { type: "float", name: "Reflected", conns: [] },
            { type: "float", name: "Raw", conns: [] },
            { type: "float", name: "Load", conns: [] },
            { type: "bool", name: "LED", conns: [] },
        );
        this.actDir = "outputs";
        this.actNum = 0;            // the output the next wire leaves from

        // The first ADC channel whose pin no other node uses.
        this.channel = Math.max(0, [...Array(ADC_CHANNELS).keys()]
            .findIndex(ch => !pinsInUse(this).has(ADC_PIN_FIRST + ch)));
        // Range k's GPIO and its own load in ohms (index 0, the always-on load, has no
        // pin). A load without a pin keeps its ohms, for when it gets one again.
        this.pins = [LIGHT_NO_PIN, ...uiLight.freePins(this, [2, 3, 4], ADC_PIN_FIRST + this.channel)];
        this.ohms = [1000000, 1000, 10000, 100000];
        this.reflect = true;        // it measures reflections, driving its LED output
        this.auto = true;           // it picks its range
        this.range = 0;             // its range when it doesn't
        this.reflectRange = 3;      // the range it measures reflections on: 100 kΩ
        this.settleUs = 1000;       // µs it waits after switching range
        this.samples = 16;          // ADC reads averaged into a reading
        this.bursts = 8;            // LED bursts averaged into a reflection
        this.values = [NaN, NaN, NaN, NaN, NaN];    // its outputs; none yet
        this.unit = 'µA';
        this.shown = false;         // a tile in the Command view
        this.fixRanges();

        this.buildGraphElement();
    }

    // Pins for its loads: each preferred one if no other node (nor `taken`) uses it,
    // otherwise the lowest free one; none if there is none.
    static freePins(node, preferred, taken) {
        const used = pinsInUse(node);
        used.add(taken);
        return preferred.map(pin => {
            const free = !used.has(pin) ? pin : USABLE_PINS.find(p => !used.has(p)) ?? LIGHT_NO_PIN;
            if (free !== LIGHT_NO_PIN) used.add(free);
            return free;
        });
    }

    // Ohms from text such as 1000, 4.7k, 10 k, 1M or 1 MΩ; null if it is not one, or is
    // out of LIGHT_OHMS_MIN to LIGHT_OHMS_MAX.
    static parseOhms(text) {
        const m = String(text).trim().match(/^(\d+(?:\.\d+)?)\s*([kKM]?)\s*(?:Ω|ohms?)?$/);
        if (!m) return null;
        const ohms = Math.round(Number(m[1]) * { '': 1, k: 1e3, K: 1e3, M: 1e6 }[m[2]]);
        return ohms >= LIGHT_OHMS_MIN && ohms <= LIGHT_OHMS_MAX ? ohms : null;
    }

    static formatOhms(ohms) {
        if (ohms >= 1e6) return `${+(ohms / 1e6).toPrecision(4)} MΩ`;
        if (ohms >= 1e3) return `${+(ohms / 1e3).toPrecision(4)} kΩ`;
        return `${+ohms.toPrecision(4)} Ω`;
    }

    // A current in µA to about three figures.
    static formatMicroamps(ua) {
        const a = Math.abs(ua);
        return `${a >= 100 ? ua.toFixed(0) : a >= 10 ? ua.toFixed(1) : a >= 1 ? ua.toFixed(2) : ua.toPrecision(2)} µA`;
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">LS</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // Whether it has range k: its always-on load (0), or load k's pin.
    hasRange(k) {
        return k === 0 ? this.ohms[0] !== 0 : k > 0 && k < LIGHT_RANGES && this.pins[k] !== LIGHT_NO_PIN;
    }

    // The load range k puts on the sense node, in ohms: its own in parallel with the
    // always-on one. As light_load_ohms in src/light.c.
    loadOhms(k) {
        if (!this.hasRange(k)) return 0;
        const base = this.ohms[0];
        if (k === 0) return base;
        return base ? this.ohms[k] * base / (this.ohms[k] + base) : this.ohms[k];
    }

    // Its ranges, most sensitive (the highest load) first.
    ranges() {
        return [0, 1, 2, 3].filter(k => this.hasRange(k)).sort((a, b) => this.loadOhms(b) - this.loadOhms(a) || a - b);
    }

    rangeName(k) {
        const own = k === 0 ? 'always on' : `GPIO ${this.pins[k]}`;
        return `${uiLight.formatOhms(this.loadOhms(k))} (${own})`;
    }

    // A range it has no load for (its pin was taken away) moves to its most sensitive one.
    fixRanges() {
        const first = this.ranges()[0];
        if (!this.hasRange(this.range)) this.range = first;
        if (!this.hasRange(this.reflectRange)) this.reflectRange = first;
    }

    // Each setter returns false, and keeps what it had, for a value it doesn't take.
    // Other pins' readings belong to other nodes, so a new pin drops the old readings.
    dropReadings() {
        for (let k = 0; k < LIGHT_OUTPUTS; k++) this.signal(k, NaN);
    }

    setChannel(channel) {
        channel = String(channel).trim();
        if (!/^\d+$/.test(channel) || Number(channel) >= ADC_CHANNELS) return false;
        const pin = ADC_PIN_FIRST + Number(channel);
        if (this.pins.includes(pin)) return false;
        if (Number(channel) === this.channel) return;
        this.channel = Number(channel);
        this.dropReadings();
    }

    // A usable pin its other pins don't use, or 'none' (LIGHT_NO_PIN).
    pinFrom(pin, own) {
        if (pin === 'none' || pin === LIGHT_NO_PIN) return LIGHT_NO_PIN;
        pin = parsePin(pin);
        if (pin === null || pin === own) return pin;
        const mine = [ADC_PIN_FIRST + this.channel, ...this.pins.slice(1)];
        return mine.includes(pin) ? null : pin;
    }

    // Load k's pin (1 to LIGHT_LOADS). It keeps at least one range.
    setLoadPin(k, pin) {
        if (!Number.isInteger(k) || k < 1 || k > LIGHT_LOADS) return false;
        pin = this.pinFrom(pin, this.pins[k]);
        if (pin === null) return false;
        if (pin === this.pins[k]) return;
        const was = this.pins[k];
        this.pins[k] = pin;
        if (!this.ranges().length) {
            this.pins[k] = was;
            return false;
        }
        this.fixRanges();
        this.dropReadings();
    }

    setLoadOhms(k, text) {
        const ohms = uiLight.parseOhms(text);
        if (!Number.isInteger(k) || k < 1 || k > LIGHT_LOADS || ohms === null) return false;
        this.ohms[k] = ohms;
    }

    // The always-on load, or 'none' (0), if it has another range.
    setBaseOhms(text) {
        const none = ['none', '0', ''].includes(String(text).trim().toLowerCase());
        const ohms = none ? 0 : uiLight.parseOhms(text);
        if (ohms === null || (!ohms && !this.ranges().some(k => k !== 0))) return false;
        this.ohms[0] = ohms;
        this.fixRanges();
    }

    // Reflections on or off: off, its LED output stays off and Reflected reads 0.
    setReflect(on) {
        this.reflect = !!on;
        if (!this.reflect) this.signal(1, NaN);
    }

    // A range it has: 0 to LIGHT_RANGES - 1.
    setRange(k) {
        k = Number(k);
        if (!this.hasRange(k)) return false;
        this.range = k;
    }

    setReflectRange(k) {
        k = Number(k);
        if (!this.hasRange(k)) return false;
        this.reflectRange = k;
    }

    // A whole number from `min` to `max`, from a number or its text; null if it is not one.
    static whole(v, min, max) {
        v = String(v).trim();
        return /^\d+$/.test(v) && Number(v) >= min && Number(v) <= max ? Number(v) : null;
    }

    setSettle(v) {
        v = uiLight.whole(v, 0, LIGHT_SETTLE_MAX_US);
        if (v === null) return false;
        this.settleUs = v;
    }

    setSamples(v) {
        v = uiLight.whole(v, 1, LIGHT_SAMPLES_MAX);
        if (v === null) return false;
        this.samples = v;
    }

    setBursts(v) {
        v = uiLight.whole(v, 1, LIGHT_BURSTS_MAX);
        if (v === null) return false;
        this.bursts = v;
    }

    setWireOutput(output) {
        if (!Number.isInteger(output) || output < 0 || output >= LIGHT_OUTPUTS) return false;
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    usedPins() {
        return [ADC_PIN_FIRST + this.channel, ...this.pins].filter(pin => pin !== LIGHT_NO_PIN);
    }

    outputValue(output) {
        if (output === LIGHT_LED_OUTPUT) return Number.isNaN(this.values[output]) ? null : Number(this.values[output] >= 0.5);
        return this.values[output];
    }

    // Its outputs' units: µA, µA, ADC counts (none), kΩ, and none for its LED.
    outputUnit(output) {
        return ['µA', 'µA', '', 'kΩ', ''][output];
    }

    // Its signals from the device (PROTOCOL.md, "Telemetry text frames"): its background
    // µA (output 0), reflected µA (1), reading (2), load in kΩ (3) and LED (4).
    signal(output, value) {
        if (!(output >= 0 && output < LIGHT_OUTPUTS)) return;
        this.values[output] = value;
        this.emit(this.outputValue(output), output);
        if (output !== LIGHT_LED_OUTPUT && this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        const [ua, reflected, raw, kohms] = this.values;
        if (!Number.isFinite(ua)) return '–';
        let text = uiLight.formatMicroamps(ua);
        if (Number.isFinite(kohms) && Number.isFinite(raw)) text += ` (${uiLight.formatOhms(kohms * 1000)}, raw ${Math.round(raw)})`;
        if (this.reflect && Number.isFinite(reflected)) text += `, reflected ${uiLight.formatMicroamps(reflected)}`;
        return text;
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        const used = pinsInUse(this);
        const redraw = () => this.createSettings();

        // Its pins: each is offered the pins no other node, and none of its other pins, uses.
        const mine = (own) => [ADC_PIN_FIRST + this.channel, ...this.pins.slice(1)].filter(p => p !== own);
        const pinOptions = (own) => [['none', 'None'],
            ...USABLE_PINS.filter(pin => pin === own || (!used.has(pin) && !mine(own).includes(pin))).map(pin => [pin, `GPIO ${pin}`])];
        const pinValue = (pin) => pin === LIGHT_NO_PIN ? 'none' : pin;

        const channels = Array.from({ length: ADC_CHANNELS }, (_, ch) => ch)
            .filter(ch => ch === this.channel || (!used.has(ADC_PIN_FIRST + ch) && !mine(-1).includes(ADC_PIN_FIRST + ch)))
            .map(ch => [ch, `ADC${ch} · GPIO ${ADC_PIN_FIRST + ch}`]);
        settings.appendChild(selectSetting('Sense pin', channels, () => this.channel, (ch) => { this.setChannel(ch); redraw(); }));

        for (let k = 1; k <= LIGHT_LOADS; k++) {
            settings.appendChild(selectSetting(`Load ${k} pin`, pinOptions(this.pins[k]), () => pinValue(this.pins[k]),
                (pin) => { this.setLoadPin(k, pin); redraw(); }));
            if (this.pins[k] !== LIGHT_NO_PIN) {
                settings.appendChild(editableSetting(`Load ${k}`, () => uiLight.formatOhms(this.ohms[k]),
                    (v) => { if (this.setLoadOhms(k, v) === false) return false; redraw(); },
                    {title: 'Ohms, such as 1k, 4.7k or 1M'}));
            }
        }
        settings.appendChild(editableSetting('Always-on load', () => this.ohms[0] ? uiLight.formatOhms(this.ohms[0]) : 'none',
            (v) => { if (this.setBaseOhms(v) === false) return false; redraw(); },
            {title: 'Ohms, such as 1M; none if it has none'}));

        settings.appendChild(choiceSetting('Ranging', {auto: 'Auto', fixed: 'Fixed'},
            () => this.auto ? 'auto' : 'fixed', (v) => { this.auto = v === 'auto'; redraw(); }));
        const rangeOptions = this.ranges().map(k => [k, this.rangeName(k)]);
        if (!this.auto) settings.appendChild(selectSetting('Range', rangeOptions, () => this.range, (k) => this.setRange(k)));
        settings.appendChild(editableSetting('Settle (µs)', () => String(this.settleUs), (v) => this.setSettle(v),
            {type: 'number', min: 0, max: LIGHT_SETTLE_MAX_US, step: 1}));
        settings.appendChild(editableSetting('Samples', () => String(this.samples), (v) => this.setSamples(v),
            {type: 'number', min: 1, max: LIGHT_SAMPLES_MAX, step: 1}));

        settings.appendChild(choiceSetting('Reflections', {on: 'On', off: 'Off'},
            () => this.reflect ? 'on' : 'off', (v) => { this.setReflect(v === 'on'); redraw(); }));
        if (this.reflect) {
            settings.appendChild(selectSetting('Reflection range', rangeOptions, () => this.reflectRange, (k) => this.setReflectRange(k)));
            settings.appendChild(editableSetting('Bursts', () => String(this.bursts), (v) => this.setBursts(v),
                {type: 'number', min: 1, max: LIGHT_BURSTS_MAX, step: 1}));
        }

        settings.appendChild(choiceSetting('Wire from', {0: 'µA', 1: 'Reflected', 2: 'Raw', 3: 'Load', 4: 'LED'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(shownSetting(this));
    }

    // In the Command view, if shown: a tile with the label and the latest readings.
    buildUI(commandInterface) {
        if (!this.shown) return;
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = `${this.label}, ADC${this.channel} on GPIO ${ADC_PIN_FIRST + this.channel}`;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    options() {
        return (this.auto ? LIGHT_AUTO : 0) | (this.reflect ? LIGHT_REFLECT : 0);
    }

    // See MSG_HW_LIGHT in protocol.js: a load without a pin is 0 ohms.
    payload() {
        const u32 = (v) => [(v >>> 24) & 0xFF, (v >>> 16) & 0xFF, (v >>> 8) & 0xFF, v & 0xFF];
        const u16 = (v) => [(v >> 8) & 0xFF, v & 0xFF];
        return [this.channel, this.options(), this.range, this.reflectRange, ...this.pins.slice(1),
            ...[0, 1, 2, 3].flatMap(k => u32(k === 0 || this.hasRange(k) ? this.ohms[k] : 0)),
            ...u16(this.settleUs), this.samples, this.bursts];
    }

    marshal() {
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, LIGHT_PAYLOAD_LEN,
            ...this.payload()];
    }

    // Its settings, from a payload the device would run (as light_payload_read in
    // src/protocol.h); any other, or one with a pin another node has, keeps the defaults.
    restore(pay, r, len) {
        if (len !== LIGHT_PAYLOAD_LEN) return;
        const u32 = (i) => (pay[i] << 24 >>> 0) + (pay[i + 1] << 16) + (pay[i + 2] << 8) + pay[i + 3];
        const channel = pay[0], options = pay[1], range = pay[2], reflectRange = pay[3];
        const pins = [LIGHT_NO_PIN, pay[4], pay[5], pay[6]];
        const ohms = [0, 1, 2, 3].map(k => u32(7 + 4 * k));
        const settleUs = pay[23] << 8 | pay[24], samples = pay[25], bursts = pay[26];

        if (channel >= ADC_CHANNELS || options & ~(LIGHT_AUTO | LIGHT_REFLECT)) return;
        const taken = [ADC_PIN_FIRST + channel, ...pins.slice(1)].filter(pin => pin !== LIGHT_NO_PIN);
        if (!taken.every(pinUsable) || new Set(taken).size !== taken.length) return;
        const fitted = (k) => k === 0 ? ohms[0] !== 0 : pins[k] !== LIGHT_NO_PIN;
        const ohmsOk = (k) => fitted(k) ? ohms[k] >= LIGHT_OHMS_MIN && ohms[k] <= LIGHT_OHMS_MAX : ohms[k] === 0;
        if (![0, 1, 2, 3].every(ohmsOk) || ![0, 1, 2, 3].some(fitted)) return;
        if (range >= LIGHT_RANGES || reflectRange >= LIGHT_RANGES) return;
        if (!(options & LIGHT_AUTO) && !fitted(range)) return;
        if (options & LIGHT_REFLECT && !fitted(reflectRange)) return;
        if (settleUs > LIGHT_SETTLE_MAX_US || samples < 1 || samples > LIGHT_SAMPLES_MAX
            || bursts < 1 || bursts > LIGHT_BURSTS_MAX) return;
        const used = pinsInUse(this);
        if (taken.some(pin => used.has(pin))) return;

        Object.assign(this, { channel, range, reflectRange, pins, settleUs, samples, bursts });
        // A load without a pin keeps its default ohms, for when it gets one.
        ohms.forEach((v, k) => { if (k === 0 || fitted(k)) this.ohms[k] = v; });
        this.auto = !!(options & LIGHT_AUTO);
        this.reflect = !!(options & LIGHT_REFLECT);
        this.fixRanges();
    }
}

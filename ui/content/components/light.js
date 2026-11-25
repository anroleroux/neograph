// Auto-ranging moves to a less sensitive range above this reading (LIGHT_RAW_HIGH in
// src/light.h); the offline demo reads on the most sensitive range under it.
const LIGHT_RAW_HIGH = 3900;

// A light sensor: a phototransistor (such as a BPT-BP0334) from 3.3 V into a sense node
// on an ADC pin, with load resistors from the sense node: one to GND that is always on
// (or none), and up to LIGHT_LOADS more, each to a GPIO the device drives low to switch
// it in. A range is the load the photocurrent flows through: range 0 the always-on load
// alone, range k load k in parallel with it. It reads the background light. With object
// detection it turns its LED output on and off, a scan each, and measures how much of
// that LED's light an object in front of it sends back; with directional detection too,
// while there is an object, it also flashes a left and a right LED in turn, and gives the
// object's direction. A source with eight outputs (LIGHT_OUT_): the background, reflected,
// left and right light in µA and the direction, -1 to 1, all numbers; then its LED, left
// LED and right LED, on/off, for GPOs or NeoPixels to light the LEDs with. Its readings
// reach the browser as telemetry, for its tile and the nodes wired to it; two more
// signals, its ADC reading and the load it read through, are for its tile only.
class uiLight extends uiComponents {
    constructor(x = 0, y = 0) {
        super("lightsensor", x, y);     // not "light": that is the UI light's (uiIndicator)
        this.label = "Light";

        this.tc = MSG_HW_LIGHT;

        this.outputs.push(
            ...uiLight.OUTPUT_NAMES.map((name, k) => ({ type: k >= LIGHT_OUT_LED ? "bool" : "float", name, conns: [] })),
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
        this.reflect = false;       // object detection: it flashes its LED and measures reflections
        this.direction = false;     // directional detection, with an object: its left and right LEDs too
        this.minUa = 1;             // µA: the least reflection that is an object
        this.everyMs = 0;           // ms from one reflection reading's start to the next's; 0: one after another
        this.auto = true;           // it picks its range
        this.range = 0;             // its range when it doesn't
        this.reflectRange = 3;      // the range it measures reflections on: 100 kΩ
        this.settleUs = 1000;       // µs it waits after switching range
        this.samples = 16;          // ADC reads averaged into a reading
        this.bursts = 4;            // LED bursts averaged into a reflection
        this.values = Array(LIGHT_SIGNALS).fill(NaN);   // its outputs, then its reading and load; none yet
        this.unit = 'µA';
        this.shown = false;         // a tile in the Command view
        this.fixRanges();

        this.buildGraphElement();
    }

    static OUTPUT_NAMES = ['µA', 'Reflected', 'Left', 'Right', 'Direction', 'LED', 'LED Left', 'LED Right'];

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
        for (let k = 0; k < LIGHT_SIGNALS; k++) this.signal(k, NaN);
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

    // Object detection on or off. Off, its LEDs stay off, its reflections and direction
    // read 0, and directional detection goes off too.
    setReflect(on) {
        this.reflect = !!on;
        if (this.reflect) return;
        this.direction = false;
        for (let k = LIGHT_OUT_REFLECTED; k < LIGHT_OUTPUTS; k++) this.signal(k, NaN);
    }

    // Directional detection on or off: only with object detection.
    setDirection(on) {
        if (on && !this.reflect) return false;
        this.direction = !!on;
        if (!this.direction) [LIGHT_OUT_LEFT, LIGHT_OUT_RIGHT, LIGHT_OUT_DIRECTION, LIGHT_OUT_LED_LEFT, LIGHT_OUT_LED_RIGHT]
            .forEach(k => this.signal(k, NaN));
    }

    // The least reflection that is an object, µA: a number 0 or more, as a float32 keeps it.
    setMinReflection(v) {
        v = String(v).trim();
        if (!/^\d*\.?\d+(e[+-]?\d+)?$/i.test(v)) return false;
        v = Math.fround(Number(v));
        if (!Number.isFinite(v) || v < 0) return false;
        this.minUa = v;
    }

    // Seconds from one reflection reading to the next, to the ms, 0 to LIGHT_EVERY_MAX_MS /
    // 1000: its LEDs rest off between them. 0: one after another.
    setEvery(v) {
        v = String(v).trim();
        if (!/^\d*\.?\d+$/.test(v)) return false;
        const ms = Math.round(Number(v) * 1000);
        if (ms > LIGHT_EVERY_MAX_MS) return false;
        this.everyMs = ms;
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
        if (output >= LIGHT_OUT_LED && output < LIGHT_OUTPUTS) return Number.isNaN(this.values[output]) ? null : Number(this.values[output] >= 0.5);
        return this.values[output];
    }

    // Its outputs' units: µA for its four lights, none for its direction and LEDs.
    outputUnit(output) {
        return output <= LIGHT_OUT_RIGHT ? 'µA' : '';
    }

    // Its signals from the device (PROTOCOL.md, "Telemetry text frames"): its outputs
    // (LIGHT_OUT_), handed to the nodes wired to them, then its reading and load
    // (LIGHT_SIG_RAW, LIGHT_SIG_LOAD), for its tile only.
    signal(output, value) {
        if (!(output >= 0 && output < LIGHT_SIGNALS)) return;
        this.values[output] = value;
        if (output < LIGHT_OUTPUTS) this.emit(this.outputValue(output), output);
        if ((output < LIGHT_OUT_LED || output >= LIGHT_OUTPUTS) && this.tileState?.isConnected)
            setTileText(this.tileState, this.reading());
    }

    // Whether there is an object: its reflection is at least its minimum.
    object() {
        return this.reflect && this.values[LIGHT_OUT_REFLECTED] >= this.minUa;
    }

    reading() {
        const [ua, reflected, left, right] = this.values;
        if (!Number.isFinite(ua)) return '–';
        const f = uiLight.formatMicroamps;
        let text = f(ua);
        const raw = this.values[LIGHT_SIG_RAW], kohms = this.values[LIGHT_SIG_LOAD];
        if (Number.isFinite(raw) && Number.isFinite(kohms)) text += ` (${uiLight.formatOhms(kohms * 1000)}, raw ${Math.round(raw)})`;
        if (this.reflect && Number.isFinite(reflected)) text += `, reflected ${f(reflected)}`;
        if (this.direction && this.object() && Number.isFinite(left) && Number.isFinite(right))
            text += `, left ${f(left)}, right ${f(right)}`;
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

        settings.appendChild(choiceSetting('Object detection', {on: 'On', off: 'Off'},
            () => this.reflect ? 'on' : 'off', (v) => { this.setReflect(v === 'on'); redraw(); }));
        if (this.reflect) {
            settings.appendChild(selectSetting('Reflection range', rangeOptions, () => this.reflectRange, (k) => this.setReflectRange(k)));
            settings.appendChild(editableSetting('Bursts', () => String(this.bursts), (v) => this.setBursts(v),
                {type: 'number', min: 1, max: LIGHT_BURSTS_MAX, step: 1}));
            settings.appendChild(editableSetting('Measure every (s)', () => this.everyMs ? String(this.everyMs / 1000) : '0',
                (v) => this.setEvery(v), {type: 'number', min: 0, max: LIGHT_EVERY_MAX_MS / 1000, step: 'any',
                    title: '0: one reading after another; otherwise its LEDs rest off between readings'}));
            settings.appendChild(editableSetting('Min reflection (µA)', () => String(+this.minUa.toPrecision(6)),
                (v) => this.setMinReflection(v), {type: 'number', min: 0, step: 'any'}));
            settings.appendChild(choiceSetting('Directional', {on: 'On', off: 'Off'},
                () => this.direction ? 'on' : 'off', (v) => this.setDirection(v === 'on')));
        }

        settings.appendChild(selectSetting('Wire from', uiLight.OUTPUT_NAMES.map((name, k) => [k, name]),
            () => this.actNum, (o) => this.setWireOutput(Number(o))));
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
        setTileText(this.tileState, this.reading());
        commandInterface.appendChild(control);
    }

    options() {
        return (this.auto ? LIGHT_AUTO : 0) | (this.reflect ? LIGHT_REFLECT : 0)
            | (this.reflect && this.direction ? LIGHT_DIRECTION : 0);
    }

    // See MSG_HW_LIGHT in protocol.js: a load without a pin is 0 ohms.
    payload() {
        const u32 = (v) => [(v >>> 24) & 0xFF, (v >>> 16) & 0xFF, (v >>> 8) & 0xFF, v & 0xFF];
        const u16 = (v) => [(v >> 8) & 0xFF, v & 0xFF];
        return [this.channel, this.options(), this.range, this.reflectRange, ...this.pins.slice(1),
            ...[0, 1, 2, 3].flatMap(k => u32(k === 0 || this.hasRange(k) ? this.ohms[k] : 0)),
            ...u16(this.settleUs), this.samples, this.bursts, ...float32Bytes(this.minUa), ...u16(this.everyMs)];
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
        const settleUs = pay[23] << 8 | pay[24], samples = pay[25], bursts = pay[26], minUa = float32At(pay, 27);
        const everyMs = pay[31] << 8 | pay[32];

        if (channel >= ADC_CHANNELS || options & ~(LIGHT_AUTO | LIGHT_REFLECT | LIGHT_DIRECTION)) return;
        if (options & LIGHT_DIRECTION && !(options & LIGHT_REFLECT)) return;
        if (!Number.isFinite(minUa) || !(minUa >= 0) || everyMs > LIGHT_EVERY_MAX_MS) return;
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

        Object.assign(this, { channel, range, reflectRange, pins, settleUs, samples, bursts, minUa, everyMs });
        // A load without a pin keeps its default ohms, for when it gets one.
        ohms.forEach((v, k) => { if (k === 0 || fitted(k)) this.ohms[k] = v; });
        this.auto = !!(options & LIGHT_AUTO);
        this.reflect = !!(options & LIGHT_REFLECT);
        this.direction = !!(options & LIGHT_DIRECTION);
        this.fixRanges();
    }
}

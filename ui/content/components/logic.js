// How long the device's state of a logic node holds after its last report: the
// device reports each one at least every second (PROTOCOL.md, "Telemetry text frames").
const LOGIC_DEVICE_FRESH_MS = 2500;

// Logic: nodes the device runs, every scan, without the browser (see PROTOCOL.md,
// "Logic"). Each reads one or more sources and is a source itself, boolean or (a
// scale, min or max) floating, so it is both: wires come into it, and leave it
// for outputs or other logic.
//
// The device first, the browser as a fallback. While the device reports a
// node's state (its `sig` telemetry), the node shows that. Otherwise (no device,
// a node not saved yet, a device that went quiet) the browser runs the same
// logic on the values its sources have in the browser, so a graph can be tried
// out without hardware. Either way the node hands its state on to the nodes
// wired to it: lights, and other logic.
class uiLogic extends uiComponents {
    constructor(type, tc, text, label, input, x = 0, y = 0) {
        super(type, x, y);
        this.label = label;
        this.text = text;           // shown in the node

        this.tc = tc;

        // One input, taking `input.max` wires of `input.type`, and one output:
        // boolean, or floating if `input.out` says so.
        this.floating = input.out === "float";
        this.inputs.push({ type: input.type, conns: [], max: input.max });
        this.outputs.push({ type: this.floating ? "float" : "bool", conns: [] });
        this.actDir = "both";
        this.actNum = 0;

        this.value = null;          // the state shown: true or false, or a number; null before there is one
        this.browserValue = null;   // the state the browser works out
        this.deviceValue = null;    // the state the device last reported
        this.deviceAt = 0;          // when, by performance.now(); 0 never
        this.shown = false;         // a tile in the Command view

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="-5" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7"></text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7"></text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <circle cx="24" cy="0" r="4" fill="none" stroke="#BFCDE7" stroke-width="1.5" opacity="0.6" stroke-dasharray="2 2" />
            <title>Run in the browser</title>
        `
        g.children[1].textContent = this.text;
        g.children[2].textContent = this.label;
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
        this.gElmState = g.children[5];
    }

    // The text in the node, such as a compare's "> 40".
    setText(text) {
        this.text = text;
        this.gElm.children[1].textContent = text;
    }

    outputValue(output) {
        return this.value == null ? null : Number(this.value);
    }

    // A boolean node's state as text, or a floating one's value.
    static text(v, floating) {
        if (v == null) return '–';
        return floating ? String(Number(v.toPrecision(4))) : v ? 'On' : 'Off';
    }

    // Whether the device has reported this node's state lately.
    fromDevice() {
        return this.deviceAt > 0 && performance.now() - this.deviceAt < LOGIC_DEVICE_FRESH_MS;
    }

    // Its signal from the device (PROTOCOL.md, "Telemetry text frames"): 1 on, 0
    // off. It is shown while fresh; once stale, the browser's state is.
    signal(output, value) {
        this.deviceSignal(output, value);
        this.deviceAt = performance.now();
        clearTimeout(this.staleTimer);
        this.staleTimer = setTimeout(() => this.show(), LOGIC_DEVICE_FRESH_MS + 50);
        this.show();
    }

    // The device's value of one of its outputs: most have one.
    deviceSignal(output, value) {
        this.deviceValue = this.floating ? value : value >= 0.5;
    }

    // The value a wire brings, as a number: its source's, or through a feedback
    // wire what it last handed on. A source with no value yet, or no wire, reads
    // 0, as on the device.
    static connValue(conn) {
        const v = !conn ? null : conn.feedback ? conn.held : conn.start.node.outputValue(conn.start.output);
        return v == null || !Number.isFinite(Number(v)) ? 0 : Number(v);
    }

    // The value of this node's k-th wire in (see connValue).
    inputValue(k = 0) {
        return uiLogic.connValue(this.inConns()[k]);
    }

    // A boolean input is on from 0.5, as on the device.
    static isOn(v) { return v >= 0.5; }

    // The browser's state of this node from its inputs now (see each kind).
    compute() { return false; }

    // Work out the browser's state again, and show it if it is the one shown.
    evaluate() {
        this.browserValue = this.compute();
        this.show();
    }

    // Show the device's state while it is fresh, else the browser's, and hand a
    // change on to the wired nodes. A dashed dot marks the browser's.
    show() {
        const device = this.fromDevice();
        const on = device ? this.deviceValue : this.browserValue;
        this.gElmState.setAttribute('fill', on === true ? '#4CAF50' : 'none');
        this.gElmState.setAttribute('stroke-dasharray', device ? 'none' : '2 2');
        this.gElm.querySelector('title').textContent = device ? 'Run on the device' : 'Run in the browser';
        if (this.tileState?.isConnected) this.tileState.textContent = uiLogic.text(on, this.floating);
        if (on === this.value) return;
        this.value = on;
        this.emit(this.outputValue(0));
    }

    // A source wired into it has a new value: once the change has reached all its
    // inputs (see evaluateLater). A wire came in or went: at once. A wire coming
    // or going is not an input going on (see rewired).
    receive(value) { evaluateLater(this); }
    addEdge(e) {
        super.addEdge(e);
        if (e.end.node !== this) return;
        this.rewired();
        this.evaluate();
    }
    edgeRemoved(e) {
        this.rewired();
        this.evaluate();
    }

    // Its wires in changed: a node that acts on its input going on takes the
    // input as it is now.
    rewired() {}

    reading() {
        return uiLogic.text(this.value, this.floating);
    }

    remove() {
        clearTimeout(this.staleTimer);
        super.remove();
    }

    createSettings() {
        super.createSettings();
        this.logicSettings(document.getElementById('settings'));
        document.getElementById('settings').appendChild(shownSetting(this));
    }

    // Settings of its own, before the Command view choice.
    logicSettings(settings) {}

    // In the Command view, if shown: a tile with the label and the latest state.
    buildUI(commandInterface) {
        if (!this.shown) return;
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    // The message header, then the payload.
    message(payload) {
        return [this.tc, MSG_FLAG_OUTPUT | (this.shown ? MSG_FLAG_SHOWN : 0), (this.id >> 8) & 0xFF, this.id & 0xFF,
            payload.length, ...payload];
    }

    marshal() {
        return this.message(this.sourceRef());
    }
}

// A number as a 32-bit big-endian IEEE 754 float, and back.
function float32Bytes(v) {
    const view = new DataView(new ArrayBuffer(4));
    view.setFloat32(0, v);
    return [...new Uint8Array(view.buffer)];
}
function float32At(bytes, i) {
    return new DataView(Uint8Array.from(bytes.slice(i, i + 4)).buffer).getFloat32(0);
}
const uint32Bytes = (v) => [(v >>> 24) & 0xFF, (v >>> 16) & 0xFF, (v >>> 8) & 0xFF, v & 0xFF];
const uint32At = (bytes, i) => ((bytes[i] << 24) | (bytes[i + 1] << 16) | (bytes[i + 2] << 8) | bytes[i + 3]) >>> 0;

// A number as the device holds it, a 32-bit float, shown with up to 6 digits.
const showFloat = (v) => String(Number(v.toPrecision(6)));

// Seconds, to the millisecond, as milliseconds from minMs to a day; null if not.
function parseSeconds(v, minMs) {
    v = String(v).trim();
    if (!/^\d+(\.\d{0,3})?$|^\.\d{1,3}$/.test(v)) return null;
    const ms = Math.round(Number(v) * 1000);
    return ms >= minMs && ms <= DELAY_MAX_MS ? ms : null;
}

// A compare: on while a floating source (a slider, an ADC, the core temperature)
// is above its threshold, or below it. Its hysteresis keeps it from flickering:
// above 40 with a hysteresis of 1 goes off only below 39.
class uiCompare extends uiLogic {
    constructor(x = 0, y = 0) {
        super("compare", MSG_LOGIC_COMPARE, "", "Compare", { type: "float", max: 1 }, x, y);
        this.below = false;
        this.threshold = 0.5;
        this.hysteresis = 0;
        this.showCondition();
        this.evaluate();
    }

    // On past the threshold, and off only back past it by the hysteresis.
    compute() {
        const v = this.inputValue(0), on = this.browserValue === true;
        if (this.below) return on ? v <= this.threshold + this.hysteresis : v < this.threshold;
        return on ? v >= this.threshold - this.hysteresis : v > this.threshold;
    }

    showCondition() {
        this.setText(`${this.below ? '<' : '>'} ${showFloat(this.threshold)}`);
    }

    setCondition(condition) {
        if (condition !== 'above' && condition !== 'below') return false;
        this.below = condition === 'below';
        this.showCondition();
        this.evaluate();
    }

    // A finite number, as a 32-bit float; null if it isn't one.
    static parse(v) {
        v = String(v).trim();
        if (v === '' || !Number.isFinite(Number(v))) return null;
        const f = Math.fround(Number(v));
        return Number.isFinite(f) ? f : null;
    }

    setThreshold(v) {
        v = uiCompare.parse(v);
        if (v === null) return false;
        this.threshold = v;
        this.showCondition();
        this.evaluate();
    }

    setHysteresis(v) {
        v = uiCompare.parse(v);
        if (v === null || v < 0) return false;
        this.hysteresis = v;
        this.evaluate();
    }

    // The unit of the source wired in, such as °C, for the settings' labels.
    unitText() {
        const unit = this.inConns()[0]?.start.node.unit;
        return unit ? ` (${unit})` : '';
    }

    logicSettings(settings) {
        settings.appendChild(choiceSetting('On while', {above: 'Above', below: 'Below'},
            () => this.below ? 'below' : 'above', (c) => this.setCondition(c)));
        settings.appendChild(editableSetting('Threshold' + this.unitText(), () => showFloat(this.threshold),
            (v) => this.setThreshold(v), {type: 'number', step: 'any'}));
        settings.appendChild(editableSetting('Hysteresis' + this.unitText(), () => showFloat(this.hysteresis),
            (v) => this.setHysteresis(v), {type: 'number', min: 0, step: 'any'}));
    }

    // Its source, condition, threshold and hysteresis.
    marshal() {
        return this.message([...this.sourceRef(), this.below ? COMPARE_BELOW : COMPARE_ABOVE,
            ...float32Bytes(this.threshold), ...float32Bytes(this.hysteresis)]);
    }

    // From a saved payload after its source reference, at r.
    restore(pay, r, len) {
        if (len !== r + 9) return;
        this.setCondition(pay[r] === COMPARE_BELOW ? 'below' : 'above');
        this.setThreshold(float32At(pay, r + 1));
        this.setHysteresis(float32At(pay, r + 5));
    }
}

// An AND or an OR of up to LOGIC_INPUTS_MAX boolean sources. Only the wired
// inputs count; with none, it is off.
class uiGate extends uiLogic {
    constructor(and, x = 0, y = 0) {
        super(and ? "and" : "or", and ? MSG_LOGIC_AND : MSG_LOGIC_OR, and ? "AND" : "OR",
            and ? "AND" : "OR", { type: "bool", max: LOGIC_INPUTS_MAX }, x, y);
        this.evaluate();
    }

    // All (AND) or any (OR) of the wired inputs on; with none, off.
    compute() {
        const ins = this.inConns().map((c, k) => uiLogic.isOn(this.inputValue(k)));
        return ins.length > 0 && (this.tc === MSG_LOGIC_AND ? ins.every(Boolean) : ins.some(Boolean));
    }

    // A source reference for each wire in; one to nothing if there are none.
    marshal() {
        const refs = this.inConns().flatMap(connRef);
        return this.message(refs.length ? refs : refBytes(null));
    }
}
class uiAnd extends uiGate { constructor(x = 0, y = 0) { super(true, x, y); } }
class uiOr extends uiGate { constructor(x = 0, y = 0) { super(false, x, y); } }

// A NOT: on while its boolean source is off, and while nothing is wired in.
class uiNot extends uiLogic {
    constructor(x = 0, y = 0) {
        super("not", MSG_LOGIC_NOT, "NOT", "NOT", { type: "bool", max: 1 }, x, y);
        this.evaluate();
    }

    compute() { return !uiLogic.isOn(this.inputValue(0)); }
}

// A delay: its boolean source, once the source has been on for the on delay,
// and off for the off delay. A shorter blip doesn't get through.
class uiDelay extends uiLogic {
    constructor(x = 0, y = 0) {
        super("delay", MSG_LOGIC_DELAY, "DLY", "Delay", { type: "bool", max: 1 }, x, y);
        this.holdsState = true;
        this.onMs = 0;
        this.offMs = 0;
        this.pending = null;        // the state it is waiting to take, or null
        this.evaluate();
    }

    // Its state now; when its input differs, a timer takes the input's state
    // after the on or off delay, unless the input goes back first.
    compute() {
        const input = uiLogic.isOn(this.inputValue(0)), now = this.browserValue === true;
        if (input === now) {
            clearTimeout(this.timer);
            this.pending = null;
            return now;
        }
        if (this.pending === input) return now;        // already waiting for it
        clearTimeout(this.timer);
        const ms = input ? this.onMs : this.offMs;
        if (ms === 0) {
            this.pending = null;
            return input;
        }
        this.pending = input;
        this.timer = setTimeout(() => {
            this.pending = null;
            this.browserValue = input;
            this.show();
        }, ms);
        return now;
    }

    setOnDelay(s) { const ms = parseSeconds(s, 0); if (ms === null) return false; this.onMs = ms; }
    setOffDelay(s) { const ms = parseSeconds(s, 0); if (ms === null) return false; this.offMs = ms; }

    // Taken out of the graph: its timer with it.
    remove() {
        clearTimeout(this.timer);
        super.remove();
    }

    logicSettings(settings) {
        settings.appendChild(editableSetting('On delay (s)', () => String(this.onMs / 1000),
            (v) => this.setOnDelay(v), {type: 'number', min: 0, step: 'any'}));
        settings.appendChild(editableSetting('Off delay (s)', () => String(this.offMs / 1000),
            (v) => this.setOffDelay(v), {type: 'number', min: 0, step: 'any'}));
    }

    // Its source, then its delays in milliseconds.
    marshal() {
        return this.message([...this.sourceRef(), ...uint32Bytes(this.onMs), ...uint32Bytes(this.offMs)]);
    }

    // From a saved payload after its source reference, at r.
    restore(pay, r, len) {
        if (len !== r + 8) return;
        const on = uint32At(pay, r), off = uint32At(pay, r + 4);
        if (on <= DELAY_MAX_MS) this.onMs = on;
        if (off <= DELAY_MAX_MS) this.offMs = off;
    }
}

// A scale: its floating input mapped from one range onto another, such as 30 to
// 60 °C onto 0 to 1 for a PWM. Clamped, the output stays within its range.
class uiScale extends uiLogic {
    constructor(x = 0, y = 0) {
        super("scale", MSG_LOGIC_SCALE, "SCL", "Scale", { type: "float", max: 1, out: "float" }, x, y);
        this.range = [0, 1, 0, 1];      // the input's from and to, the output's from and to
        this.clamp = true;
        this.evaluate();
    }

    compute() {
        const [a, b, c, d] = this.range;
        const y = c + (this.inputValue(0) - a) / (b - a) * (d - c);
        return this.clamp ? Math.min(Math.max(y, Math.min(c, d)), Math.max(c, d)) : y;
    }

    // One end of a range, 0 to 3 as in `range`; the input's two must differ.
    setEnd(i, v) {
        v = uiCompare.parse(v);
        if (v === null || (i < 2 && v === this.range[1 - i])) return false;
        this.range[i] = v;
        this.evaluate();
    }

    setClamp(clamp) {
        this.clamp = !!clamp;
        this.evaluate();
    }

    logicSettings(settings) {
        const unit = this.inConns()[0]?.start.node.unit;
        const labels = [`Input from${unit ? ` (${unit})` : ''}`, `Input to${unit ? ` (${unit})` : ''}`, 'Output from', 'Output to'];
        labels.forEach((label, i) => settings.appendChild(editableSetting(label, () => showFloat(this.range[i]),
            (v) => this.setEnd(i, v), {type: 'number', step: 'any'})));
        settings.appendChild(choiceSetting('Clamp', {yes: 'Yes', no: 'No'},
            () => this.clamp ? 'yes' : 'no', (v) => this.setClamp(v === 'yes')));
    }

    marshal() {
        return this.message([...this.sourceRef(), this.clamp ? SCALE_CLAMP : 0, ...this.range.flatMap(float32Bytes)]);
    }

    restore(pay, r, len) {
        if (len !== r + 17) return;
        const range = [0, 1, 2, 3].map(i => float32At(pay, r + 1 + 4 * i));
        if (!range.every(Number.isFinite) || range[0] === range[1]) return;
        this.range = range;
        this.setClamp(pay[r] & SCALE_CLAMP);
    }
}

// A min or a max of up to LOGIC_INPUTS_MAX floating sources, in their unit. Only
// the wired inputs count; with none, it is 0.
class uiExtreme extends uiLogic {
    constructor(max, x = 0, y = 0) {
        super(max ? "max" : "min", max ? MSG_LOGIC_MAX : MSG_LOGIC_MIN, max ? "MAX" : "MIN",
            max ? "Max" : "Min", { type: "float", max: LOGIC_INPUTS_MAX, out: "float" }, x, y);
        this.evaluate();
    }

    // The unit of what it picks from: its first wired input's.
    get unit() {
        return this.inConns()[0]?.start.node.unit ?? '';
    }

    compute() {
        const ins = this.inConns().map((c, k) => this.inputValue(k));
        if (!ins.length) return 0;
        return this.tc === MSG_LOGIC_MAX ? Math.max(...ins) : Math.min(...ins);
    }

    marshal() {
        const refs = this.inConns().flatMap(connRef);
        return this.message(refs.length ? refs : refBytes(null));
    }
}
class uiMin extends uiExtreme { constructor(x = 0, y = 0) { super(false, x, y); } }
class uiMax extends uiExtreme { constructor(x = 0, y = 0) { super(true, x, y); } }

// A pulse: on for its time from each time its boolean source goes on, however
// long the source stays on. Going on again while on starts the time again.
class uiPulse extends uiLogic {
    constructor(x = 0, y = 0) {
        super("pulse", MSG_LOGIC_PULSE, "PLS", "Pulse", { type: "bool", max: 1 }, x, y);
        this.holdsState = true;
        this.onMs = 1000;
        this.inWas = false;
        this.evaluate();
    }

    rewired() { this.inWas = uiLogic.isOn(this.inputValue(0)); }

    compute() {
        const input = uiLogic.isOn(this.inputValue(0)), rose = input && !this.inWas;
        this.inWas = input;
        if (!rose) return this.browserValue === true;
        clearTimeout(this.timer);
        this.timer = setTimeout(() => {
            this.browserValue = false;
            this.show();
        }, this.onMs);
        return true;
    }

    setTime(s) { const ms = parseSeconds(s, 1); if (ms === null) return false; this.onMs = ms; }

    logicSettings(settings) {
        settings.appendChild(editableSetting('Time on (s)', () => String(this.onMs / 1000),
            (v) => this.setTime(v), {type: 'number', min: 0.001, step: 'any'}));
    }

    marshal() {
        return this.message([...this.sourceRef(), ...uint32Bytes(this.onMs)]);
    }

    restore(pay, r, len) {
        if (len !== r + 4) return;
        const ms = uint32At(pay, r);
        if (ms >= 1 && ms <= DELAY_MAX_MS) this.onMs = ms;
    }

    remove() {
        clearTimeout(this.timer);
        super.remove();
    }
}

// A blink: on and off in turn while its boolean source is on, starting on; with
// nothing wired in, always.
class uiBlink extends uiLogic {
    constructor(x = 0, y = 0) {
        super("blink", MSG_LOGIC_BLINK, "BLK", "Blink", { type: "bool", max: 1 }, x, y);
        this.holdsState = true;
        this.onMs = 500;
        this.offMs = 500;
        this.blinking = false;
        this.evaluate();
    }

    compute() {
        const enabled = this.inConns().length === 0 || uiLogic.isOn(this.inputValue(0));
        if (!enabled) {
            clearTimeout(this.timer);
            this.blinking = false;
            return false;
        }
        if (this.blinking) return this.browserValue === true;
        this.blinking = true;
        this.schedule(true);
        return true;
    }

    // Turn over after the time on (from on) or off (from off).
    schedule(on) {
        clearTimeout(this.timer);
        this.timer = setTimeout(() => {
            this.browserValue = !on;
            this.show();
            this.schedule(!on);
        }, on ? this.onMs : this.offMs);
    }

    setOnTime(s) { const ms = parseSeconds(s, 1); if (ms === null) return false; this.onMs = ms; }
    setOffTime(s) { const ms = parseSeconds(s, 1); if (ms === null) return false; this.offMs = ms; }

    logicSettings(settings) {
        settings.appendChild(editableSetting('Time on (s)', () => String(this.onMs / 1000),
            (v) => this.setOnTime(v), {type: 'number', min: 0.001, step: 'any'}));
        settings.appendChild(editableSetting('Time off (s)', () => String(this.offMs / 1000),
            (v) => this.setOffTime(v), {type: 'number', min: 0.001, step: 'any'}));
    }

    marshal() {
        return this.message([...this.sourceRef(), ...uint32Bytes(this.onMs), ...uint32Bytes(this.offMs)]);
    }

    restore(pay, r, len) {
        if (len !== r + 8) return;
        const on = uint32At(pay, r), off = uint32At(pay, r + 4);
        if (on >= 1 && on <= DELAY_MAX_MS) this.onMs = on;
        if (off >= 1 && off <= DELAY_MAX_MS) this.offMs = off;
    }

    remove() {
        clearTimeout(this.timer);
        super.remove();
    }
}

// A toggle: turns over each time its boolean source goes on, so a momentary
// button works as a switch.
class uiToggle extends uiLogic {
    constructor(x = 0, y = 0) {
        super("toggle", MSG_LOGIC_TOGGLE, "TGL", "Toggle", { type: "bool", max: 1 }, x, y);
        this.holdsState = true;
        this.inWas = false;
        this.evaluate();
    }

    rewired() { this.inWas = uiLogic.isOn(this.inputValue(0)); }

    compute() {
        const input = uiLogic.isOn(this.inputValue(0)), rose = input && !this.inWas;
        this.inWas = input;
        const now = this.browserValue === true;
        return rose ? !now : now;
    }
}

// Logic whose wires in each have a role, told apart in its settings as a PWM's
// are: a latch's set and reset, a counter's count and reset. A wire drawn in
// takes the first role with no source yet, else the last. `roles` lists [role,
// setting label] pairs, in the order they are saved.
class uiRoles extends uiLogic {
    constructor(type, tc, text, label, roles, x = 0, y = 0) {
        super(type, tc, text, label, { type: "bool", max: roles.length }, x, y);
        this.roles = roles;
        this.holdsState = true;
        for (const [role] of roles) {
            this[role + 'Source'] = null;      // the source node, and which of its outputs
            this[role + 'Output'] = 0;
        }
    }

    // The wire bringing a role's source; null with none.
    roleConn(role) {
        const node = this[role + 'Source'], output = this[role + 'Output'];
        return node ? this.inConns().find(c => c.start.node === node && c.start.output === output) ?? null : null;
    }

    // The value of a role's source (see connValue); 0 with none.
    roleValue(role) {
        return uiLogic.connValue(this.roleConn(role));
    }

    // A role from an output of a boolean source node, or from none, and redraw
    // the wires. A source of another type, or one that would close a loop through
    // nothing that holds a state, is refused.
    setRole(role, node, output = 0) {
        if (node && (!this.takesType(node, output) || statelessLoop(node, this))) return false;
        this[role + 'Source'] = node;
        this[role + 'Output'] = node ? output : 0;
        this.syncWires();
        this.evaluate();
    }

    drivenBy(role, node, output) {
        return this[role + 'Source'] === node && this[role + 'Output'] === output;
    }

    // Make the wires into this node match its roles' sources.
    syncWires() {
        const refs = this.roles.map(([r]) => r).filter(r => this[r + 'Source'])
            .map(r => ({ node: this[r + 'Source'], output: this[r + 'Output'] }));
        const wired = (c, r) => c.start.node === r.node && c.start.output === r.output;
        this.inConns().filter(c => !refs.some(r => wired(c, r))).forEach(c => c.remove());
        refs.filter((r, i) => !refs.slice(0, i).some(q => wired({ start: q }, r)))
            .filter(r => !this.inConns().some(c => wired(c, r)))
            .forEach(r => new gEdge(r.node, this, r.output));
    }

    addEdge(e) {
        const src = e.start.node, output = e.start.output;
        if (e.end.node === this && !this.roles.some(([r]) => this.drivenBy(r, src, output))) {
            const role = (this.roles.find(([r]) => !this[r + 'Source']) ?? this.roles.at(-1))[0];
            this[role + 'Source'] = src;
            this[role + 'Output'] = output;
            if (selected === this) this.createSettings();
        }
        super.addEdge(e);
    }

    edgeRemoved(e) {
        for (const [role] of this.roles) {
            if (!this.drivenBy(role, e.start.node, e.start.output)) continue;
            this[role + 'Source'] = null;
            this[role + 'Output'] = 0;
        }
        if (selected === this) this.createSettings();
        super.edgeRemoved(e);
    }

    logicSettings(settings) {
        const choices = Object.values(elements).filter(n => n !== this).flatMap(n => n.outputs
            .map((out, i) => [n, out, i]).filter(([, , i]) => this.takesType(n, i) && !statelessLoop(n, this))
            .map(([n, out, i]) => [`${n.name}:${i}`,
                n.outputs.length > 1 ? `${n.label} ${out.name} (${n.name})` : `${n.label} (${n.name})`]));
        for (const [role, label] of this.roles) {
            const source = this[role + 'Source'];
            settings.appendChild(selectSetting(label, [['', 'None'], ...choices],
                () => source ? `${source.name}:${this[role + 'Output']}` : '',
                (pick) => {
                    const [name, output] = pick.split(':');
                    this.setRole(role, elements[name] ?? null, Number(output));
                    this.createSettings();
                }));
        }
    }

    // Its roles' source references, in order; a feedback wire's marked.
    roleRefs() {
        return this.roles.flatMap(([role]) => refBytes(this[role + 'Source'], this[role + 'Output'],
            this.roleConn(role)?.feedback));
    }

    marshal() {
        return this.message(this.roleRefs());
    }
}

// A latch: on once its set source goes on, and off once its reset source does;
// while both are on, off.
class uiLatch extends uiRoles {
    constructor(x = 0, y = 0) {
        super("latch", MSG_LOGIC_LATCH, "LCH", "Latch", [['set', 'Set'], ['reset', 'Reset']], x, y);
        this.evaluate();
    }

    compute() {
        if (uiLogic.isOn(this.roleValue('reset'))) return false;
        return this.browserValue === true || uiLogic.isOn(this.roleValue('set'));
    }
}

// A counter: counts each time its count source goes on, up to its preset, and is
// done (its boolean output 0, on) there; its reset source sets it back to 0, and
// wins. Its count is its floating output 1. A wire leaves from the output picked
// in its settings, as a 2D slider's.
class uiCounter extends uiRoles {
    constructor(x = 0, y = 0) {
        super("counter", MSG_LOGIC_COUNTER, "", "Counter", [['count', 'Count'], ['reset', 'Reset']], x, y);
        this.outputs[0].name = 'Done';
        this.outputs.push({ type: "float", name: "Count", conns: [] });
        this.preset = 3;
        this.inWas = false;
        this.browserCount = 0;      // the count the browser works out
        this.deviceCount = null;    // the count the device last reported
        this.count = null;          // the count shown
        this.evaluate();
    }

    rewired() { this.inWas = uiLogic.isOn(this.roleValue('count')); }

    compute() {
        const input = uiLogic.isOn(this.roleValue('count')), rose = input && !this.inWas;
        this.inWas = input;
        if (uiLogic.isOn(this.roleValue('reset'))) this.browserCount = 0;
        else if (rose && this.browserCount < this.preset) this.browserCount++;
        return this.browserCount >= this.preset;
    }

    deviceSignal(output, value) {
        if (output === 1) this.deviceCount = value;
        else super.deviceSignal(output, value);
    }

    // Its done state, as any logic's; then its count, handed on from output 1.
    show() {
        super.show();
        const count = this.fromDevice() && this.deviceCount != null ? this.deviceCount : this.browserCount;
        const changed = count !== this.count;
        this.count = count;
        this.setText(`${count}/${this.preset}`);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
        if (changed) this.emit(count, 1);
    }

    outputValue(output) {
        return output === 1 ? this.count : super.outputValue(output);
    }

    reading() {
        return this.count == null ? '–' : `${this.count} of ${this.preset}`;
    }

    // A whole number from 1 to COUNTER_MAX. A count past it comes back to it.
    setPreset(v) {
        v = String(v).trim();
        if (!/^\d+$/.test(v) || Number(v) < 1 || Number(v) > COUNTER_MAX) return false;
        this.preset = Number(v);
        this.browserCount = Math.min(this.browserCount, this.preset);
        this.evaluate();
    }

    // The output new wires leave from: 0 (done) or 1 (its count).
    setWireOutput(output) {
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    logicSettings(settings) {
        settings.appendChild(choiceSetting('Wire from', {0: 'Done', 1: 'Count'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(editableSetting('Preset', () => String(this.preset),
            (v) => this.setPreset(v), {type: 'number', min: 1, max: COUNTER_MAX, step: 1}));
        super.logicSettings(settings);
    }

    // Its count and reset sources, then its preset.
    marshal() {
        return this.message([...this.roleRefs(), ...uint32Bytes(this.preset)]);
    }

    // From a saved payload: its preset after its two source references.
    restore(pay, r, len) {
        if (len !== 2 * r + 4) return;
        this.setPreset(uint32At(pay, 2 * r));
    }
}

// How long the device's state of a logic node holds after its last report: the
// device reports each one at least every second (PROTOCOL.md, "Telemetry text frames").
const LOGIC_DEVICE_FRESH_MS = 2500;

// Logic: nodes the device runs, every scan, without the browser (see PROTOCOL.md,
// "Logic"). Each reads one or more sources and is a boolean source itself, so it
// is both: wires come into it, and leave it for outputs or other logic.
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

        // One input, taking `input.max` wires of `input.type`, and one boolean output.
        this.inputs.push({ type: input.type, conns: [], max: input.max });
        this.outputs.push({ type: "bool", conns: [] });
        this.actDir = "both";
        this.actNum = 0;

        this.value = null;          // the state shown: true or false; null before there is one
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

    // Whether the device has reported this node's state lately.
    fromDevice() {
        return this.deviceAt > 0 && performance.now() - this.deviceAt < LOGIC_DEVICE_FRESH_MS;
    }

    // Its signal from the device (PROTOCOL.md, "Telemetry text frames"): 1 on, 0
    // off. It is shown while fresh; once stale, the browser's state is.
    signal(output, value) {
        this.deviceValue = value >= 0.5;
        this.deviceAt = performance.now();
        clearTimeout(this.staleTimer);
        this.staleTimer = setTimeout(() => this.show(), LOGIC_DEVICE_FRESH_MS + 50);
        this.show();
    }

    // The value of the source wired into this node's k-th wire, as a number; a
    // source with no value yet, or no wire, reads 0, as on the device.
    inputValue(k = 0) {
        const conn = this.inConns()[k];
        const v = conn ? conn.start.node.outputValue(conn.start.output) : null;
        return v == null || !Number.isFinite(Number(v)) ? 0 : Number(v);
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
        this.gElmState.setAttribute('fill', on ? '#4CAF50' : 'none');
        this.gElmState.setAttribute('stroke-dasharray', device ? 'none' : '2 2');
        this.gElm.querySelector('title').textContent = device ? 'Run on the device' : 'Run in the browser';
        if (this.tileState?.isConnected) this.tileState.textContent = on == null ? '–' : on ? 'On' : 'Off';
        if (on === this.value) return;
        this.value = on;
        this.emit(this.outputValue(0));
    }

    // A source wired into it has a new value; a wire came in or went.
    receive(value) { this.evaluate(); }
    addEdge(e) {
        super.addEdge(e);
        if (e.end.node === this) this.evaluate();
    }
    edgeRemoved(e) { this.evaluate(); }

    reading() {
        return this.value == null ? '–' : this.value ? 'On' : 'Off';
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
    restore(pay, r) {
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
        const refs = this.inConns().flatMap(c => refBytes(c.start.node, c.start.output));
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

    // Seconds, 0 to a day, to the millisecond; null if not.
    static parse(v) {
        v = String(v).trim();
        if (!/^\d+(\.\d{0,3})?$|^\.\d{1,3}$/.test(v)) return null;
        const ms = Math.round(Number(v) * 1000);
        return ms <= DELAY_MAX_MS ? ms : null;
    }

    setOnDelay(s) { const ms = uiDelay.parse(s); if (ms === null) return false; this.onMs = ms; }
    setOffDelay(s) { const ms = uiDelay.parse(s); if (ms === null) return false; this.offMs = ms; }

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
    restore(pay, r) {
        const on = uint32At(pay, r), off = uint32At(pay, r + 4);
        if (on <= DELAY_MAX_MS) this.onMs = on;
        if (off <= DELAY_MAX_MS) this.offMs = off;
    }
}

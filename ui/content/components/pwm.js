// A PWM output. Its duty cycle and its frequency are each constant, or from an
// input: a source node, picked in the settings, whose value 0 gives the
// parameter's min and 1 its max. The PWM's wires are those sources.
class uiPWM extends uiComponents {
    constructor(x = 0, y = 0) {
        super("pwm",x,y);

        this.tc = MSG_HW_PWM;

        this.inputs.push(
            {
                type:"number",
                conns:[],
                max:2
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = 2;

        this.dutyFromInput = false;
        this.duty = 50;
        this.dutyMin = 0;
        this.dutyMax = 100;
        this.dutySource = null;

        this.freqFromInput = false;
        this.freq = 1000;
        this.freqMin = 100;
        this.freqMax = 10000;
        this.freqSource = null;

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">PWM</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // Each setter takes a whole number in its range, and returns false otherwise.
    setPin(pin) {
        pin = String(pin).trim();
        if (!/^\d+$/.test(pin) || Number(pin) > 28) return false;
        this.pin = Number(pin);
    }

    // A whole number from min to max; null if it isn't one.
    static parse(v, min, max) {
        v = String(v).trim();
        if (!/^\d+$/.test(v) || Number(v) < min || Number(v) > max) return null;
        return Number(v);
    }

    setDuty(d)    { d = uiPWM.parse(d, 0, PWM_DUTY_MAX); if (d === null) return false; this.duty = d; }
    setDutyMin(d) { d = uiPWM.parse(d, 0, PWM_DUTY_MAX); if (d === null) return false; this.dutyMin = d; }
    setDutyMax(d) { d = uiPWM.parse(d, 0, PWM_DUTY_MAX); if (d === null) return false; this.dutyMax = d; }
    setFreq(f)    { f = uiPWM.parse(f, PWM_FREQ_MIN, 0xFFFF); if (f === null) return false; this.freq = f; }
    setFreqMin(f) { f = uiPWM.parse(f, PWM_FREQ_MIN, 0xFFFF); if (f === null) return false; this.freqMin = f; }
    setFreqMax(f) { f = uiPWM.parse(f, PWM_FREQ_MIN, 0xFFFF); if (f === null) return false; this.freqMax = f; }

    // Make a parameter ('duty' or 'freq') constant or from an input. A constant
    // parameter has no source.
    setMode(param, mode) {
        if (mode !== 'constant' && mode !== 'input') return false;
        this[param + 'FromInput'] = mode === 'input';
        if (mode === 'constant') this.setSource(param, null);
    }

    // Drive a parameter from a source node, or from none, and redraw the wires.
    setSource(param, node) {
        this[param + 'Source'] = node;
        this.syncWires();
    }

    // The sources the parameters use.
    sources() {
        return [...new Set([this.dutySource, this.freqSource].filter(Boolean))];
    }

    // Make the wires into this PWM match its sources: one from each.
    syncWires() {
        const sources = this.sources();
        this.inConns().filter(c => !sources.includes(c.start.node)).forEach(c => c.remove());
        sources.filter(n => !this.inConns().some(c => c.start.node === n))
            .forEach(n => new gEdge(n, this));
    }

    // A wire drawn into the PWM drives the duty cycle if that has no source
    // yet, otherwise the frequency; the parameter it drives is from the input.
    addEdge(e) {
        super.addEdge(e);
        const src = e.start.node;
        if (this.sources().includes(src)) return;
        const param = this.dutySource ? 'freq' : 'duty';
        this[param + 'FromInput'] = true;
        this[param + 'Source'] = src;
        if (selected === this) this.createSettings();
    }

    // A wire taken out of the PWM takes its source off the parameters.
    edgeRemoved(e) {
        const src = e.start.node;
        if (this.dutySource === src) this.dutySource = null;
        if (this.freqSource === src) this.freqSource = null;
        if (selected === this) this.createSettings();
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(editableSetting('GPIO pin', () => this.pin, (pin) => this.setPin(pin),
            {type: 'number', min: 0, max: 28, step: 1}));
        this.paramSettings(settings, 'freq', 'Frequency', 'Hz', {type: 'number', min: PWM_FREQ_MIN, max: 0xFFFF, step: 1});
        this.paramSettings(settings, 'duty', 'Duty cycle', '%', {type: 'number', min: 0, max: PWM_DUTY_MAX, step: 1});
    }

    // The settings of one parameter: constant or from an input, then its value,
    // or the input with the values at 0 and at 1. Changing the mode or the
    // input shows the fields for it.
    paramSettings(settings, param, label, unit, attrs) {
        const cap = param[0].toUpperCase() + param.slice(1);
        settings.appendChild(choiceSetting(label, {constant: 'Constant', input: 'From input'},
            () => this[param + 'FromInput'] ? 'input' : 'constant',
            (mode) => { this.setMode(param, mode); this.createSettings(); }));

        if (!this[param + 'FromInput']) {
            settings.appendChild(editableSetting(`${label} (${unit})`, () => this[param],
                (v) => this['set' + cap](v), attrs));
            return;
        }
        // Any input node can be picked; picking one draws its wire.
        const choices = Object.values(elements).filter(n => n.actDir === 'outputs')
            .map(n => [n.name, `${n.label} (${n.name})`]);
        settings.appendChild(selectSetting('Input', [['', 'None'], ...choices],
            () => this[param + 'Source']?.name ?? '',
            (name) => { this.setSource(param, elements[name] ?? null); this.createSettings(); }));
        settings.appendChild(editableSetting(`${label} at 0 (${unit})`, () => this[param + 'Min'],
            (v) => this['set' + cap + 'Min'](v), attrs));
        settings.appendChild(editableSetting(`${label} at 1 (${unit})`, () => this[param + 'Max'],
            (v) => this['set' + cap + 'Max'](v), attrs));
    }

    buildUI(commandInterface) {}

    marshal() {
        const id16 = (v) => [(v >> 8) & 0xFF, v & 0xFF];
        const options = (this.dutyFromInput ? PWM_DUTY_INPUT : 0) | (this.freqFromInput ? PWM_FREQ_INPUT : 0);
        return [
            this.tc, 0x00, ...id16(this.id), 15,
            ...id16(this.dutySource?.id ?? 0),
            this.pin,
            ...id16(this.freq),
            options,
            this.duty, this.dutyMin, this.dutyMax,
            ...id16(this.freqSource?.id ?? 0),
            ...id16(this.freqMin), ...id16(this.freqMax),
        ];
    }
}
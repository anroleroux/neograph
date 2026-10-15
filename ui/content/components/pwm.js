// A PWM output. Its duty cycle and its frequency are each constant, or from an
// input: an output of a floating source node (a slider, a 2D slider's X or Y),
// picked in the settings, whose value 0 gives the parameter's min and 1 its
// max. The PWM's wires are those sources.
class uiPWM extends uiComponents {
    constructor(x = 0, y = 0) {
        super("pwm",x,y);

        this.tc = MSG_HW_PWM;

        this.inputs.push(
            {
                type:"float",
                conns:[],
                max:2
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = freePin(this, 2);     // 2, unless another node uses it

        this.dutyFromInput = false;
        this.duty = 50;
        this.dutyMin = 0;
        this.dutyMax = 100;
        this.dutySource = null;    // the source node, and which of its outputs
        this.dutyOutput = 0;

        this.freqFromInput = false;
        this.freq = 1000;
        this.freqMin = 100;
        this.freqMax = 10000;
        this.freqLog = false;      // from the input: min to max logarithmically, not linearly
        this.freqSource = null;
        this.freqOutput = 0;

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
        pin = parsePin(pin);
        if (pin === null) return false;
        this.pin = pin;
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
    setFreqScale(scale) {
        if (scale !== 'linear' && scale !== 'log') return false;
        this.freqLog = scale === 'log';
    }

    setMode(param, mode) {
        if (mode !== 'constant' && mode !== 'input') return false;
        this[param + 'FromInput'] = mode === 'input';
        if (mode === 'constant') this.setSource(param, null);
    }

    // Drive a parameter from an output of a floating source node (its first by
    // default), or from none, and redraw the wires. A source of another type is refused.
    setSource(param, node, output = 0) {
        if (node && !this.takesType(node, output)) return false;
        this[param + 'Source'] = node;
        this[param + 'Output'] = node ? output : 0;
        this.syncWires();
    }

    // Whether a parameter is driven by output `output` of `node`.
    drivenBy(param, node, output) {
        return this[param + 'Source'] === node && this[param + 'Output'] === output;
    }

    // The source outputs the parameters use, as {node, output}, each once.
    sources() {
        const refs = ['duty', 'freq'].filter(p => this[p + 'Source'])
            .map(p => ({ node: this[p + 'Source'], output: this[p + 'Output'] }));
        return refs.filter((r, i) => !refs.slice(0, i).some(q => q.node === r.node && q.output === r.output));
    }

    // Make the wires into this PWM match its sources: one from each.
    syncWires() {
        const sources = this.sources();
        const wired = (c, r) => c.start.node === r.node && c.start.output === r.output;
        this.inConns().filter(c => !sources.some(r => wired(c, r))).forEach(c => c.remove());
        sources.filter(r => !this.inConns().some(c => wired(c, r)))
            .forEach(r => new gEdge(r.node, this, r.output));
    }

    // A wire drawn into the PWM drives the duty cycle if that has no source
    // yet, otherwise the frequency; the parameter it drives is from the input.
    addEdge(e) {
        super.addEdge(e);
        const src = e.start.node, output = e.start.output;
        if (this.drivenBy('duty', src, output) || this.drivenBy('freq', src, output)) return;
        const param = this.dutySource ? 'freq' : 'duty';
        this[param + 'FromInput'] = true;
        this[param + 'Source'] = src;
        this[param + 'Output'] = output;
        if (selected === this) this.createSettings();
    }

    // A wire taken out of the PWM takes its source off the parameters.
    edgeRemoved(e) {
        for (const param of ['duty', 'freq']) {
            if (!this.drivenBy(param, e.start.node, e.start.output)) continue;
            this[param + 'Source'] = null;
            this[param + 'Output'] = 0;
        }
        if (selected === this) this.createSettings();
    }

    usedPins() {
        return [this.pin];
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(pinSetting(this, () => this.pin, (pin) => this.setPin(pin)));
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
        // Any floating output of a node (a slider, a 2D slider's X or Y) can be
        // picked, as "n5:0"; picking one draws its wire.
        const choices = Object.values(elements).flatMap(n => n.outputs
            .map((out, i) => [n, out, i]).filter(([, , i]) => this.takesType(n, i))
            .map(([n, out, i]) => [`${n.name}:${i}`,
                n.outputs.length > 1 ? `${n.label} ${out.name} (${n.name})` : `${n.label} (${n.name})`]));
        const source = this[param + 'Source'];
        settings.appendChild(selectSetting('Input', [['', 'None'], ...choices],
            () => source ? `${source.name}:${this[param + 'Output']}` : '',
            (pick) => {
                const [name, output] = pick.split(':');
                this.setSource(param, elements[name] ?? null, Number(output));
                this.createSettings();
            }));
        settings.appendChild(editableSetting(`${label} at 0 (${unit})`, () => this[param + 'Min'],
            (v) => this['set' + cap + 'Min'](v), attrs));
        settings.appendChild(editableSetting(`${label} at 1 (${unit})`, () => this[param + 'Max'],
            (v) => this['set' + cap + 'Max'](v), attrs));
        // A logarithmic frequency multiplies by the same factor for each step of
        // the input, so every octave gets the same share of a slider.
        if (param === 'freq') {
            settings.appendChild(choiceSetting('Scale', {linear: 'Linear', log: 'Logarithmic'},
                () => this.freqLog ? 'log' : 'linear', (scale) => this.setFreqScale(scale)));
        }
    }

    buildUI(commandInterface) {}

    marshal() {
        const id16 = (v) => [(v >> 8) & 0xFF, v & 0xFF];
        const options = (this.dutyFromInput ? PWM_DUTY_INPUT : 0) | (this.freqFromInput ? PWM_FREQ_INPUT : 0)
            | (this.freqLog ? PWM_FREQ_LOG : 0);
        return [
            this.tc, MSG_FLAG_OUTPUT, ...id16(this.id), 17,
            ...refBytes(this.dutySource, this.dutyOutput),
            this.pin,
            ...id16(this.freq),
            options,
            this.duty, this.dutyMin, this.dutyMax,
            ...refBytes(this.freqSource, this.freqOutput),
            ...id16(this.freqMin), ...id16(this.freqMax),
        ];
    }
}
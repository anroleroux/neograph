// A stepper motor on four pins, through a ULN2003 driver board (IN1 to IN4), such as
// a 28BYJ-48. Its one floating input either sets where it is, in revolutions per
// unit of the input, or how fast it turns, in RPM per unit. The device moves it
// within its top speed and acceleration; the browser never drives it.
class uiStepper extends uiComponents {
    constructor(x = 0, y = 0) {
        super("stepper", x, y);

        this.tc = MSG_HW_STEPPER;

        this.inputs.push(
            {
                type: "float",
                conns: [],
                max: 1
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pins = uiStepper.freePins(this);
        this.drive = STEPPER_HALF;
        this.stepsRev = 2048;      // full steps a revolution: a 28BYJ-48's, 32 × its 64:1 gearbox
        this.speedMode = false;    // its input sets its speed, not where it is
        this.scale = 1;            // revolutions (or RPM) per unit of its input
        this.rpm = 15;             // its top speed: about the most a 28BYJ-48 does at 5 V
        this.accel = 30;           // RPM a second; 0 = at once
        this.reverse = false;
        this.release = false;      // turn its coils off when it stops

        this.buildGraphElement();
    }

    // Four pins no other node uses: GPIO 10 to 13 if they are free, otherwise the
    // lowest four free ones.
    static freePins(node) {
        const used = pinsInUse(node);
        const preferred = [10, 11, 12, 13];
        if (preferred.every(pin => !used.has(pin))) return preferred;
        const free = USABLE_PINS.filter(pin => !used.has(pin));
        return free.length >= STEPPER_PINS ? free.slice(0, STEPPER_PINS) : preferred;
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">STP</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // Each setter returns false, and keeps what it had, for a value out of its range.

    // The pin of IN(k + 1): a usable one that its other inputs don't use.
    setPin(k, pin) {
        pin = parsePin(pin);
        if (pin === null || !Number.isInteger(k) || k < 0 || k >= STEPPER_PINS
            || this.pins.some((p, j) => j !== k && p === pin)) return false;
        this.pins[k] = pin;
    }

    setDrive(drive) {
        if (![STEPPER_WAVE, STEPPER_FULL, STEPPER_HALF].includes(drive)) return false;
        this.drive = drive;
    }

    setStepsRev(v) {
        v = String(v).trim();
        if (!/^\d+$/.test(v) || Number(v) < STEPPER_STEPS_MIN || Number(v) > 0xFFFF) return false;
        this.stepsRev = Number(v);
    }

    setMode(mode) {
        if (mode !== 'position' && mode !== 'speed') return false;
        this.speedMode = mode === 'speed';
    }

    // Any finite number: below 0 turns it the other way.
    setScale(v) {
        v = uiCompare.parse(v);
        if (v === null) return false;
        this.scale = v;
    }

    setRpm(v) {
        v = uiCompare.parse(v);
        if (v === null || v <= 0 || v > STEPPER_RPM_MAX) return false;
        this.rpm = v;
    }

    setAccel(v) {
        v = uiCompare.parse(v);
        if (v === null || v < 0) return false;
        this.accel = v;
    }

    usedPins() {
        return [...this.pins];
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');

        // Each input is offered the pins no other node, and none of its other inputs, uses.
        const used = pinsInUse(this);
        this.pins.forEach((own, k) => {
            const pins = USABLE_PINS.filter(pin => pin === own
                || (!used.has(pin) && !this.pins.some((p, j) => j !== k && p === pin)));
            settings.appendChild(selectSetting(`IN${k + 1} pin`, pins.map(pin => [pin, `GPIO ${pin}`]),
                () => this.pins[k], (pin) => { this.setPin(k, pin); this.createSettings(); }));
        });

        settings.appendChild(choiceSetting('Drive', {[STEPPER_WAVE]: 'Wave', [STEPPER_FULL]: 'Full', [STEPPER_HALF]: 'Half'},
            () => String(this.drive), (v) => this.setDrive(Number(v))));
        settings.appendChild(editableSetting('Steps per revolution', () => this.stepsRev,
            (v) => this.setStepsRev(v), {type: 'number', min: STEPPER_STEPS_MIN, max: 0xFFFF, step: 1}));

        settings.appendChild(choiceSetting('Input sets', {position: 'Position', speed: 'Speed'},
            () => this.speedMode ? 'speed' : 'position', (v) => { this.setMode(v); this.createSettings(); }));
        // Per unit of what is wired in: per V for an ADC, per °C for the core temperature.
        const per = this.inConns()[0]?.start.node.unit || 'unit';
        settings.appendChild(editableSetting(this.speedMode ? `RPM per ${per}` : `Revolutions per ${per}`,
            () => showFloat(this.scale), (v) => this.setScale(v), {type: 'number', step: 'any'}));

        settings.appendChild(editableSetting('Top speed (RPM)', () => showFloat(this.rpm),
            (v) => this.setRpm(v), {type: 'number', min: 0, max: STEPPER_RPM_MAX, step: 'any'}));
        settings.appendChild(editableSetting('Acceleration (RPM/s)', () => showFloat(this.accel),
            (v) => this.setAccel(v), {type: 'number', min: 0, step: 'any'}));
        settings.appendChild(choiceSetting('Direction', {normal: 'Normal', reverse: 'Reversed'},
            () => this.reverse ? 'reverse' : 'normal', (v) => this.reverse = v === 'reverse'));
        settings.appendChild(choiceSetting('When stopped', {hold: 'Hold', release: 'Release'},
            () => this.release ? 'release' : 'hold', (v) => this.release = v === 'release'));
    }

    buildUI(commandInterface) {}

    options() {
        return this.drive | (this.speedMode ? STEPPER_SPEED : 0) | (this.reverse ? STEPPER_REVERSE : 0)
            | (this.release ? STEPPER_RELEASE : 0);
    }

    marshal() {
        const pay = [...this.sourceRef(), ...this.pins, this.options(), (this.stepsRev >> 8) & 0xFF, this.stepsRev & 0xFF,
            ...float32Bytes(this.scale), ...float32Bytes(this.rpm), ...float32Bytes(this.accel)];
        return [this.tc, MSG_FLAG_OUTPUT, (this.id >> 8) & 0xFF, this.id & 0xFF, pay.length, ...pay];
    }

    // Its settings, after its source reference. A payload the device wouldn't run
    // keeps the defaults; so does a pin another node already has.
    restore(pay, r, len) {
        if (len !== r + 19) return;
        const q = (i) => pay[r + i];
        const [scale, rpm, accel] = [7, 11, 15].map(i => float32At(pay, r + i));
        const options = q(4), steps = q(5) << 8 | q(6);
        if (options & ~(STEPPER_DRIVE_MASK | STEPPER_SPEED | STEPPER_REVERSE | STEPPER_RELEASE)
            || (options & STEPPER_DRIVE_MASK) > STEPPER_HALF || steps < STEPPER_STEPS_MIN
            || ![scale, rpm, accel].every(Number.isFinite) || !(rpm > 0 && rpm <= STEPPER_RPM_MAX) || accel < 0) return;
        const pins = [0, 1, 2, 3].map(q), used = pinsInUse(this);
        if (pins.every((pin, k) => pinUsable(pin) && !used.has(pin) && pins.indexOf(pin) === k)) this.pins = pins;
        this.drive = options & STEPPER_DRIVE_MASK;
        this.speedMode = !!(options & STEPPER_SPEED);
        this.reverse = !!(options & STEPPER_REVERSE);
        this.release = !!(options & STEPPER_RELEASE);
        this.stepsRev = steps;
        this.scale = scale;
        this.rpm = rpm;
        this.accel = accel;
    }
}

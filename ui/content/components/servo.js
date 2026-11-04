// A hobby servo, such as an SG90, on one pin: the device sends it a pulse every
// 20 ms (SERVO_FREQ), whose width sets its angle. Its one floating input is taken
// as a fraction of its source's full scale, as a PWM takes it: 0 gives its angle
// at 0, and 1 its angle at 1. The browser never drives it.
class uiServo extends uiComponents {
    constructor(x = 0, y = 0) {
        super("servo", x, y);

        this.tc = MSG_HW_SERVO;

        this.inputs.push(
            {
                type: "float",
                conns: [],
                max: 1
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = freePin(this, 14);  // 14, unless another node uses it or its slice
        this.angleMin = 0;             // degrees at input 0
        this.angleMax = 180;           // degrees at input 1
        this.pulseMin = 500;           // µs at 0°: an SG90's end, give or take
        this.pulseMax = 2400;          // µs at 180°
        this.speed = 0;                // °/s at most; 0 = as fast as it turns

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">SRV</text>
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

    setAngleMin(v) { v = uiPWM.parse(v, 0, SERVO_ANGLE_MAX); if (v === null) return false; this.angleMin = v; }
    setAngleMax(v) { v = uiPWM.parse(v, 0, SERVO_ANGLE_MAX); if (v === null) return false; this.angleMax = v; }
    setPulseMin(v) { v = uiPWM.parse(v, SERVO_PULSE_MIN, SERVO_PULSE_MAX); if (v === null) return false; this.pulseMin = v; }
    setPulseMax(v) { v = uiPWM.parse(v, SERVO_PULSE_MIN, SERVO_PULSE_MAX); if (v === null) return false; this.pulseMax = v; }
    setSpeed(v)    { v = uiPWM.parse(v, 0, SERVO_SPEED_MAX); if (v === null) return false; this.speed = v; }

    usedPins() {
        return [this.pin];
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(pinSetting(this, () => this.pin, (pin) => this.setPin(pin)));
        const angle = {type: 'number', min: 0, max: SERVO_ANGLE_MAX, step: 1};
        const pulse = {type: 'number', min: SERVO_PULSE_MIN, max: SERVO_PULSE_MAX, step: 1};
        settings.appendChild(editableSetting('Angle at 0 (°)', () => this.angleMin, (v) => this.setAngleMin(v), angle));
        settings.appendChild(editableSetting('Angle at 1 (°)', () => this.angleMax, (v) => this.setAngleMax(v), angle));
        settings.appendChild(editableSetting('Pulse at 0° (µs)', () => this.pulseMin, (v) => this.setPulseMin(v), pulse));
        settings.appendChild(editableSetting(`Pulse at ${SERVO_ANGLE_MAX}° (µs)`, () => this.pulseMax, (v) => this.setPulseMax(v), pulse));
        settings.appendChild(editableSetting('Speed limit (°/s)', () => this.speed, (v) => this.setSpeed(v),
            {type: 'number', min: 0, max: SERVO_SPEED_MAX, step: 1}));
    }

    buildUI(commandInterface) {}

    marshal() {
        const u16 = (v) => [(v >> 8) & 0xFF, v & 0xFF];
        const pay = [...this.sourceRef(), this.pin, this.angleMin, this.angleMax,
            ...u16(this.pulseMin), ...u16(this.pulseMax), ...u16(this.speed)];
        return [this.tc, MSG_FLAG_OUTPUT, ...u16(this.id), pay.length, ...pay];
    }

    // Its settings, after its source reference; a value out of its range keeps the default.
    restore(pay, r, len) {
        if (len !== r + 9) return;
        const q = (i) => pay[r + i], u16 = (i) => q(i) << 8 | q(i + 1);
        this.setPin(q(0));
        this.setAngleMin(q(1));
        this.setAngleMax(q(2));
        this.setPulseMin(u16(3));
        this.setPulseMax(u16(5));
        this.setSpeed(u16(7));
    }
}

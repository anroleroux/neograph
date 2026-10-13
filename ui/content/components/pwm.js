class uiPWM extends uiComponents {
    constructor(x = 0, y = 0) {
        super("pwm",x,y);

        this.tc = MSG_HW_PWM;

        this.inputs.push(
            {
                type:"bool",
                conn:null
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = 2;
        this.freq = 1000;
        this.duty = 50;

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

    setFreq(freq) {
        freq = String(freq).trim();
        if (!/^\d+$/.test(freq) || Number(freq) < PWM_FREQ_MIN || Number(freq) > 0xFFFF) return false;
        this.freq = Number(freq);
    }

    setDuty(duty) {
        duty = String(duty).trim();
        if (!/^\d+$/.test(duty) || Number(duty) > PWM_DUTY_MAX) return false;
        this.duty = Number(duty);
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(editableSetting('GPIO pin', () => this.pin, (pin) => this.setPin(pin),
            {type: 'number', min: 0, max: 28, step: 1}));
        settings.appendChild(editableSetting('Frequency (Hz)', () => this.freq, (freq) => this.setFreq(freq),
            {type: 'number', min: PWM_FREQ_MIN, max: 0xFFFF, step: 1}));
        settings.appendChild(editableSetting('Duty cycle (%)', () => this.duty, (duty) => this.setDuty(duty),
            {type: 'number', min: 0, max: PWM_DUTY_MAX, step: 1}));
    }

    buildUI(commandInterface) {}

    marshal() {
        const buffer = [];
        let sourceID = 0;

        if (this.inputs[0].conn) {
            sourceID = this.inputs[0].conn.start.node.id;
        }

        buffer.push(this.tc);
        buffer.push(0x00);
        buffer.push((this.id >> 8) & 0xFF, this.id & 0xFF);
        buffer.push(0x06);
        buffer.push((sourceID >> 8) & 0xFF, sourceID & 0xFF);
        buffer.push(this.pin);
        buffer.push((this.freq >> 8) & 0xFF, this.freq & 0xFF);
        buffer.push(this.duty);

        return buffer;
    }
}
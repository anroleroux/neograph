class uiPWM extends uiComponents {
    constructor(x = 0, y = 0) {
        super("pwm",x,y);

        this.tc = MSG_HW_PWM;

        this.inputs.push(
            {
                type:"number",
                conn:null
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = 2;
        this.freq = 1000;
        // The duty cycle is constant (switched on and off by the input), or
        // from the input: 0 runs at dutyMin, 1 at dutyMax.
        this.dutyFromInput = false;
        this.duty = 50;
        this.dutyMin = 0;
        this.dutyMax = 100;

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

    // A duty cycle in percent, from 0 to PWM_DUTY_MAX; null if it isn't one.
    static parseDuty(duty) {
        duty = String(duty).trim();
        if (!/^\d+$/.test(duty) || Number(duty) > PWM_DUTY_MAX) return null;
        return Number(duty);
    }

    setDuty(duty) {
        duty = uiPWM.parseDuty(duty);
        if (duty === null) return false;
        this.duty = duty;
    }

    setDutyMin(duty) {
        duty = uiPWM.parseDuty(duty);
        if (duty === null) return false;
        this.dutyMin = duty;
    }

    setDutyMax(duty) {
        duty = uiPWM.parseDuty(duty);
        if (duty === null) return false;
        this.dutyMax = duty;
    }

    setDutyMode(mode) {
        if (mode !== 'constant' && mode !== 'input') return false;
        this.dutyFromInput = mode === 'input';
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(editableSetting('GPIO pin', () => this.pin, (pin) => this.setPin(pin),
            {type: 'number', min: 0, max: 28, step: 1}));
        settings.appendChild(editableSetting('Frequency (Hz)', () => this.freq, (freq) => this.setFreq(freq),
            {type: 'number', min: PWM_FREQ_MIN, max: 0xFFFF, step: 1}));
        // Changing the mode shows the duty fields for that mode.
        settings.appendChild(choiceSetting('Duty cycle', {constant: 'Constant', input: 'From input'},
            () => this.dutyFromInput ? 'input' : 'constant',
            (mode) => { this.setDutyMode(mode); this.createSettings(); }));
        const percent = {type: 'number', min: 0, max: PWM_DUTY_MAX, step: 1};
        if (this.dutyFromInput) {
            settings.appendChild(editableSetting('Duty at 0 (%)', () => this.dutyMin, (d) => this.setDutyMin(d), percent));
            settings.appendChild(editableSetting('Duty at 1 (%)', () => this.dutyMax, (d) => this.setDutyMax(d), percent));
        } else {
            settings.appendChild(editableSetting('Duty cycle (%)', () => this.duty, (d) => this.setDuty(d), percent));
        }
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
        buffer.push(0x09);
        buffer.push((sourceID >> 8) & 0xFF, sourceID & 0xFF);
        buffer.push(this.pin);
        buffer.push((this.freq >> 8) & 0xFF, this.freq & 0xFF);
        buffer.push(this.dutyFromInput ? PWM_DUTY_INPUT : 0x00);
        buffer.push(this.duty, this.dutyMin, this.dutyMax);

        return buffer;
    }
}
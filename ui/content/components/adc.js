// The ADC's reference voltage: a reading of ADC_RAW_FULL is this many volts.
const ADC_VREF = 3.3;

// An analog input: one of the device's ADC channels, ADC0 to ADC2 on GPIO 26 to
// 28. A floating source whose value is the reading in volts, 0 to ADC_VREF. On the
// device it drives the hardware outputs wired to it (a PWM) directly, with the
// reading as a fraction of ADC_VREF; its readings also reach the browser as
// telemetry, for its tile and the plots wired to it.
class uiADC extends uiComponents {
    constructor(x = 0, y = 0) {
        super("adc", x, y);
        this.label = "ADC";

        this.tc = MSG_HW_ADC;

        this.outputs.push(
            {
                type:"float",
                conns:[]
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        this.channel = 0;
        this.value = NaN;       // volts; no reading yet
        this.unit = 'V';

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">ADC</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // A channel from 0 to ADC_CHANNELS - 1, from a number or its text. Another
    // channel's readings belong to other nodes, so the old reading is dropped.
    setChannel(channel) {
        channel = String(channel).trim();
        if (!/^\d+$/.test(channel) || Number(channel) >= ADC_CHANNELS) return false;
        if (Number(channel) === this.channel) return;
        this.channel = Number(channel);
        this.update(NaN);
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        const channels = Array.from({ length: ADC_CHANNELS },
            (_, ch) => [ch, `ADC${ch} · GPIO ${ADC_PIN_FIRST + ch}`]);
        settings.appendChild(selectSetting('Channel', channels, () => this.channel, (ch) => this.setChannel(ch)));
    }

    // A new reading from the device, 0 to ADC_RAW_FULL, or NaN for none: hand
    // it on to the wired nodes in volts.
    update(raw) {
        this.value = raw / ADC_RAW_FULL * ADC_VREF;
        this.emit(this.value);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        return Number.isFinite(this.value) ? `${this.value.toFixed(2)} V` : '–';
    }

    // In the Command view: a tile with the label and the latest reading.
    buildUI(commandInterface) {
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = `${this.label}, ADC${this.channel} on GPIO ${ADC_PIN_FIRST + this.channel}`;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    marshal() {
        return [this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x01, this.channel];
    }
}

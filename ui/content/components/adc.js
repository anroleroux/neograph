// The ADC's reference voltage: a reading of ADC_RAW_FULL is this many volts.
const ADC_VREF = 3.3;

// An analog input: one of the device's ADC channels, ADC0 to ADC2 on GPIO 26 to
// 28. A floating source whose value is the reading in volts, 0 to ADC_VREF. On the
// device it drives the hardware outputs wired to it (a PWM) directly, with the
// reading as a fraction of ADC_VREF; its readings also reach the browser as
// telemetry, in volts, for its tile and the plots wired to it.
class uiADC extends uiComponents {
    // What its box says, and the label it starts with.
    static badge = "ADC";
    static defaultLabel = "ADC";

    constructor(x = 0, y = 0, kind = "adc", tc = MSG_HW_ADC) {
        super(kind, x, y);
        this.label = this.constructor.defaultLabel;

        this.tc = tc;

        this.outputs.push(
            {
                type:"float",
                conns:[]
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        // The first channel whose pin no other node uses.
        this.channel = Math.max(0, [...Array(ADC_CHANNELS).keys()]
            .findIndex(ch => !pinsInUse(this).has(ADC_PIN_FIRST + ch)));
        this.value = NaN;       // volts; no reading yet
        this.unit = 'V';
        this.shown = false;     // a tile in the Command view

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">${this.constructor.badge}</text>
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

    // Its channel's pin.
    usedPins() {
        return [ADC_PIN_FIRST + this.channel];
    }

    // The channels offered are its own and those whose pin no other node uses.
    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        const used = pinsInUse(this);
        const channels = Array.from({ length: ADC_CHANNELS }, (_, ch) => ch)
            .filter(ch => ch === this.channel || !used.has(ADC_PIN_FIRST + ch))
            .map(ch => [ch, `ADC${ch} · GPIO ${ADC_PIN_FIRST + ch}`]);
        settings.appendChild(selectSetting('Channel', channels, () => this.channel, (ch) => this.setChannel(ch)));
        settings.appendChild(shownSetting(this));
    }

    // Its signal from the device (PROTOCOL.md, "Telemetry text frames"), in volts.
    signal(output, value) { this.update(value); }

    // A new reading from the device, in volts, or NaN for none: hand it on to
    // the wired nodes.
    update(volts) {
        this.value = volts;
        this.emit(this.value);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        return Number.isFinite(this.value) ? `${this.value.toFixed(2)} V` : '–';
    }

    // In the Command view, if shown: a tile with the label and the latest reading.
    buildUI(commandInterface) {
        if (!this.shown) return;
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
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x01, this.channel];
    }
}

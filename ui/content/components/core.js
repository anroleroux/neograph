class uiCore extends uiComponents {
    constructor(x = 0, y = 0) {
        super("core", x, y);
        this.label = "Core temperature";

        this.tc = MSG_HW_CORE;

        // The temperature comes from the device's telemetry, which only the
        // browser receives, so it drives only UI outputs (see takesType).
        this.outputs.push(
            {
                type:"float",
                conns:[],
                browserOnly:true
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        this.value = NaN;       // °C; no reading yet
        this.unit = '°C';
        this.shown = false;     // a tile in the Command view

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">TMP</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // A new temperature from the device, in °C: hand it on to the wired nodes.
    update(value) {
        this.value = value;
        this.emit(value);
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        return Number.isFinite(this.value) ? `${this.value.toFixed(1)} °C` : '–';
    }

    createSettings() {
        super.createSettings();
        document.getElementById('settings').appendChild(shownSetting(this));
    }

    // In the Command view, if shown: a tile with the label and the latest temperature.
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

    marshal() {
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x00];
    }
}

// Telemetry from the device, a text frame (see PROTOCOL.md, "Telemetry text
// frames"): "core T" is the chip temperature, which the core nodes pass on, and
// "adc C R" a reading R of ADC channel C, which that channel's ADC nodes pass on,
// and "gpi P S" the state S (1 on, 0 off) of GPIO input pin P, which its node passes on.
function receiveTelemetry(text) {
    const [name, ...values] = text.split(' ');
    if (name === 'core') {
        const temperature = parseFloat(values[0]);
        Object.values(elements).filter(e => e instanceof uiCore).forEach(e => e.update(temperature));
    } else if (name === 'adc') {
        const [channel, raw] = values.map(Number);
        Object.values(elements).filter(e => e instanceof uiADC && e.channel === channel).forEach(e => e.update(raw));
    } else if (name === 'gpi') {
        const [pin, on] = values.map(Number);
        Object.values(elements).filter(e => e instanceof uiGPI && e.pin === pin).forEach(e => e.update(on === 1));
    }
}

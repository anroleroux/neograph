class uiCore extends uiComponents {
    constructor(x = 0, y = 0) {
        super("core", x, y);
        this.label = "Core temperature";

        this.tc = MSG_HW_CORE;

        // In the browser the temperature comes from the device's telemetry, in °C.
        // On the device core 1 reads it and drives the outputs wired to it, from
        // 0 °C (0) to CORE_TEMP_FULL °C (1).
        this.outputs.push(
            {
                type:"float",
                conns:[]
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

    // Its signal from the device (PROTOCOL.md, "Telemetry text frames"), in °C.
    signal(output, value) { this.update(value); }

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
// frames"): "sig I O V" is the value V of output O of element nI, a signal of the
// program core 1 runs. A hardware input (core temperature, ADC, GPIO input, LM35)
// passes its value on to the nodes wired to it; other nodes ignore theirs.
function receiveTelemetry(text) {
    const [name, ...values] = text.split(' ');
    if (name === 'sig') {
        const [id, output, value] = values.map(Number);
        elements['n' + id]?.signal?.(output, value);
    }
}

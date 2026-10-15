// A GPIO input: one of the device's header pins, read as on or off. A boolean
// source. On the device it drives the hardware outputs wired to it (an LED, GPO
// or USB keyboard) directly, as a button would from the browser; its state also
// reaches the browser as telemetry, for its tile and the lights wired to it.
class uiGPI extends uiComponents {
    constructor(x = 0, y = 0) {
        super("gpi", x, y);
        this.label = "GPIO input";

        this.tc = MSG_HW_GPI;

        this.outputs.push(
            {
                type:"bool",
                conns:[]
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        this.pin = 3;
        this.pull = GPI_PULL_DOWN;
        this.invert = false;    // on while the pin is low
        this.value = null;      // true or false; null before the first reading

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">GPI</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // A usable pin, from a number or its text. Another pin's readings belong to
    // other nodes, so the old reading is dropped.
    setPin(pin) {
        pin = parsePin(pin);
        if (pin === null) return false;
        if (pin === this.pin) return;
        this.pin = pin;
        this.update(null);
    }

    // The pin's pull: GPI_PULL_NONE, GPI_PULL_UP or GPI_PULL_DOWN.
    setPull(pull) {
        pull = Number(pull);
        if (![GPI_PULL_NONE, GPI_PULL_UP, GPI_PULL_DOWN].includes(pull)) return false;
        this.pull = pull;
    }

    // When it is on: 'high' or 'low'.
    setActive(level) {
        if (level !== 'high' && level !== 'low') return false;
        this.invert = level === 'low';
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(pinSetting(() => this.pin, (pin) => this.setPin(pin)));
        settings.appendChild(choiceSetting('Pull', {[GPI_PULL_NONE]: 'None', [GPI_PULL_UP]: 'Up', [GPI_PULL_DOWN]: 'Down'},
            () => String(this.pull), (p) => this.setPull(p)));
        settings.appendChild(choiceSetting('On when', {high: 'High', low: 'Low'},
            () => this.invert ? 'low' : 'high', (l) => this.setActive(l)));
    }

    outputValue(output) {
        return this.value == null ? null : Number(this.value);
    }

    // A new state from the device, or null for none: hand it on to the wired nodes.
    update(on) {
        this.value = on;
        this.emit(this.outputValue(0));
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        return this.value == null ? '–' : this.value ? 'On' : 'Off';
    }

    // In the Command view: a tile with the label and the latest state.
    buildUI(commandInterface) {
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = `${this.label}, GPIO ${this.pin}`;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    // The pin, then its pull and whether it is on while low.
    marshal() {
        return [this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x02,
            this.pin, this.pull | (this.invert ? GPI_INVERT : 0)];
    }
}

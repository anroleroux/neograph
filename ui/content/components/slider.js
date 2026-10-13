class uiSlider extends uiComponents {
    constructor(x = 0, y = 0) {
        super("slider", x, y);

        this.tc = MSG_UI_SLIDER;

        this.outputs.push(
            {
                type:"number",
                conns:[]
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        this.value = 0;
        this.vertical = false;

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">SLD</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <path d="" stroke="#BFCDE7" stroke-width="1.5" stroke-linecap="round" opacity="0.6" />
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
        this.gElmTrack = g.children[5];
        this.drawTrack();
    }

    // A short track beside the name, along the slider's orientation.
    drawTrack() {
        this.gElmTrack.setAttribute('d', this.vertical ? 'M 26 -11 V 11' : 'M -26 11 H 26');
    }

    setOrientation(orientation) {
        if (orientation !== 'horizontal' && orientation !== 'vertical') return false;
        this.vertical = orientation === 'vertical';
        this.drawTrack();
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(choiceSetting('Orientation', {horizontal: 'Horizontal', vertical: 'Vertical'},
            () => this.vertical ? 'vertical' : 'horizontal', (o) => this.setOrientation(o)));
    }

    // In the Command view: the label, the value from 0 to 1, and the slider.
    // Only a change of the value's byte is sent, so a drag sends at most one
    // command per step.
    buildUI(commandInterface) {
        const control = document.createElement('div');
        control.className = 'control-slider' + (this.vertical ? ' vertical' : '');
        control.innerHTML = `<span class="name"></span><span class="value"></span>`
            + `<input type="range" min="0" max="${VALUE_FULL}" step="1">`;
        control.querySelector('.name').textContent = this.label;
        const shown = control.querySelector('.value');
        const input = control.querySelector('input');
        input.value = Math.round(this.value * VALUE_FULL);
        input.setAttribute('aria-label', this.label);
        shown.textContent = this.value.toFixed(2);

        input.addEventListener('input', () => {
            const value = Number(input.value) / VALUE_FULL;
            shown.textContent = value.toFixed(2);
            if (value === this.value) return;
            this.value = value;
            this.sendValue(value);
        });
        commandInterface.appendChild(control);
    }

    marshal() {
        return [
            this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x01,
            this.vertical ? SLIDER_VERTICAL : 0x00,
        ];
    }
}
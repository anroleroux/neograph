class uiGPO extends uiComponents {
    constructor(x = 0, y = 0) {
        super("gpo",x,y);

        this.tc = MSG_HW_GPO;

        this.inputs.push(
            {
                type:"bool",
                conn:null
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = 1;

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">GPO</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.insertAdjacentHTML('beforeend', `
            <label>GPIO pin: <input type="number" id="pin-input" min="0" max="28" value="${this.pin}"></label>
        `);
        document.getElementById('pin-input').addEventListener('change', (e) => {
            const pin = parseInt(e.target.value, 10);
            if (pin >= 0 && pin <= 28) {
                this.pin = pin;
            } else {
                e.target.value = this.pin;
            }
        });
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
        buffer.push(0x03);
        buffer.push((sourceID >> 8) & 0xFF, sourceID & 0xFF);
        buffer.push(this.pin);

        return buffer;
    }
}
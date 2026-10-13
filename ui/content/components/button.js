class uiButton extends uiComponents {
    constructor(x = 0, y = 0) {
        super("button", x, y);

        this.tc = MSG_UI_BUTTON;

        this.outputs.push(
            {
                type:"bool",
                conns:[]
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        this.value = false;

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">BTN</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // In the Command view: a tile with the label, the state below it, and a
    // light that is green while the button is on.
    buildUI(commandInterface) {
        const control = document.createElement('button');
        control.type = 'button';
        control.className = 'control-btn';
        control.innerHTML = `<span class="light"></span><span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;

        const show = () => {
            control.setAttribute('aria-pressed', String(this.value));
            control.querySelector('.state').textContent = this.value ? 'On' : 'Off';
        };
        show();
        control.addEventListener('click', () => {
            this.handleClick();
            show();
        });
        commandInterface.appendChild(control);
    }

    marshal() {
        const buffer = [];

        buffer.push(this.tc);
        buffer.push(0x00);
        buffer.push((this.id >> 8) & 0xFF, this.id & 0xFF);
        buffer.push(0x00);

        return buffer;
    }

    // Each click toggles the button: pressed sends 1, released 0.
    handleClick() {
        this.value = !this.value;
        this.sendValue(this.value ? 1 : 0);
    }
}
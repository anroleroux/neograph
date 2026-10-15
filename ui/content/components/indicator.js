// A light: a UI output that shows in the Command view whether the boolean
// signal wired into it (a button) is on or off. As an LED, it is on from
// VALUE_HALF up. Nothing is sent for it: the source hands it each value in the
// browser, so the device stores its message but does nothing with it.
class uiIndicator extends uiComponents {
    constructor(x = 0, y = 0) {
        super("light", x, y);
        this.label = "Light";

        this.tc = MSG_UI_INDICATOR;

        this.inputs.push(
            {
                type:"bool",
                conns:[],
                max:1
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.on = null;     // true or false; null without a source

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="-6" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">LGT</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <circle cx="22" cy="0" r="4" fill="none" stroke="#BFCDE7" stroke-width="1.5" opacity="0.6" />
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // A new value from the source: on from VALUE_HALF up; null when it has none.
    receive(value) {
        this.on = value == null ? null : Number(value) >= VALUE_HALF / VALUE_FULL;
        this.show?.();
    }

    // Wired: the light shows the source's current value.
    addEdge(e) {
        super.addEdge(e);
        if (e.end.node === this) this.receive(e.start.node.outputValue(e.start.output));
    }

    edgeRemoved(e) {
        this.receive(null);
    }

    // In the Command view: a tile with a lamp, lit while the signal is on, the
    // label, and the state below it.
    buildUI(commandInterface) {
        const control = document.createElement('div');
        control.className = 'control-light';
        control.setAttribute('role', 'status');
        control.innerHTML = `<span class="lamp"></span><span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;
        const state = control.querySelector('.state');

        this.show = () => {
            control.dataset.on = String(this.on === true);
            state.textContent = this.on == null ? '–' : this.on ? 'On' : 'Off';
        };
        this.show();
        commandInterface.appendChild(control);
    }

    // The source.
    marshal() {
        const source = this.sourceRef();
        return [this.tc, MSG_FLAG_OUTPUT, (this.id >> 8) & 0xFF, this.id & 0xFF, source.length, ...source];
    }
}

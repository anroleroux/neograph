// A 2D slider: a square pad in the Command view whose puck sets two floating
// outputs, X (0 at the left to 1 at the right) and Y (0 at the bottom to 1 at
// the top). A wire leaves it from the output picked in its settings. Let go,
// it stays where it is, or with "Return to centre" springs back to 0.5, 0.5
// like a joystick.
class uiSlider2D extends uiComponents {
    constructor(x = 0, y = 0) {
        super("slider2d", x, y);
        this.label = "2D slider";

        this.tc = MSG_UI_SLIDER_2D;

        this.outputs.push(
            {
                type:"float",
                name:"X",
                conns:[]
            },
            {
                type:"float",
                name:"Y",
                conns:[]
            }
        );
        this.actDir = "outputs";
        this.actNum = 0;

        this.values = [0, 0];
        this.returns = false;       // to the centre when let go

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="-6" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">XY</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <path d="M 14 -9 V 9 H 30 M 22 -1 m -2 0 a 2 2 0 1 0 4 0 a 2 2 0 1 0 -4 0" fill="none" stroke="#BFCDE7" stroke-width="1.5" stroke-linecap="round" opacity="0.6" />
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    outputValue(output) {
        return this.values[output];
    }

    // What it does when let go: 'stay' or 'centre'.
    setRelease(release) {
        if (release !== 'stay' && release !== 'centre') return false;
        this.returns = release === 'centre';
    }

    // The output new wires leave from: 0 (X) or 1 (Y).
    setWireOutput(output) {
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(choiceSetting('Wire from', {0: 'X', 1: 'Y'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(choiceSetting('When let go', {stay: 'Stay', centre: 'Return to centre'},
            () => this.returns ? 'centre' : 'stay', (r) => this.setRelease(r)));
    }

    // In the Command view: a tile with the label, the values of X and Y from 0
    // to 1, and the pad. Dragging anywhere on the pad moves the puck there; the
    // arrow keys move it a step (Shift: ten). As a slider's, only a change of
    // a value's byte is sent, with both values in one command.
    buildUI(commandInterface) {
        const control = document.createElement('div');
        control.className = 'control-pad';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`
            + `<div class="pad" tabindex="0" role="group"><span class="puck"></span></div>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;
        const state = control.querySelector('.state');
        const pad = control.querySelector('.pad');
        const puck = control.querySelector('.puck');
        pad.setAttribute('aria-label', `${this.label}: X and Y. Arrow keys move it.`);

        const show = () => {
            const [x, y] = this.values;
            state.textContent = `${x.toFixed(2)}, ${y.toFixed(2)}`;
            puck.style.left = `${x * 100}%`;
            puck.style.top = `${(1 - y) * 100}%`;
            pad.setAttribute('aria-description', `X ${x.toFixed(2)}, Y ${y.toFixed(2)}`);
        };
        show();

        // Values as bytes, 0 to VALUE_FULL; sent if either changed.
        const move = (bx, by) => {
            const clamp = (b) => Math.max(0, Math.min(VALUE_FULL, Math.round(b)));
            const values = [clamp(bx) / VALUE_FULL, clamp(by) / VALUE_FULL];
            if (values[0] === this.values[0] && values[1] === this.values[1]) return;
            this.values = values;
            show();
            this.sendValues(values);
        };
        const release = () => { if (this.returns) move(VALUE_HALF, VALUE_HALF); };

        const point = (e) => {
            const box = pad.getBoundingClientRect();
            move((e.clientX - box.left) / box.width * VALUE_FULL, (box.bottom - e.clientY) / box.height * VALUE_FULL);
        };
        pad.addEventListener('pointerdown', (e) => {
            pad.setPointerCapture(e.pointerId);
            point(e);
        });
        pad.addEventListener('pointermove', (e) => { if (pad.hasPointerCapture(e.pointerId)) point(e); });
        pad.addEventListener('pointerup', release);
        pad.addEventListener('pointercancel', release);

        const steps = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowDown: [0, -1], ArrowUp: [0, 1] };
        pad.addEventListener('keydown', (e) => {
            const step = steps[e.key];
            if (!step) return;
            e.preventDefault();
            const n = e.shiftKey ? 10 : 1;
            move(this.values[0] * VALUE_FULL + step[0] * n, this.values[1] * VALUE_FULL + step[1] * n);
        });
        pad.addEventListener('keyup', (e) => { if (steps[e.key]) release(); });

        commandInterface.appendChild(control);
    }

    marshal() {
        return [
            this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x01,
            this.returns ? SLIDER_RETURN : 0x00,
        ];
    }
}

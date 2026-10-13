// A plot's time window, in seconds: how much of the signal it shows.
const PLOT_WINDOW_MIN = 1;
const PLOT_WINDOW_MAX = 255;

class uiPlot extends uiComponents {
    constructor(x = 0, y = 0) {
        super("plot", x, y);
        this.label = "Plot";

        this.tc = MSG_UI_GRAPH;

        this.inputs.push(
            {
                type:"float",
                conns:[],
                max:1
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.window = 10;
        // The signal as [time in ms, value], one sample per change; a null value
        // marks where the plot lost its source.
        this.samples = [];

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">PLT</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <path d="M -26 11 H -14 V 7 H -4 V 13 H 8 V 9 H 26" fill="none" stroke="#BFCDE7" stroke-width="1.5" stroke-linejoin="round" opacity="0.6" />
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    setWindow(seconds) {
        const n = Number(seconds);
        if (!Number.isInteger(n) || n < PLOT_WINDOW_MIN || n > PLOT_WINDOW_MAX) return false;
        this.window = n;
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(editableSetting('Window (s)', () => this.window, (s) => this.setWindow(s),
            {type: 'number', min: PLOT_WINDOW_MIN, max: PLOT_WINDOW_MAX}));
    }

    // A new value from the source. Samples older than the window are dropped,
    // except the last of them, which holds the signal at the plot's left edge.
    receive(value) {
        const now = performance.now();
        this.samples.push([now, value]);
        const start = now - PLOT_WINDOW_MAX * 1000;
        const old = this.samples.findIndex(([t]) => t >= start) - 1;
        if (old > 0) this.samples.splice(0, old);
    }

    // Wired: the plot starts from the source's current value.
    addEdge(e) {
        super.addEdge(e);
        if (e.end.node === this) this.receive(Number(e.start.node.value));
    }

    edgeRemoved(e) {
        this.receive(null);
    }

    // The value at a time: the last sample at or before it, undefined before the first.
    valueAt(t) {
        let value;
        for (const [ts, v] of this.samples) {
            if (ts > t) break;
            value = v;
        }
        return value;
    }

    // In the Command view: a tile with the label, the current value, and the
    // signal over the last `window` seconds, 0 at the bottom to 1 at the top,
    // newest on the right. Pointing at the plot reads the value at that time.
    buildUI(commandInterface) {
        const control = document.createElement('div');
        control.className = 'control-plot';
        control.innerHTML = `<span class="name"></span><span class="state"></span><canvas></canvas>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;
        const state = control.querySelector('.state');
        const canvas = control.querySelector('canvas');
        canvas.setAttribute('role', 'img');
        canvas.setAttribute('aria-label', `${this.label}, last ${this.window} seconds`);

        let pointerX = null;
        canvas.addEventListener('pointermove', (e) => { pointerX = e.offsetX; });
        canvas.addEventListener('pointerleave', () => { pointerX = null; });

        const show = (v) => v == null ? '–' : v.toFixed(2);
        const draw = () => {
            if (!canvas.isConnected) return;    // the Command view was rebuilt
            requestAnimationFrame(draw);
            if (container.className !== 'command') return;

            const dpr = window.devicePixelRatio || 1;
            const w = canvas.clientWidth, h = canvas.clientHeight;
            if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
                canvas.width = Math.round(w * dpr);
                canvas.height = Math.round(h * dpr);
            }
            const ctx = canvas.getContext('2d');
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            ctx.clearRect(0, 0, w, h);

            const now = performance.now();
            const span = this.window * 1000;
            const pad = 2;      // keeps a 2px line at 0 or 1 inside the canvas
            const x = (t) => (t - (now - span)) / span * w;
            const y = (v) => pad + (1 - v) * (h - 2 * pad);

            // Grid at 0, ½ and 1.
            ctx.strokeStyle = 'rgba(32, 32, 43, 0.15)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (const v of [0, 0.5, 1]) {
                ctx.moveTo(0, Math.round(y(v)) + 0.5);
                ctx.lineTo(w, Math.round(y(v)) + 0.5);
            }
            ctx.stroke();

            // The signal, held from each sample to the next.
            ctx.strokeStyle = '#20202b';
            ctx.lineWidth = 2;
            ctx.lineJoin = 'round';
            ctx.beginPath();
            this.samples.forEach(([t, v], i) => {
                if (v == null) return;
                const end = i + 1 < this.samples.length ? this.samples[i + 1][0] : now;
                if (end < now - span) return;
                const prev = this.samples[i - 1];
                const x0 = Math.max(0, x(t));
                if (prev && prev[1] != null && x(t) >= 0) ctx.lineTo(x0, y(v));
                else ctx.moveTo(x0, y(v));
                ctx.lineTo(x(end), y(v));
            });
            ctx.stroke();

            // The value now, or where the pointer is.
            if (pointerX == null) {
                state.textContent = show(this.valueAt(now));
            } else {
                const t = now - span + pointerX / w * span;
                ctx.strokeStyle = 'rgba(32, 32, 43, 0.4)';
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(Math.round(pointerX) + 0.5, 0);
                ctx.lineTo(Math.round(pointerX) + 0.5, h);
                ctx.stroke();
                state.textContent = `${show(this.valueAt(t))} · ${((now - t) / 1000).toFixed(1)} s ago`;
            }
        };
        commandInterface.appendChild(control);
        requestAnimationFrame(draw);
    }

    marshal() {
        const sourceID = this.sourceID();
        return [
            this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x03,
            (sourceID >> 8) & 0xFF, sourceID & 0xFF, this.window,
        ];
    }
}

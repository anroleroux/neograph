// A plot's time window, in seconds: how much of the signal it shows.
const PLOT_WINDOW_MIN = 1;
const PLOT_WINDOW_MAX = 255;
// The longest unit a plot saves, in UTF-8 bytes.
const PLOT_UNIT_MAX = 16;

// A value axis that fits `values`: round-numbered ticks, the lowest at or below
// the smallest value and the highest at or above the largest, about four steps
// apart. A flat signal gets a range around its value; no values, 0 to 1.
// `format` writes a tick with as many decimals as the step needs, or with
// `precise`, a value to 4 significant digits.
function plotAxis(values) {
    let lo = values.length ? Math.min(...values) : 0;
    let hi = values.length ? Math.max(...values) : 1;
    if (lo === hi) {
        const d = lo === 0 ? 1 : Math.abs(lo) * 0.1;
        lo -= d;
        hi += d;
    }
    const rough = (hi - lo) / 4;
    const p = 10 ** Math.floor(Math.log10(rough));
    const f = rough / p;
    const step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * p;
    const first = Math.floor(lo / step), last = Math.ceil(hi / step);
    const ticks = [];
    for (let i = first; i <= last; i++) ticks.push(i * step);

    const exp = step >= 1e5 || step < 1e-4;
    const decimals = Math.max(0, -Math.floor(Math.log10(step)));
    const format = (v, precise = false) => {
        if (precise) return String(Number(v.toPrecision(4)));
        return exp ? v.toExponential(1) : v.toFixed(decimals);
    };
    return { min: ticks[0], max: ticks[ticks.length - 1], ticks, format };
}

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
        // The unit shown with the values: null for the source's own (see
        // sourceUnit), or text that overrides it.
        this.unitOverride = null;
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

    // The unit of the source wired in, '' if it has none (a slider) or there is no source.
    sourceUnit() {
        return this.inConns()[0]?.start.node.unit ?? '';
    }

    // The unit the plot shows: its own if set, otherwise its source's.
    unit() {
        return this.unitOverride ?? this.sourceUnit();
    }

    // Set the unit: text overrides the source's unit; nothing, or text starting
    // with "auto", goes back to it. Returns false if the text is too long to save.
    setUnit(unit) {
        unit = String(unit).trim();
        if (unit === '' || /^auto\b/i.test(unit)) {
            this.unitOverride = null;
            return;
        }
        if (new TextEncoder().encode(unit).length > PLOT_UNIT_MAX) return false;
        this.unitOverride = unit;
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(editableSetting('Window (s)', () => this.window, (s) => this.setWindow(s),
            {type: 'number', min: PLOT_WINDOW_MIN, max: PLOT_WINDOW_MAX}));
        settings.appendChild(editableSetting('Unit',
            () => this.unitOverride ?? `Auto (${this.sourceUnit() || 'none'})`, (u) => this.setUnit(u)));
    }

    // A new value from the source, any number. One that is not finite leaves a
    // gap in the trace. Samples older than the widest window are dropped, except
    // the last of them, which holds the signal at the plot's left edge.
    receive(value) {
        const now = performance.now();
        this.samples.push([now, Number.isFinite(value) ? value : null]);
        const start = now - PLOT_WINDOW_MAX * 1000;
        const old = this.samples.findIndex(([t]) => t >= start) - 1;
        if (old > 0) this.samples.splice(0, old);
    }

    // Wired: the plot starts from the source's current value.
    addEdge(e) {
        super.addEdge(e);
        if (e.end.node === this) this.receive(Number(e.start.node.outputValue(e.start.output)));
    }

    edgeRemoved(e) {
        this.receive(null);
    }

    // The samples from a time on, starting with the value held at that time.
    samplesSince(t) {
        const i = this.samples.findIndex(([ts]) => ts > t);
        const from = i === -1 ? this.samples.length : i;
        const shown = this.samples.slice(from);
        if (from > 0) shown.unshift([t, this.samples[from - 1][1]]);
        return shown;
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
    // signal over the last `window` seconds, newest on the right. The value axis
    // fits what the window shows, whatever its range, with gridlines at round
    // numbers. Pointing at the plot reads the value at that time.
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
            ctx.font = '10px sans-serif';

            const now = performance.now();
            const span = this.window * 1000;
            const shown = this.samplesSince(now - span);
            const axis = plotAxis(shown.map(([, v]) => v).filter(v => v != null));

            // The tick labels sit left of the plot, which takes the rest.
            const pad = 6;      // keeps a 2px line and the labels inside the canvas
            const left = Math.ceil(Math.max(...axis.ticks.map(v => ctx.measureText(axis.format(v)).width))) + 6;
            const x = (t) => left + (t - (now - span)) / span * (w - left);
            const y = (v) => pad + (axis.max - v) / (axis.max - axis.min) * (h - 2 * pad);

            ctx.strokeStyle = 'rgba(32, 32, 43, 0.15)';
            ctx.fillStyle = 'rgba(32, 32, 43, 0.6)';
            ctx.lineWidth = 1;
            ctx.textAlign = 'right';
            ctx.textBaseline = 'middle';
            ctx.beginPath();
            for (const v of axis.ticks) {
                const ty = Math.round(y(v)) + 0.5;
                ctx.moveTo(left, ty);
                ctx.lineTo(w, ty);
                ctx.fillText(axis.format(v), left - 4, ty);
            }
            ctx.stroke();

            // The signal, held from each sample to the next.
            ctx.strokeStyle = '#20202b';
            ctx.lineWidth = 2;
            ctx.lineJoin = 'round';
            ctx.beginPath();
            shown.forEach(([t, v], i) => {
                if (v == null) return;
                const end = i + 1 < shown.length ? shown[i + 1][0] : now;
                const x0 = Math.max(left, x(t));
                if (i > 0 && shown[i - 1][1] != null) ctx.lineTo(x0, y(v));
                else ctx.moveTo(x0, y(v));
                ctx.lineTo(x(end), y(v));
            });
            ctx.stroke();

            // The value now, or where the pointer is.
            const unit = this.unit();
            const show = (v) => v == null ? '–' : axis.format(v, true) + (unit ? ' ' + unit : '');
            if (pointerX == null || pointerX < left) {
                state.textContent = show(this.valueAt(now));
            } else {
                const t = now - span + (pointerX - left) / (w - left) * span;
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

    // The source, the window, then the unit if one is set: none means the source's.
    marshal() {
        const source = this.sourceRef();
        const unit = new TextEncoder().encode(this.unitOverride ?? '');
        return [
            this.tc, MSG_FLAG_OUTPUT, (this.id >> 8) & 0xFF, this.id & 0xFF, source.length + 1 + unit.length,
            ...source, this.window, ...unit,
        ];
    }
}

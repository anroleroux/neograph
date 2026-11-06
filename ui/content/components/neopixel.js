// NeoPixels: a chain of WS2812 LEDs on one pin, laid out as an array (rows of
// columns), a circle (rings, one inside the other) or a doughnut (one ring). It takes
// two wires: a number, taken as a fraction of its source's full scale as a servo takes
// it, which its graphic draws on the LEDs (a bar level indicator, or a point moving
// from a start to an end); and an on/off, which turns every LED off while it is off
// (not wired, it is always on). The device drives them; the browser only shows a
// preview in the settings.
class uiNeoPixel extends uiComponents {
    constructor(x = 0, y = 0) {
        super("neopixel", x, y);

        this.tc = MSG_HW_NEOPIXEL;

        // One wire of each type: a number, and an on/off. A new one replaces the wire
        // of its type (see addEdge), so the input never drops the other's.
        this.inputs.push(
            {
                type: "float",
                conns: [],
                max: 3
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;
        this.wireRefs = 2;                 // restoreGraph: its value's source, then its on/off's

        this.pin = freePin(this, 16);      // 16, unless another node uses it
        this.layout = NEOPIXEL_ARRAY;
        this.rows = 4;                     // an array
        this.cols = 4;
        this.zigzag = false;               // an array: every other row runs back
        this.rings = [24, 16, 12, 8, 1];   // a circle: each ring's LEDs, in wiring order
        this.ringLeds = 24;                // a doughnut
        this.brightness = 40;              // 0-255: NeoPixels are bright, and draw up to 60 mA each
        this.graphic = NEOPIXEL_GFX_BAR;
        this.along = NEOPIXEL_BAR_ROW;     // a bar: along a row or column; around a ring or across the rings
        this.position = 0;                 // a bar: which row, column or ring; a doughnut's first LED. From 0
        this.from = [0, 0];                // a point: its start and end: an array's row and column; a
        this.to = [3, 3];                  //   circle's ring and LED; a doughnut's 0 and LED. From 0
        this.reverse = false;
        this.colourInput = false;          // its colour goes from `colour` at 0 to `colourAt1` at 1
        this.colour = [0, 255, 0];
        this.colourAt1 = [255, 0, 0];
        this.preview = 0.6;                // the level the settings preview shows; not saved

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">NPX</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // ── Its two wires ──

    // The type of what a wire brings: "float" for its value, "bool" for on/off.
    static wireType(conn) {
        return conn.start.node.outputs[conn.start.output].type;
    }

    // A number or an on/off, from a source whose values reach the device. Not a
    // block's port that takes its type from its first wire: which input it would be
    // is not known.
    takesType(source, output = source.actNum) {
        const out = source.outputs[output];
        return !!out && !out.browserOnly && !source.typeFromWire?.() && (out.type === 'float' || out.type === 'bool');
    }

    // A wire in replaces the one of its type.
    addEdge(e) {
        if (e.end.node === this) {
            const type = uiNeoPixel.wireType(e);
            this.inConns().filter(c => uiNeoPixel.wireType(c) === type).forEach(c => c.remove());
        }
        super.addEdge(e);
    }

    valueConn() { return this.inConns().find(c => uiNeoPixel.wireType(c) === 'float'); }
    enableConn() { return this.inConns().find(c => uiNeoPixel.wireType(c) === 'bool'); }

    // ── Its layout ──

    // Its size as saved (see NEOPIXEL_ layouts): an array's rows and columns, a
    // circle's rings, a doughnut's LEDs.
    size() {
        return this.layout === NEOPIXEL_ARRAY ? [this.rows, this.cols]
            : this.layout === NEOPIXEL_CIRCLE ? [...this.rings] : [this.ringLeds];
    }

    leds() {
        return this.layout === NEOPIXEL_ARRAY ? this.rows * this.cols : this.size().reduce((a, b) => a + b, 0);
    }

    // The positions its bar may start from or run along: an array's rows or columns,
    // a circle's rings (one, across them), a doughnut's LEDs.
    positions() {
        if (this.layout === NEOPIXEL_ARRAY) return this.along === NEOPIXEL_BAR_ROW ? this.rows : this.cols;
        if (this.layout === NEOPIXEL_CIRCLE) return this.along === NEOPIXEL_BAR_ROW ? this.rings.length : 1;
        return this.ringLeds;
    }

    // Whether a point's place [a, b] is on its LEDs: an array's row and column, a
    // circle's ring and an LED of it, a doughnut's 0 and an LED.
    placeValid([a, b]) {
        if (this.layout === NEOPIXEL_ARRAY) return a < this.rows && b < this.cols;
        if (this.layout === NEOPIXEL_CIRCLE) return a < this.rings.length && b < this.rings[a];
        return a === 0 && b < this.ringLeds;
    }

    // After a change of layout, size or direction: a bar position or point place it
    // doesn't have goes back to its default.
    keepPosition() {
        if (this.layout === NEOPIXEL_DOUGHNUT) this.along = NEOPIXEL_BAR_ROW;
        if (this.position >= this.positions()) this.position = 0;
        if (!this.placeValid(this.from)) this.from = [0, 0];
        if (!this.placeValid(this.to)) this.to = this.layout === NEOPIXEL_ARRAY ? [this.rows - 1, this.cols - 1] : [0, 0];
    }

    // Each setter returns false, and keeps what it had, for a value out of its range.

    setPin(pin) {
        pin = parsePin(pin);
        if (pin === null) return false;
        this.pin = pin;
    }

    setLayout(layout) {
        if (![NEOPIXEL_ARRAY, NEOPIXEL_CIRCLE, NEOPIXEL_DOUGHNUT].includes(layout)) return false;
        this.layout = layout;
        this.keepPosition();
    }

    setRows(v) {
        v = uiPWM.parse(v, 1, 255);
        if (v === null || v * this.cols > NEOPIXEL_LEDS_MAX) return false;
        this.rows = v;
        this.keepPosition();
    }

    setCols(v) {
        v = uiPWM.parse(v, 1, 255);
        if (v === null || v * this.rows > NEOPIXEL_LEDS_MAX) return false;
        this.cols = v;
        this.keepPosition();
    }

    // A circle's rings, as a list of LED counts in wiring order: "24, 16, 12, 8, 1".
    setRings(text) {
        const parts = String(text).split(/[\s,+]+/).filter(Boolean);
        const rings = parts.map(p => uiPWM.parse(p, 1, 255));
        if (!rings.length || rings.length > NEOPIXEL_RINGS_MAX || rings.includes(null)
            || rings.reduce((a, b) => a + b, 0) > NEOPIXEL_LEDS_MAX) return false;
        this.rings = rings;
        this.keepPosition();
    }

    setRingLeds(v) {
        v = uiPWM.parse(v, 1, 255);
        if (v === null) return false;
        this.ringLeds = v;
        this.keepPosition();
    }

    setBrightness(v) {
        v = uiPWM.parse(v, 0, 255);
        if (v === null) return false;
        this.brightness = v;
    }

    setGraphic(g) {
        if (![NEOPIXEL_GFX_BAR, NEOPIXEL_GFX_POINT, NEOPIXEL_GFX_PATCH].includes(g)) return false;
        this.graphic = g;
    }

    setAlong(along) {
        if (along !== NEOPIXEL_BAR_ROW && along !== NEOPIXEL_BAR_COLUMN) return false;
        if (this.layout === NEOPIXEL_DOUGHNUT && along !== NEOPIXEL_BAR_ROW) return false;
        this.along = along;
        this.keepPosition();
    }

    // A bar's position as shown, from 1.
    setPosition(v) {
        v = uiPWM.parse(v, 1, this.positions());
        if (v === null) return false;
        this.position = v - 1;
    }

    // One part of a point's start or end (`end` 'from' or 'to'), as shown, from 1: k 0
    // its row or ring, k 1 its column or LED. A ring keeps the LED if the ring has it,
    // otherwise goes to its LED 1.
    setPlace(end, k, v) {
        const place = [...this[end]];
        const max = this.layout === NEOPIXEL_ARRAY ? (k === 0 ? this.rows : this.cols)
            : this.layout === NEOPIXEL_CIRCLE ? (k === 0 ? this.rings.length : this.rings[place[0]]) : this.ringLeds;
        v = uiPWM.parse(v, 1, max);
        if (v === null || (this.layout === NEOPIXEL_DOUGHNUT && k === 0)) return false;
        place[k] = v - 1;
        if (!this.placeValid(place)) place[1] = 0;
        this[end] = place;
    }

    // A colour as #rrggbb: its colour (at 0), or with `at1` its colour at 1.
    setColour(hex, at1 = false) {
        const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(String(hex).trim());
        if (!m) return false;
        this[at1 ? 'colourAt1' : 'colour'] = m.slice(1).map(h => parseInt(h, 16));
    }

    colourHex(at1 = false) {
        return '#' + this[at1 ? 'colourAt1' : 'colour'].map(c => c.toString(16).padStart(2, '0')).join('');
    }

    usedPins() {
        return [this.pin];
    }

    // ── What it shows: as src/neopixel.c works it out ──

    // The LED an array's row and column (from 0, at the top left) is wired to.
    arrayLed(row, col) {
        if (this.zigzag && row % 2 === 1) col = this.cols - 1 - col;
        return row * this.cols + col;
    }

    ringFirst(ring) {
        return this.rings.slice(0, ring).reduce((a, b) => a + b, 0);
    }

    // A circle's rings from the centre out: smallest first, in wiring order among equals.
    ringsOutwards() {
        return this.rings.map((n, i) => i).sort((a, b) => this.rings[a] - this.rings[b] || a - b);
    }

    barSteps() {
        if (this.layout === NEOPIXEL_ARRAY) return this.along === NEOPIXEL_BAR_ROW ? this.cols : this.rows;
        if (this.layout === NEOPIXEL_CIRCLE) return this.along === NEOPIXEL_BAR_ROW ? this.rings[this.position] : this.rings.length;
        return this.ringLeds;
    }

    // The LEDs of step e of its bar, from the start of the bar.
    barLeds(e) {
        const steps = this.barSteps(), s = this.reverse ? steps - 1 - e : e;
        const around = (first, n, from) => [first + (from + (this.reverse ? n - e : e)) % n];
        if (this.layout === NEOPIXEL_ARRAY) {
            return this.along === NEOPIXEL_BAR_ROW ? [this.arrayLed(this.position, s)]
                : [this.arrayLed(steps - 1 - s, this.position)];
        }
        if (this.layout === NEOPIXEL_CIRCLE) {
            if (this.along === NEOPIXEL_BAR_ROW) return around(this.ringFirst(this.position), this.rings[this.position], 0);
            const ring = this.ringsOutwards()[s], first = this.ringFirst(ring);
            return [...Array(this.rings[ring]).keys()].map(i => first + i);
        }
        return around(0, this.ringLeds, this.position);
    }

    // The LED its point is on at `fraction`: on an array straight from its start to its
    // end; on a circle or doughnut from its start ring to its end ring, and around from
    // its start angle to its end angle (once round from an LED to itself).
    pointLed(fraction) {
        if (this.layout === NEOPIXEL_ARRAY) {
            const at = (k) => Math.floor(this.from[k] + (this.to[k] - this.from[k]) * fraction + 0.5);
            return this.arrayLed(at(0), at(1));
        }
        const rings = this.layout === NEOPIXEL_CIRCLE ? this.rings : [this.ringLeds];
        const outwards = this.layout === NEOPIXEL_CIRCLE ? this.ringsOutwards() : [0];
        const rank = (p) => outwards.indexOf(p[0]), turn = (p) => p[1] / rings[p[0]];
        const [rank0, rank1, turn0, turn1] = [rank(this.from), rank(this.to), turn(this.from), turn(this.to)];
        const sweep = this.pointSweep();
        let t = turn0 + (this.reverse ? -sweep : sweep) * fraction;
        t -= Math.trunc(t);
        if (t < 0) t += 1;
        const ring = outwards[Math.floor(rank0 + (rank1 - rank0) * fraction + 0.5)], n = rings[ring];
        const first = this.layout === NEOPIXEL_CIRCLE ? this.ringFirst(ring) : 0;
        return first + Math.floor(t * n + 0.5) % n;
    }

    // The rings a point's place is on from the centre out, and its angle, in turns.
    ringPlace(p) {
        const rings = this.layout === NEOPIXEL_CIRCLE ? this.rings : [this.ringLeds];
        const outwards = this.layout === NEOPIXEL_CIRCLE ? this.ringsOutwards() : [0];
        return [outwards.indexOf(p[0]), p[1] / rings[p[0]]];
    }

    // How far round a point on a circle or doughnut goes, in turns.
    pointSweep() {
        const turn0 = this.ringPlace(this.from)[1], turn1 = this.ringPlace(this.to)[1];
        let sweep = this.reverse ? turn0 - turn1 : turn1 - turn0;
        if (sweep < 0) sweep += 1;
        if (sweep === 0 && this.from[0] === this.to[0] && this.from[1] === this.to[1]) sweep = 1;
        return sweep;
    }

    // The steps of a point's path, each from one LED to the next (see neopixel_point_steps).
    pointSteps() {
        if (this.layout === NEOPIXEL_ARRAY) {
            return Math.max(Math.abs(this.to[0] - this.from[0]), Math.abs(this.to[1] - this.from[1]));
        }
        const rings = this.layout === NEOPIXEL_CIRCLE ? this.rings : [this.ringLeds];
        const n = Math.max(rings[this.from[0]], rings[this.to[0]]);
        const around = Math.floor(this.pointSweep() * n + 0.5);
        return Math.max(around, Math.abs(this.ringPlace(this.to)[0] - this.ringPlace(this.from)[0]));
    }

    // A point's LEDs at `fraction`, as [led, amount]: the two either side of it on its
    // path, each as much as it is near it; or the one it is on.
    pointLeds(fraction) {
        const steps = this.pointSteps();
        if (steps === 0) return [[this.pointLed(fraction), 1]];
        const at = fraction * steps, i = Math.min(Math.floor(at), steps - 1), near = at - i;
        const a = this.pointLed(i / steps), b = this.pointLed((i + 1) / steps);
        return a === b ? [[a, 1]] : [[a, 1 - near], [b, near]];
    }

    // The LEDs of a patch (see neopixel_render's patch_light): on an array the rectangle
    // with its start and end at its corners; on a circle or doughnut, on each ring from
    // its start ring to its end ring, from the LED nearest its start angle round to the
    // one nearest its end angle.
    patchLeds() {
        const leds = [];
        if (this.layout === NEOPIXEL_ARRAY) {
            const [r0, r1] = [this.from[0], this.to[0]].sort((a, b) => a - b);
            const [c0, c1] = [this.from[1], this.to[1]].sort((a, b) => a - b);
            for (let r = r0; r <= r1; r++) for (let c = c0; c <= c1; c++) leds.push(this.arrayLed(r, c));
            return leds;
        }
        const rings = this.layout === NEOPIXEL_CIRCLE ? this.rings : [this.ringLeds];
        const outwards = this.layout === NEOPIXEL_CIRCLE ? this.ringsOutwards() : [0];
        const [rank0, turn0] = this.ringPlace(this.from), [rank1, turn1] = this.ringPlace(this.to);
        let sweep = this.reverse ? turn0 - turn1 : turn1 - turn0;
        if (sweep < 0) sweep += 1;
        for (let j = Math.min(rank0, rank1); j <= Math.max(rank0, rank1); j++) {
            const ring = outwards[j], n = rings[ring], first = this.layout === NEOPIXEL_CIRCLE ? this.ringFirst(ring) : 0;
            const start = Math.floor(turn0 * n + 0.5) % n, more = Math.min(Math.floor(sweep * n + 0.5), n - 1);
            for (let e = 0; e <= more; e++) leds.push(first + (this.reverse ? start + n - e : start + e) % n);
        }
        return leds;
    }

    // Its colour at `fraction`, [r, g, b], scaled by an amount and its brightness.
    colourAt(fraction, amount) {
        const t = this.colourInput ? fraction : 0, k = amount * this.brightness / 255;
        return this.colour.map((c, i) => Math.floor((c + (this.colourAt1[i] - c) * t) * k + 0.5));
    }

    // Each LED's colour, [r, g, b], for its input at `fraction` of full scale; all off
    // when not `on`.
    render(fraction, on = true) {
        const out = Array.from({length: this.leds()}, () => [0, 0, 0]);
        if (!on) return out;
        fraction = Math.max(0, Math.min(1, fraction));
        if (this.graphic === NEOPIXEL_GFX_PATCH) {
            const c = this.colourAt(fraction, 1);
            this.patchLeds().forEach(led => out[led] = c);
            return out;
        }
        if (this.graphic === NEOPIXEL_GFX_POINT) {
            this.pointLeds(fraction).forEach(([led, amount]) => out[led] = this.colourAt(fraction, amount));
            return out;
        }
        const steps = this.barSteps(), lit = fraction * steps;
        for (let e = 0; e < steps && lit > e; e++) {
            const c = this.colourAt(fraction, Math.min(1, lit - e));
            this.barLeds(e).forEach(i => out[i] = c);
        }
        return out;
    }

    // Where each LED is drawn in the preview, [x, y], in LED spacings.
    ledPlaces() {
        const onRing = (n, radius) => [...Array(n).keys()].map(i => {
            const a = 2 * Math.PI * i / n - Math.PI / 2;      // from the top, clockwise
            return [radius * Math.cos(a), radius * Math.sin(a)];
        });
        if (this.layout === NEOPIXEL_ARRAY) {
            const places = [];
            for (let r = 0; r < this.rows; r++) for (let c = 0; c < this.cols; c++) places[this.arrayLed(r, c)] = [c, r];
            return places;
        }
        if (this.layout === NEOPIXEL_DOUGHNUT) return onRing(this.ringLeds, Math.max(1, this.ringLeds / (2 * Math.PI)));
        // A circle: each ring at a radius by its place from the centre out; one LED alone in the middle.
        const places = [], outwards = this.ringsOutwards();
        let radius = 0;
        outwards.forEach((ring, j) => {
            const n = this.rings[ring];
            radius = n === 1 && j === 0 ? 0 : Math.max(radius + 1, n / (2 * Math.PI));
            onRing(n, radius).forEach((p, i) => places[this.ringFirst(ring) + i] = p);
        });
        return places;
    }

    // An SVG of the LEDs at the preview level: off ones dim, lit ones in their colour.
    // Brightness is left out, so a dim setting still shows.
    previewSvg() {
        const places = this.ledPlaces(), xs = places.map(p => p[0]), ys = places.map(p => p[1]);
        const [x0, y0] = [Math.min(...xs) - 0.6, Math.min(...ys) - 0.6];
        const [w, h] = [Math.max(...xs) - x0 + 0.6, Math.max(...ys) - y0 + 0.6];
        const brightness = this.brightness;
        this.brightness = 255;
        const colours = this.render(this.preview);
        this.brightness = brightness;
        const dots = places.map(([x, y], i) => {
            const [r, g, b] = colours[i], on = r || g || b;
            return `<circle cx="${x.toFixed(2)}" cy="${y.toFixed(2)}" r="0.4" fill="${on ? `rgb(${r},${g},${b})` : '#BFCDE726'}"><title>LED ${i + 1}</title></circle>`;
        }).join('');
        return `<svg class="neopixel-preview" viewBox="${x0.toFixed(2)} ${y0.toFixed(2)} ${w.toFixed(2)} ${h.toFixed(2)}" role="img" aria-label="Preview of the LEDs">${dots}</svg>`;
    }

    // ── Settings ──

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        const again = () => this.createSettings();
        settings.appendChild(pinSetting(this, () => this.pin, (pin) => this.setPin(pin)));

        settings.appendChild(choiceSetting('Layout',
            {[NEOPIXEL_ARRAY]: 'Array', [NEOPIXEL_CIRCLE]: 'Circle', [NEOPIXEL_DOUGHNUT]: 'Doughnut'},
            () => String(this.layout), (v) => { this.setLayout(Number(v)); again(); }));
        const count = (max) => ({type: 'number', min: 1, max, step: 1});
        if (this.layout === NEOPIXEL_ARRAY) {
            settings.appendChild(editableSetting('Rows', () => this.rows, (v) => this.setRows(v) ?? again(), count(255)));
            settings.appendChild(editableSetting('Columns', () => this.cols, (v) => this.setCols(v) ?? again(), count(255)));
            settings.appendChild(choiceSetting('Wiring', {rows: 'Row by row', zigzag: 'Zigzag'},
                () => this.zigzag ? 'zigzag' : 'rows', (v) => { this.zigzag = v === 'zigzag'; again(); }));
        } else if (this.layout === NEOPIXEL_CIRCLE) {
            settings.appendChild(editableSetting('Rings (LEDs, as wired)', () => this.rings.join(', '),
                (v) => this.setRings(v) ?? again()));
        } else {
            settings.appendChild(editableSetting('LEDs', () => this.ringLeds, (v) => this.setRingLeds(v) ?? again(), count(255)));
        }
        settings.appendChild(editableSetting('Brightness (0-255)', () => this.brightness, (v) => this.setBrightness(v),
            {type: 'number', min: 0, max: 255, step: 1}));

        settings.appendChild(selectSetting('Graphic',
            [[NEOPIXEL_GFX_BAR, 'Bar level'], [NEOPIXEL_GFX_POINT, 'Moving point'], [NEOPIXEL_GFX_PATCH, 'Patch']],
            () => this.graphic, (v) => { this.setGraphic(Number(v)); again(); }));
        if (this.graphic !== NEOPIXEL_GFX_BAR) this.pointSettings(settings, again);
        else this.barSettings(settings, again);
        this.colourSettings(settings, again);
        settings.appendChild(this.previewSetting());
    }

    direction(settings, again, forward, back) {
        settings.appendChild(choiceSetting('Direction', {forward, back},
            () => this.reverse ? 'back' : 'forward', (v) => { this.reverse = v === 'back'; again(); }));
    }

    // A bar's settings: what it runs along and where, and which way.
    barSettings(settings, again) {
        const along = (options) => settings.appendChild(choiceSetting('Along', options,
            () => String(this.along), (v) => { this.setAlong(Number(v)); again(); }));
        const position = (label) => settings.appendChild(editableSetting(label, () => this.position + 1,
            (v) => this.setPosition(v) ?? again(), {type: 'number', min: 1, max: this.positions(), step: 1}));

        if (this.layout === NEOPIXEL_ARRAY) {
            along({[NEOPIXEL_BAR_ROW]: 'Row', [NEOPIXEL_BAR_COLUMN]: 'Column'});
            const row = this.along === NEOPIXEL_BAR_ROW;
            position(row ? 'Row' : 'Column');
            this.direction(settings, again, row ? 'Left to right' : 'Bottom up', row ? 'Right to left' : 'Top down');
        } else if (this.layout === NEOPIXEL_CIRCLE) {
            along({[NEOPIXEL_BAR_ROW]: 'Around a ring', [NEOPIXEL_BAR_COLUMN]: 'Centre out'});
            if (this.along === NEOPIXEL_BAR_ROW) {
                position('Ring');
                this.direction(settings, again, 'As wired', 'Reversed');
            } else {
                this.direction(settings, again, 'Centre out', 'Outside in');
            }
        } else {
            position('Starts at LED');
            this.direction(settings, again, 'As wired', 'Reversed');
        }
    }

    // A point's or patch's settings: where it starts and ends (a patch: its corners on an
    // array), and on a ring which way round.
    pointSettings(settings, again) {
        const place = (end, k, label, max) => settings.appendChild(editableSetting(label, () => this[end][k] + 1,
            (v) => this.setPlace(end, k, v) ?? again(), {type: 'number', min: 1, max, step: 1}));
        for (const [end, name] of [['from', 'Start'], ['to', 'End']]) {
            if (this.layout === NEOPIXEL_ARRAY) {
                place(end, 0, `${name} row`, this.rows);
                place(end, 1, `${name} column`, this.cols);
            } else if (this.layout === NEOPIXEL_CIRCLE) {
                place(end, 0, `${name} ring`, this.rings.length);
                place(end, 1, `${name} LED`, this.rings[this[end][0]]);
            } else {
                place(end, 1, `${name} LED`, this.ringLeds);
            }
        }
        if (this.layout !== NEOPIXEL_ARRAY) this.direction(settings, again, 'As wired', 'Reversed');
    }

    // Its colour: one, or from a colour at 0 to a colour at 1 with its input.
    colourSettings(settings, again) {
        settings.appendChild(choiceSetting('Colour', {fixed: 'Fixed', input: 'From input'},
            () => this.colourInput ? 'input' : 'fixed', (v) => { this.colourInput = v === 'input'; again(); }));
        if (this.colourInput) {
            settings.appendChild(this.colourPicker('Colour at 0', false));
            settings.appendChild(this.colourPicker('Colour at 1', true));
        } else {
            settings.appendChild(this.colourPicker('Fixed colour', false));
        }
    }

    colourPicker(text, at1) {
        const row = document.createElement('div');
        row.className = 'editable colour';
        const label = document.createElement('span');
        label.className = 'label';
        label.textContent = text + ':';
        const input = document.createElement('input');
        input.type = 'color';
        input.value = this.colourHex(at1);
        input.setAttribute('aria-label', text);
        input.addEventListener('input', () => { this.setColour(input.value, at1); this.drawPreview(); });
        row.append(label, input);
        return row;
    }

    // The preview, with a slider for the level it shows.
    previewSetting() {
        const box = document.createElement('div');
        box.className = 'neopixel-box';
        const row = document.createElement('div');
        row.className = 'editable';
        const label = document.createElement('span');
        label.className = 'label';
        label.textContent = 'Preview at:';
        const input = document.createElement('input');
        input.type = 'range';
        input.min = 0;
        input.max = 100;
        input.value = Math.round(this.preview * 100);
        input.setAttribute('aria-label', 'Preview level, %');
        input.addEventListener('input', () => { this.preview = input.value / 100; this.drawPreview(); });
        row.append(label, input);
        const drawing = document.createElement('div');
        drawing.className = 'neopixel-drawing';
        box.append(row, drawing);
        this.previewElm = drawing;
        drawing.innerHTML = this.previewSvg();
        return box;
    }

    drawPreview() {
        if (this.previewElm?.isConnected) this.previewElm.innerHTML = this.previewSvg();
    }

    buildUI(commandInterface) {}

    // ── Saving ──

    // Its graphic's settings, as saved: a bar's or a point's, then its options and
    // colour, and with NEOPIXEL_GFX_COLOUR_INPUT its colour at 1.
    graphicBytes() {
        const options = (this.reverse ? NEOPIXEL_GFX_REVERSE : 0) | (this.colourInput ? NEOPIXEL_GFX_COLOUR_INPUT : 0);
        const head = this.graphic !== NEOPIXEL_GFX_BAR ? [...this.from, ...this.to] : [this.along, this.position];
        return [...head, options, ...this.colour, ...(this.colourInput ? this.colourAt1 : [])];
    }

    marshal() {
        const size = this.size(), gfx = this.graphicBytes();
        const pay = [...connRef(this.valueConn()), ...connRef(this.enableConn()),
            this.pin, this.layout, this.zigzag ? NEOPIXEL_ZIGZAG : 0, this.brightness,
            size.length, ...size, this.graphic, gfx.length, ...gfx];
        return [this.tc, MSG_FLAG_OUTPUT, (this.id >> 8) & 0xFF, this.id & 0xFF, pay.length, ...pay];
    }

    // Its settings, after its two source references (r is one's length). One the
    // device wouldn't run keeps the defaults (see neopixel_payload_read); so does a pin
    // another node has.
    restore(pay, r, len) {
        const at = 2 * r, q = (i) => pay[at + i];
        if (len < at + 5) return;
        const k = q(4);
        if (k < 1 || k > NEOPIXEL_RINGS_MAX || len < at + 7 + k || len !== at + 7 + k + q(6 + k)) return;
        const [pin, layout, options, brightness] = [0, 1, 2, 3].map(q);
        const size = [...Array(k).keys()].map(i => q(5 + i));
        const graphic = q(5 + k), g = (i) => q(7 + k + i);
        if (options & ~NEOPIXEL_ZIGZAG || size.includes(0)) return;
        const leds = layout === NEOPIXEL_ARRAY ? size[0] * size[1] : size.reduce((a, b) => a + b, 0);
        if (leds > NEOPIXEL_LEDS_MAX || ![NEOPIXEL_ARRAY, NEOPIXEL_CIRCLE, NEOPIXEL_DOUGHNUT].includes(layout)
            || (layout === NEOPIXEL_ARRAY && k !== 2) || (layout === NEOPIXEL_DOUGHNUT && k !== 1)) return;
        const base = graphic === NEOPIXEL_GFX_BAR ? NEOPIXEL_BAR_LEN : graphic === NEOPIXEL_GFX_POINT ? NEOPIXEL_POINT_LEN
            : graphic === NEOPIXEL_GFX_PATCH ? NEOPIXEL_PATCH_LEN : 0;
        if (!base) return;
        const gfxOptions = g(base - NEOPIXEL_COLOUR_LEN - 1);
        const colourInput = !!(gfxOptions & NEOPIXEL_GFX_COLOUR_INPUT);
        if (gfxOptions & ~(NEOPIXEL_GFX_REVERSE | NEOPIXEL_GFX_COLOUR_INPUT)
            || q(6 + k) !== base + (colourInput ? NEOPIXEL_COLOUR_LEN : 0)) return;

        // Its layout first, which the graphic's settings are checked against.
        const was = {layout: this.layout, rows: this.rows, cols: this.cols, rings: this.rings, ringLeds: this.ringLeds};
        this.layout = layout;
        if (layout === NEOPIXEL_ARRAY) [this.rows, this.cols] = size;
        else if (layout === NEOPIXEL_CIRCLE) this.rings = size;
        else this.ringLeds = size[0];
        let ok;
        if (graphic !== NEOPIXEL_GFX_BAR) {
            ok = this.placeValid([g(0), g(1)]) && this.placeValid([g(2), g(3)]);
        } else {
            const [along, position] = [g(0), g(1)];
            ok = along <= NEOPIXEL_BAR_COLUMN && (layout === NEOPIXEL_ARRAY ? position < size[along === NEOPIXEL_BAR_ROW ? 0 : 1]
                : layout === NEOPIXEL_CIRCLE ? (along === NEOPIXEL_BAR_COLUMN ? position === 0 : position < k)
                : along === NEOPIXEL_BAR_ROW && position < size[0]);
        }
        if (!ok) {
            Object.assign(this, was);
            return;
        }
        if (graphic !== NEOPIXEL_GFX_BAR) {
            this.from = [g(0), g(1)];
            this.to = [g(2), g(3)];
        } else {
            this.along = g(0);
            this.position = g(1);
        }
        if (pinUsable(pin) && !pinsInUse(this).has(pin)) this.pin = pin;
        this.zigzag = !!(options & NEOPIXEL_ZIGZAG);
        this.brightness = brightness;
        this.graphic = graphic;
        this.reverse = !!(gfxOptions & NEOPIXEL_GFX_REVERSE);
        this.colourInput = colourInput;
        this.colour = [base - 3, base - 2, base - 1].map(g);
        if (colourInput) this.colourAt1 = [base, base + 1, base + 2].map(g);
        this.keepPosition();
    }
}

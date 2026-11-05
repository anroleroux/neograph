// NeoPixels: a chain of WS2812 LEDs on one pin, laid out as an array (rows of
// columns), a circle (rings, one inside the other) or a doughnut (one ring). Its one
// floating input is taken as a fraction of its source's full scale, as a servo takes
// it, and its graphic draws that on the LEDs: for now a bar level indicator. The
// device drives them; the browser only shows a preview in the settings.
class uiNeoPixel extends uiComponents {
    constructor(x = 0, y = 0) {
        super("neopixel", x, y);

        this.tc = MSG_HW_NEOPIXEL;

        this.inputs.push(
            {
                type: "float",
                conns: [],
                max: 1
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.pin = freePin(this, 16);      // 16, unless another node uses it
        this.layout = NEOPIXEL_ARRAY;
        this.rows = 8;                     // an array
        this.cols = 8;
        this.zigzag = false;               // an array: every other row runs back
        this.rings = [24, 16, 12, 8, 1];   // a circle: each ring's LEDs, in wiring order
        this.ringLeds = 24;                // a doughnut
        this.brightness = 40;              // 0-255: NeoPixels are bright, and draw up to 60 mA each
        this.graphic = NEOPIXEL_GFX_BAR;
        this.along = NEOPIXEL_BAR_ROW;     // a bar: along a row or column; around a ring or across the rings
        this.position = 0;                 // which row, column or ring; a doughnut's first LED. From 0
        this.reverse = false;
        this.colour = [0, 255, 0];
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

    // After a change of layout, size or direction: a bar position it doesn't have
    // goes back to the first.
    keepPosition() {
        if (this.layout === NEOPIXEL_DOUGHNUT) this.along = NEOPIXEL_BAR_ROW;
        if (this.position >= this.positions()) this.position = 0;
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
        if (g !== NEOPIXEL_GFX_BAR) return false;
        this.graphic = g;
    }

    setAlong(along) {
        if (along !== NEOPIXEL_BAR_ROW && along !== NEOPIXEL_BAR_COLUMN) return false;
        if (this.layout === NEOPIXEL_DOUGHNUT && along !== NEOPIXEL_BAR_ROW) return false;
        this.along = along;
        this.keepPosition();
    }

    // A position as shown, from 1.
    setPosition(v) {
        v = uiPWM.parse(v, 1, this.positions());
        if (v === null) return false;
        this.position = v - 1;
    }

    // A colour as #rrggbb.
    setColour(hex) {
        const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(String(hex).trim());
        if (!m) return false;
        this.colour = m.slice(1).map(h => parseInt(h, 16));
    }

    colourHex() {
        return '#' + this.colour.map(c => c.toString(16).padStart(2, '0')).join('');
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

    // Each LED's colour, [r, g, b], for its input at `fraction` of full scale.
    render(fraction) {
        const out = Array.from({length: this.leds()}, () => [0, 0, 0]);
        const steps = this.barSteps(), lit = Math.max(0, Math.min(1, fraction)) * steps;
        for (let e = 0; e < steps && lit > e; e++) {
            const k = Math.min(1, lit - e) * this.brightness / 255;
            const c = this.colour.map(v => Math.floor(v * k + 0.5));
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

        settings.appendChild(selectSetting('Graphic', [[NEOPIXEL_GFX_BAR, 'Bar level']],
            () => this.graphic, (v) => { this.setGraphic(Number(v)); again(); }));
        this.barSettings(settings, again);
        settings.appendChild(this.previewSetting());
    }

    // A bar's settings: what it runs along and where, which way, and its colour.
    barSettings(settings, again) {
        const along = (options) => settings.appendChild(choiceSetting('Along', options,
            () => String(this.along), (v) => { this.setAlong(Number(v)); again(); }));
        const position = (label) => settings.appendChild(editableSetting(label, () => this.position + 1,
            (v) => this.setPosition(v) ?? again(), {type: 'number', min: 1, max: this.positions(), step: 1}));
        const direction = (forward, back) => settings.appendChild(choiceSetting('Direction', {forward, back},
            () => this.reverse ? 'back' : 'forward', (v) => { this.reverse = v === 'back'; again(); }));

        if (this.layout === NEOPIXEL_ARRAY) {
            along({[NEOPIXEL_BAR_ROW]: 'Row', [NEOPIXEL_BAR_COLUMN]: 'Column'});
            const row = this.along === NEOPIXEL_BAR_ROW;
            position(row ? 'Row' : 'Column');
            direction(row ? 'Left to right' : 'Bottom up', row ? 'Right to left' : 'Top down');
        } else if (this.layout === NEOPIXEL_CIRCLE) {
            along({[NEOPIXEL_BAR_ROW]: 'Around a ring', [NEOPIXEL_BAR_COLUMN]: 'Centre out'});
            if (this.along === NEOPIXEL_BAR_ROW) {
                position('Ring');
                direction('As wired', 'Reversed');
            } else {
                direction('Centre out', 'Outside in');
            }
        } else {
            position('Starts at LED');
            direction('As wired', 'Reversed');
        }
        settings.appendChild(this.colourSetting());
    }

    colourSetting() {
        const row = document.createElement('div');
        row.className = 'editable colour';
        const label = document.createElement('span');
        label.className = 'label';
        label.textContent = 'Colour:';
        const input = document.createElement('input');
        input.type = 'color';
        input.value = this.colourHex();
        input.addEventListener('input', () => { this.setColour(input.value); this.drawPreview(); });
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

    marshal() {
        const size = this.size();
        const bar = [this.along, this.position, this.reverse ? NEOPIXEL_BAR_REVERSE : 0, ...this.colour];
        const pay = [...this.sourceRef(), this.pin, this.layout, this.zigzag ? NEOPIXEL_ZIGZAG : 0, this.brightness,
            size.length, ...size, this.graphic, bar.length, ...bar];
        return [this.tc, MSG_FLAG_OUTPUT, (this.id >> 8) & 0xFF, this.id & 0xFF, pay.length, ...pay];
    }

    // Its settings, after its source reference. One the device wouldn't run keeps the
    // defaults (see neopixel_payload_read); so does a pin another node has.
    restore(pay, r, len) {
        const q = (i) => pay[r + i];
        if (len < r + 5) return;
        const k = q(4);
        if (k < 1 || k > NEOPIXEL_RINGS_MAX || len < r + 7 + k || len !== r + 7 + k + q(6 + k)) return;
        const [pin, layout, options, brightness] = [0, 1, 2, 3].map(q);
        const size = [...Array(k).keys()].map(i => q(5 + i));
        const g = (i) => q(5 + k + i);
        if (options & ~NEOPIXEL_ZIGZAG || size.includes(0) || g(0) !== NEOPIXEL_GFX_BAR || g(1) !== NEOPIXEL_BAR_LEN) return;
        const [along, position, barOptions] = [g(2), g(3), g(4)];
        if (barOptions & ~NEOPIXEL_BAR_REVERSE || along > NEOPIXEL_BAR_COLUMN) return;
        const leds = layout === NEOPIXEL_ARRAY ? size[0] * size[1] : size.reduce((a, b) => a + b, 0);
        if (leds > NEOPIXEL_LEDS_MAX) return;
        if (layout === NEOPIXEL_ARRAY) {
            if (k !== 2 || position >= size[along === NEOPIXEL_BAR_ROW ? 0 : 1]) return;
            [this.rows, this.cols] = size;
        } else if (layout === NEOPIXEL_CIRCLE) {
            if (along === NEOPIXEL_BAR_COLUMN ? position !== 0 : position >= k) return;
            this.rings = size;
        } else if (layout === NEOPIXEL_DOUGHNUT) {
            if (k !== 1 || along !== NEOPIXEL_BAR_ROW || position >= size[0]) return;
            this.ringLeds = size[0];
        } else {
            return;
        }
        if (pinUsable(pin) && !pinsInUse(this).has(pin)) this.pin = pin;
        this.layout = layout;
        this.zigzag = !!(options & NEOPIXEL_ZIGZAG);
        this.brightness = brightness;
        this.graphic = g(0);
        this.along = along;
        this.position = position;
        this.reverse = !!(barOptions & NEOPIXEL_BAR_REVERSE);
        this.colour = [g(5), g(6), g(7)];
    }
}

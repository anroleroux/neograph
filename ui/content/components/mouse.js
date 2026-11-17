// The gestures a mouse's pad takes beyond one finger, in MOUSE_GESTURE_ order:
// [name, whether it is a tap].
const MOUSE_GESTURE_NAMES = [
    ['Two-finger drag', false],
    ['Three-finger drag', false],
    ['Pinch', false],
    ['Two-finger tap', true],
    ['Three-finger tap', true],
];

// The keyboard modifiers a gesture may hold (HID bits, as a keyboard's), and the mouse
// buttons (HID bits) a gesture may hold or click, in the order the pad shows them.
const MOUSE_MODIFIER_NAMES = [[0x01, 'Ctrl'], [0x02, 'Shift'], [0x04, 'Alt'], [0x08, 'Win']];
const MOUSE_BUTTON_NAMES = [[0x01, 'Left'], [0x04, 'Middle'], [0x02, 'Right']];

// What each gesture does in a preset: [modifiers, buttons, MOUSE_ACT_].
const MOUSE_PRESETS = {
    standard: [
        [0x00, 0x00, MOUSE_ACT_SCROLL],     // two fingers scroll
        [0x00, 0x01, MOUSE_ACT_MOVE],       // three fingers drag with the left button
        [0x01, 0x00, MOUSE_ACT_SCROLL],     // a pinch is Ctrl+wheel: zoom
        [0x00, 0x02, MOUSE_ACT_CLICK],      // a two-finger tap right-clicks
        [0x00, 0x04, MOUSE_ACT_CLICK],      // a three-finger tap middle-clicks
    ],
    blender: [
        [0x00, 0x04, MOUSE_ACT_MOVE],       // orbit: middle-drag
        [0x02, 0x04, MOUSE_ACT_MOVE],       // pan: Shift+middle-drag
        [0x00, 0x00, MOUSE_ACT_SCROLL],     // zoom: the wheel
        [0x00, 0x02, MOUSE_ACT_CLICK],
        [0x00, 0x04, MOUSE_ACT_CLICK],
    ],
};

const MOUSE_TAP_MS = 250;       // a touch shorter than this, that moved less than MOUSE_TAP_PX, is a tap
const MOUSE_TAP_PX = 10;
const MOUSE_PINCH_PX = 8;       // two fingers: a pinch once the gap between them changes this much
                                // more than they move together; a drag the other way round
const MOUSE_CLICK_MS = 80;      // how long a tap's click holds its buttons

// A settings row of on/off buttons, one for each [bit, name]: as a keyboard's Hold.
function bitsSetting(label, names, get, set) {
    const row = document.createElement('div');
    row.className = 'editable choice';
    const span = document.createElement('span');
    span.className = 'label';
    span.textContent = label + ':';
    row.appendChild(span);
    names.forEach(([bit, name]) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = name;
        button.classList.toggle('sel', !!(get() & bit));
        button.addEventListener('click', () => {
            set(get() ^ bit);
            button.classList.toggle('sel', !!(get() & bit));
        });
        row.appendChild(button);
    });
    return row;
}

// The USB mouse: a touch pad in the Command view that moves the pointer of the PC
// the device is plugged into, with Left, Middle and Right buttons and a scroll strip
// each way. One finger moves the pointer, and a tap clicks; two- and three-finger
// drags, a pinch and two- and three-finger taps do what the settings say, holding
// keyboard modifiers and mouse buttons as they go: Shift+middle-drag for Blender's pan.
// It has no wires: the browser sends the device what the pad does (PROTOCOL.md,
// "USB mice"), and the device sends it on to the PC.
class uiMouse extends uiComponents {
    constructor(x = 0, y = 0) {
        super("mouse", x, y);
        this.label = "Mouse";

        this.tc = MSG_HW_MOUSE;

        this.tapClick = true;       // a one-finger tap clicks the left button
        this.reverse = false;       // scrolling follows the finger, as on a phone
        this.speed = 20;            // tenths of a count per pixel dragged
        this.step = 24;             // pixels dragged a wheel click
        this.applyPreset('standard');

        this.held = { pad: 0, gesture: 0, mods: 0 };    // buttons held by the pad's buttons, and by a gesture; modifiers
        this.sent = { buttons: 0, mods: 0 };            // as last sent
        this.rest = [0, 0, 0, 0];                       // moves not yet whole: X, Y, the wheel, the horizontal wheel

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">MSE</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <title>A touch pad in the Command view</title>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // ── Its settings ──

    applyPreset(name) {
        const preset = MOUSE_PRESETS[name];
        if (!preset) return false;
        this.gestures = preset.map(([mods, buttons, act]) => ({ mods, buttons, act }));
    }

    // The preset its gestures match; null if none.
    preset() {
        return Object.keys(MOUSE_PRESETS).find(name => MOUSE_PRESETS[name].every(([mods, buttons, act], g) =>
            this.gestures[g].mods === mods && this.gestures[g].buttons === buttons && this.gestures[g].act === act)) ?? null;
    }

    static isTap(g) { return MOUSE_GESTURE_NAMES[g][1]; }

    // What gesture g may do: a drag or pinch moves or scrolls; a tap clicks.
    static acts(g) {
        return uiMouse.isTap(g) ? [[MOUSE_ACT_NONE, 'Nothing'], [MOUSE_ACT_CLICK, 'Click']]
            : [[MOUSE_ACT_NONE, 'Nothing'], [MOUSE_ACT_MOVE, 'Move pointer'], [MOUSE_ACT_SCROLL, 'Scroll']];
    }

    setAct(g, act) {
        act = Number(act);
        if (!uiMouse.acts(g).some(([a]) => a === act)) return false;
        this.gestures[g].act = act;
    }

    setMods(g, mods) {
        if (mods & ~MOUSE_MODIFIERS) return false;
        this.gestures[g].mods = mods;
    }

    setButtons(g, buttons) {
        if (buttons & ~MOUSE_BUTTONS) return false;
        this.gestures[g].buttons = buttons;
    }

    // Pointer speed: counts per pixel, 0.1 to 10.
    setSpeed(v) {
        v = Math.round(Number(String(v).trim()) * 10);
        if (!Number.isInteger(v) || v < MOUSE_SPEED_MIN || v > MOUSE_SPEED_MAX) return false;
        this.speed = v;
    }

    // Scroll step: pixels a wheel click, MOUSE_STEP_MIN to 255.
    setStep(v) {
        v = String(v).trim();
        if (!/^\d+$/.test(v) || Number(v) < MOUSE_STEP_MIN || Number(v) > 255) return false;
        this.step = Number(v);
    }

    // Its settings from a payload; an invalid one keeps them as they are.
    restore(pay, r, len) {
        if (len !== MOUSE_PAYLOAD_LEN || pay[0] & ~(MOUSE_TAP_CLICK | MOUSE_SCROLL_REVERSE)) return;
        if (pay[1] < MOUSE_SPEED_MIN || pay[1] > MOUSE_SPEED_MAX || pay[2] < MOUSE_STEP_MIN) return;
        const gestures = MOUSE_GESTURE_NAMES.map((_, g) => ({ mods: pay[3 + 3 * g], buttons: pay[4 + 3 * g], act: pay[5 + 3 * g] }));
        if (gestures.some(({ mods, buttons, act }, g) => mods & ~MOUSE_MODIFIERS || buttons & ~MOUSE_BUTTONS
            || !uiMouse.acts(g).some(([a]) => a === act))) return;
        this.tapClick = !!(pay[0] & MOUSE_TAP_CLICK);
        this.reverse = !!(pay[0] & MOUSE_SCROLL_REVERSE);
        this.speed = pay[1];
        this.step = pay[2];
        this.gestures = gestures;
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(choiceSetting('Preset', { standard: 'Standard', blender: 'Blender' },
            () => this.preset() ?? '', (name) => { this.applyPreset(name); this.createSettings(); }));
        settings.appendChild(choiceSetting('Tap to click', { on: 'On', off: 'Off' },
            () => this.tapClick ? 'on' : 'off', (v) => { this.tapClick = v === 'on'; }));
        settings.appendChild(choiceSetting('Scrolling', { traditional: 'Traditional', natural: 'Natural' },
            () => this.reverse ? 'natural' : 'traditional', (v) => { this.reverse = v === 'natural'; }));
        settings.appendChild(editableSetting('Pointer speed', () => (this.speed / 10).toFixed(1),
            (v) => this.setSpeed(v), {type: 'number', min: MOUSE_SPEED_MIN / 10, max: MOUSE_SPEED_MAX / 10, step: 0.1}));
        settings.appendChild(editableSetting('Scroll step (px)', () => String(this.step),
            (v) => this.setStep(v), {type: 'number', min: MOUSE_STEP_MIN, max: 255, step: 1}));

        // Each gesture: what it does, then the keys it holds, and the buttons it holds
        // (a move) or clicks (a tap). A scroll holds keys only.
        MOUSE_GESTURE_NAMES.forEach(([name], g) => {
            const gesture = this.gestures[g];
            settings.appendChild(selectSetting(name, uiMouse.acts(g), () => gesture.act,
                (act) => { this.setAct(g, act); this.createSettings(); }));
            if (gesture.act === MOUSE_ACT_NONE) return;
            settings.appendChild(bitsSetting('Keys', MOUSE_MODIFIER_NAMES, () => gesture.mods,
                (mods) => { this.setMods(g, mods); this.refreshPreset(); }));
            if (gesture.act !== MOUSE_ACT_SCROLL) {
                settings.appendChild(bitsSetting('Buttons', MOUSE_BUTTON_NAMES, () => gesture.buttons,
                    (buttons) => { this.setButtons(g, buttons); this.refreshPreset(); }));
            }
        });
        settings.appendChild(orderSetting(this));
    }

    // The Preset row marks the preset the gestures match, if any, after a change.
    refreshPreset() {
        const row = [...document.querySelectorAll('#settings .choice')]
            .find(r => r.querySelector('.label').textContent === 'Preset:');
        row?.querySelectorAll('button').forEach(b => b.classList.toggle('sel', b.dataset.value === this.preset()));
    }

    marshal() {
        const options = (this.tapClick ? MOUSE_TAP_CLICK : 0) | (this.reverse ? MOUSE_SCROLL_REVERSE : 0);
        const pay = [options, this.speed, this.step, ...this.gestures.flatMap(({ mods, buttons, act }) =>
            [mods, act === MOUSE_ACT_SCROLL || act === MOUSE_ACT_NONE ? 0 : buttons, act])];
        return [this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, pay.length, ...pay];
    }

    // ── Commands to the device ──

    // What is held now: the buttons, and the modifiers.
    buttons() { return this.held.pad | this.held.gesture; }

    // Add to the moves waiting: pointer counts, X right and Y down; wheel clicks, up and right.
    add(dx, dy, wheel = 0, pan = 0) {
        [dx, dy, wheel, pan].forEach((d, a) => { this.rest[a] += d; });
        this.schedule();
    }

    // Send what changed at the next frame: at most one command a frame.
    schedule() {
        if (this.frame) return;
        this.frame = requestAnimationFrame(() => { this.frame = null; this.flush(); });
    }

    // One command: the moves waiting, in whole counts and clicks (the rest waits), and
    // what is held. New modifiers go to the PC a command before new buttons, and
    // modifiers let go a command after the buttons, so a program sees Shift+middle-drag.
    // While anything is held, the command is repeated every MOUSE_KEEPALIVE_MS, so the
    // device doesn't let go of it (MOUSE_HOLD_MS).
    flush(keepalive = false) {
        const want = { buttons: this.buttons(), mods: this.held.mods };
        const buttons = want.mods & ~this.sent.mods ? this.sent.buttons & want.buttons : want.buttons;
        const mods = this.sent.buttons & ~buttons ? this.sent.mods | want.mods : want.mods;
        const limits = [32767, 32767, 127, 127];
        const moves = this.rest.map((r, a) => Math.max(-limits[a], Math.min(limits[a], Math.trunc(r))));
        moves.forEach((m, a) => { this.rest[a] -= m; });
        const moved = moves.some(m => m !== 0);
        if (!moved && !keepalive && buttons === this.sent.buttons && mods === this.sent.mods) return;

        this.sent = { buttons, mods };
        this.send([buttons, mods, (moves[0] >> 8) & 0xFF, moves[0] & 0xFF, (moves[1] >> 8) & 0xFF, moves[1] & 0xFF,
            moves[2] & 0xFF, moves[3] & 0xFF]);

        if (buttons !== want.buttons || mods !== want.mods || this.rest.some(r => Math.abs(r) >= 1)) this.schedule();
        clearTimeout(this.keepalive);
        if (buttons || mods) this.keepalive = setTimeout(() => this.flush(true), MOUSE_KEEPALIVE_MS);
    }

    send(report) {
        const pkt = Uint8Array.from([PKT_COMMAND, 0, MSG_HEADER_LEN + report.length,
            this.tc, 0, (this.id >> 8) & 0xFF, this.id & 0xFF, report.length, ...report]);
        console.log(pkt) //debug
        socket.send(pkt); //online
    }

    // Let go of everything: the pad's buttons, a gesture's hold. At once: a hidden
    // page has no frames. The buttons go in one command, the modifiers in the next.
    letGo() {
        this.held = { pad: 0, gesture: 0, mods: 0 };
        this.flush();
        this.flush();
    }

    remove() {
        this.letGo();
        super.remove();
    }

    // A tap's click: its modifiers, then its buttons, for MOUSE_CLICK_MS; then nothing.
    click(mods, buttons) {
        if (!buttons) return;
        this.held.gesture = buttons;
        this.held.mods = mods;
        this.schedule();
        setTimeout(() => { this.held.gesture = 0; this.held.mods = 0; this.schedule(); }, MOUSE_CLICK_MS);
    }

    // ── Its tile ──

    // In the Command view: a tile as wide as four controls (all of a phone's), with the
    // label, the pad, a scroll strip right of it and one below, and the three buttons.
    buildUI(commandInterface) {
        this.letGo();
        const control = document.createElement('div');
        control.className = 'control-mouse';
        control.innerHTML = `<span class="name"></span>
            <div class="mouse">
                <div class="mouse-pad" role="application"></div>
                <div class="mouse-strip vertical" role="scrollbar" aria-orientation="vertical"></div>
                <div class="mouse-strip horizontal" role="scrollbar" aria-orientation="horizontal"></div>
                <div class="mouse-buttons"></div>
            </div>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;
        const pad = control.querySelector('.mouse-pad');
        pad.setAttribute('aria-label', `${this.label}: drag to move the pointer, tap to click`);
        control.querySelector('.vertical').setAttribute('aria-label', 'Scroll up and down');
        control.querySelector('.horizontal').setAttribute('aria-label', 'Scroll left and right');

        // The buttons: each held while touched.
        const row = control.querySelector('.mouse-buttons');
        MOUSE_BUTTON_NAMES.forEach(([bit, name]) => {
            const button = document.createElement('button');
            button.type = 'button';
            button.textContent = name;
            button.setAttribute('aria-pressed', 'false');
            const press = (on) => {
                this.held.pad = on ? this.held.pad | bit : this.held.pad & ~bit;
                button.setAttribute('aria-pressed', String(on));
                this.schedule();
            };
            button.addEventListener('pointerdown', (e) => { button.setPointerCapture(e.pointerId); press(true); });
            ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(t => button.addEventListener(t, () => press(false)));
            button.addEventListener('keydown', (e) => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) press(true); });
            button.addEventListener('keyup', (e) => { if (e.key === ' ' || e.key === 'Enter') press(false); });
            button.addEventListener('contextmenu', (e) => e.preventDefault());
            row.appendChild(button);
        });

        // The strips: dragging along one turns its wheel, a click each scroll step.
        const strip = (elm, vertical) => {
            let last = null;
            elm.addEventListener('pointerdown', (e) => { elm.setPointerCapture(e.pointerId); last = [e.clientX, e.clientY]; });
            elm.addEventListener('pointermove', (e) => {
                if (!last || !elm.hasPointerCapture(e.pointerId)) return;
                const d = vertical ? e.clientY - last[1] : e.clientX - last[0];
                last = [e.clientX, e.clientY];
                this.scroll(vertical ? 0 : d, vertical ? d : 0);
            });
            ['pointerup', 'pointercancel'].forEach(t => elm.addEventListener(t, () => { last = null; }));
        };
        strip(control.querySelector('.vertical'), true);
        strip(control.querySelector('.horizontal'), false);

        this.padEvents(pad);
        commandInterface.appendChild(control);
    }

    // A drag of dx, dy pixels as scrolling: traditionally a finger up turns the wheel
    // up and a finger right the horizontal wheel right; naturally, the other way.
    scroll(dx, dy) {
        const sign = this.reverse ? -1 : 1;
        this.add(0, 0, sign * -dy / this.step, sign * dx / this.step);
    }

    // What the pad does with the fingers on it. A touch starts with the first finger
    // down and ends with the last up; the most fingers it had choose what it is: one
    // moves the pointer, two drag or pinch (whichever they do first), three drag.
    padEvents(pad) {
        const fingers = new Map();      // pointer ID → [x, y]
        let touch = null;               // { most, start, moved, kind, gap }

        const gap = () => {
            const [a, b] = [...fingers.values()];
            return Math.hypot(a[0] - b[0], a[1] - b[1]);
        };
        // What gesture g does now: hold its keys, and its buttons for a move.
        const begin = (g) => {
            const { mods, buttons, act } = this.gestures[g];
            touch.kind = g;
            if (act === MOUSE_ACT_MOVE || act === MOUSE_ACT_SCROLL) this.held.mods = mods;
            if (act === MOUSE_ACT_MOVE) this.held.gesture = buttons;
            this.schedule();
        };
        const end = () => {
            if (touch.kind !== null) { this.held.gesture = 0; this.held.mods = 0; this.schedule(); }
        };

        pad.addEventListener('pointerdown', (e) => {
            pad.setPointerCapture(e.pointerId);
            fingers.set(e.pointerId, [e.clientX, e.clientY]);
            if (!touch) touch = { most: 0, start: performance.now(), moved: 0, kind: null, gap: 0, drift: 0 };
            touch.most = Math.max(touch.most, fingers.size);
            if (fingers.size === 2 && touch.kind === null) { touch.gap = gap(); touch.drift = 0; }
            if (fingers.size === 3 && touch.kind !== MOUSE_GESTURE_DRAG3) { end(); begin(MOUSE_GESTURE_DRAG3); }
        });

        pad.addEventListener('pointermove', (e) => {
            const was = fingers.get(e.pointerId);
            if (!was || !touch) return;
            const dx = e.clientX - was[0], dy = e.clientY - was[1];
            fingers.set(e.pointerId, [e.clientX, e.clientY]);
            touch.moved += Math.hypot(dx, dy);
            const n = fingers.size;

            // Two fingers: a pinch if the gap between them changes first, else a drag.
            // The move that tells them apart counts: a pinch from the gap they started at.
            if (n === 2 && touch.kind === null) {
                touch.drift += Math.hypot(dx, dy) / 2;
                const change = Math.abs(gap() - touch.gap);
                if (change > MOUSE_PINCH_PX && change > touch.drift) begin(MOUSE_GESTURE_PINCH);
                else if (touch.drift > MOUSE_PINCH_PX) begin(MOUSE_GESTURE_DRAG2);
                else return;
            }
            if (touch.most === 1) {
                this.add(dx * this.speed / 10, dy * this.speed / 10);
                return;
            }
            if (touch.kind === null) return;
            const { act } = this.gestures[touch.kind];
            if (touch.kind === MOUSE_GESTURE_PINCH) {
                if (n !== 2) return;
                const g = gap(), spread = g - touch.gap;    // apart: up
                touch.gap = g;
                if (act === MOUSE_ACT_MOVE) this.add(0, -spread * this.speed / 10);
                if (act === MOUSE_ACT_SCROLL) this.add(0, 0, spread / this.step);
                return;
            }
            // A drag: the fingers' middle moves by this finger's move over their number.
            if (act === MOUSE_ACT_MOVE) this.add(dx / n * this.speed / 10, dy / n * this.speed / 10);
            if (act === MOUSE_ACT_SCROLL) this.scroll(dx / n, dy / n);
        });

        const up = (e) => {
            if (!fingers.delete(e.pointerId) || !touch || fingers.size) return;
            const tap = performance.now() - touch.start < MOUSE_TAP_MS && touch.moved < MOUSE_TAP_PX;
            end();
            if (tap && touch.most === 1 && this.tapClick) this.click(0, 0x01);
            if (tap && touch.most >= 2) {
                const g = this.gestures[touch.most === 2 ? MOUSE_GESTURE_TAP2 : MOUSE_GESTURE_TAP3];
                if (g.act === MOUSE_ACT_CLICK) this.click(g.mods, g.buttons);
            }
            touch = null;
        };
        pad.addEventListener('pointerup', up);
        pad.addEventListener('pointercancel', up);
        pad.addEventListener('contextmenu', (e) => e.preventDefault());
    }
}

// A page that is hidden (another tab, the phone locked) lets go of every mouse's buttons
// and keys, so none sticks on the PC.
document.addEventListener('visibilitychange', () => {
    if (document.hidden) Object.values(elements).forEach(n => n instanceof uiMouse && n.letGo());
});

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

// The parts of its tile that can be hidden, as [MOUSE_HIDE_ bit, name]. The pad stays.
const MOUSE_PARTS = [[MOUSE_HIDE_NAME, 'Title'], [MOUSE_HIDE_PRESETS, 'Presets'], [MOUSE_HIDE_VSTRIP, 'Vertical scroll'],
    [MOUSE_HIDE_HSTRIP, 'Horizontal scroll'], [MOUSE_HIDE_BUTTONS, 'Buttons']];
const MOUSE_HIDE_ALL = MOUSE_PARTS.reduce((all, [bit]) => all | bit, 0);
// A new mouse shows its pad, its presets' buttons and its mouse buttons.
const MOUSE_HIDE_DEFAULT = MOUSE_HIDE_NAME | MOUSE_HIDE_VSTRIP | MOUSE_HIDE_HSTRIP;

// The presets a mouse starts with, and may add again: what each gesture does,
// [modifiers, buttons, MOUSE_ACT_].
const MOUSE_TEMPLATES = {
    Standard: [
        [0x00, 0x00, MOUSE_ACT_SCROLL],     // two fingers scroll
        [0x00, 0x01, MOUSE_ACT_MOVE],       // three fingers drag with the left button
        [0x01, 0x00, MOUSE_ACT_SCROLL],     // a pinch is Ctrl+wheel: zoom
        [0x00, 0x02, MOUSE_ACT_CLICK],      // a two-finger tap right-clicks
        [0x00, 0x04, MOUSE_ACT_CLICK],      // a three-finger tap middle-clicks
    ],
    Blender: [
        [0x00, 0x04, MOUSE_ACT_MOVE],       // orbit: middle-drag
        [0x02, 0x04, MOUSE_ACT_MOVE],       // pan: Shift+middle-drag
        [0x00, 0x00, MOUSE_ACT_SCROLL],     // zoom: the wheel
        [0x00, 0x02, MOUSE_ACT_CLICK],
        [0x00, 0x04, MOUSE_ACT_CLICK],
    ],
};

const mouseGestures = (rows) => rows.map(([mods, buttons, act]) => ({ mods, buttons, act }));
const utf8Length = (text) => new TextEncoder().encode(text).length;

const MOUSE_TAP_MS = 250;       // a touch shorter than this, that moved less than MOUSE_TAP_PX, is a tap
const MOUSE_TAP_PX = 10;
const MOUSE_PINCH_PX = 8;       // two fingers: told apart once each has moved this far (see twoFingers)
const MOUSE_CLICK_MS = 80;      // how long a tap's click holds its buttons

// A settings row of buttons that each do something: [text, what it does, whether it can].
function actionsSetting(label, actions) {
    const row = document.createElement('div');
    row.className = 'editable choice';
    const span = document.createElement('span');
    span.className = 'label';
    span.textContent = label + ':';
    row.appendChild(span);
    actions.forEach(([text, act, can]) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = text;
        button.disabled = !can;
        button.addEventListener('click', act);
        row.appendChild(button);
    });
    return row;
}

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
        this.hidden = MOUSE_HIDE_DEFAULT;   // the parts its tile leaves out: MOUSE_HIDE_ bits
        this.noThree = false;       // the pad takes no third finger
        this.speed = 20;            // tenths of a count per pixel dragged
        this.step = 24;             // pixels dragged a wheel click
        // Its presets, each a name and what its gestures do; and the one in use.
        this.presets = Object.entries(MOUSE_TEMPLATES).map(([name, rows]) => ({ name, gestures: mouseGestures(rows) }));
        this.active = 0;

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

    // ── Its presets ──

    // What the gestures of the preset in use do.
    get gestures() { return this.presets[this.active].gestures; }

    // Use preset k, on the pad and in the settings. A gesture's hold lets go: the
    // gesture may do something else in it.
    setActive(k) {
        k = Number(k);
        if (!Number.isInteger(k) || k < 0 || k >= this.presets.length) return false;
        this.active = k;
        if (this.held.gesture || this.held.mods) { this.held.gesture = 0; this.held.mods = 0; this.schedule(); }
        this.showActive?.();
        if (selected === this) this.createSettings();
    }

    // A preset's name: 1 to MOUSE_NAME_MAX bytes, no control characters, and not
    // another preset's (whatever the case); null if it can't be one.
    nameValid(name, except = -1) {
        name = String(name).trim();
        if (!name || utf8Length(name) > MOUSE_NAME_MAX || /[\x00-\x1F\x7F]/.test(name)) return null;
        return this.presets.some((p, k) => k !== except && p.name.toLowerCase() === name.toLowerCase()) ? null : name;
    }

    // A name for a new preset from `base`: shortened to fit, and numbered if taken. A
    // copy of "Sculpt 2" is "Sculpt 3", not "Sculpt 2 2".
    newName(base) {
        base = base.replace(/ \d+$/, '') || base;
        for (let n = 1; ; n++) {
            const tail = n === 1 ? '' : ' ' + n;
            let head = base;
            while (utf8Length(head + tail) > MOUSE_NAME_MAX) head = head.slice(0, -1);
            const name = this.nameValid(head.trim() + tail);
            if (name) return name;
        }
    }

    renamePreset(name) {
        name = this.nameValid(name, this.active);
        if (!name) return false;
        this.presets[this.active].name = name;
        buildCommandInterface();
    }

    // Add a preset, and use it: a copy of the one in use, or one of MOUSE_TEMPLATES.
    // False if it has MOUSE_PRESETS_MAX.
    addPreset(template = null) {
        if (this.presets.length >= MOUSE_PRESETS_MAX) return false;
        if (template && !MOUSE_TEMPLATES[template]) return false;
        const gestures = template ? mouseGestures(MOUSE_TEMPLATES[template])
            : this.gestures.map(g => ({ ...g }));
        this.presets.push({ name: this.newName(template ?? this.presets[this.active].name), gestures });
        this.active = this.presets.length - 1;
        buildCommandInterface();
        if (selected === this) this.createSettings();
    }

    // Delete the preset in use, and use the one before it. False if it is the last.
    deletePreset() {
        if (this.presets.length <= 1) return false;
        this.presets.splice(this.active, 1);
        this.active = Math.max(0, this.active - 1);
        buildCommandInterface();
        if (selected === this) this.createSettings();
    }

    // ── Its settings ──

    static isTap(g) { return MOUSE_GESTURE_NAMES[g][1]; }
    static threeFinger(g) { return g === MOUSE_GESTURE_DRAG3 || g === MOUSE_GESTURE_TAP3; }

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

    // Whether the pad takes a third finger: without, the three-finger gestures are off.
    setThree(on) {
        this.noThree = !on;
        if (selected === this) this.createSettings();
    }

    // The parts of its tile shown (MOUSE_HIDE_ bits set for each shown), and the tile
    // built again with them.
    setShown(shown) {
        if (shown & ~MOUSE_HIDE_ALL) return false;
        this.hidden = ~shown & MOUSE_HIDE_ALL;
        buildCommandInterface();
    }

    // A preset's gestures from 15 bytes; null if they aren't ones it can have.
    static readGestures(bytes) {
        const gestures = MOUSE_GESTURE_NAMES.map((_, g) => ({ mods: bytes[3 * g], buttons: bytes[3 * g + 1], act: bytes[3 * g + 2] }));
        return gestures.some(({ mods, buttons, act }, g) => mods & ~MOUSE_MODIFIERS || buttons & ~MOUSE_BUTTONS
            || !uiMouse.acts(g).some(([a]) => a === act)) ? null : gestures;
    }

    // Its settings from a payload; an invalid one keeps them as they are. One saved
    // before presets (MOUSE_LEGACY_LEN) has one preset's gestures: named after the
    // template they match, or "Custom".
    restore(pay, r, len) {
        if (len < 3 || pay[1] < MOUSE_SPEED_MIN || pay[1] > MOUSE_SPEED_MAX || pay[2] < MOUSE_STEP_MIN) return;
        const presets = [];
        let active = 0;
        if (len === MOUSE_LEGACY_LEN) {
            const gestures = uiMouse.readGestures(pay.slice(3, len));
            if (!gestures) return;
            const same = (rows) => rows.every(([m, b, a], g) => gestures[g].mods === m && gestures[g].buttons === b && gestures[g].act === a);
            presets.push({ name: Object.keys(MOUSE_TEMPLATES).find(n => same(MOUSE_TEMPLATES[n])) ?? 'Custom', gestures });
        } else {
            if (len < 5 || pay[4] < 1 || pay[4] > MOUSE_PRESETS_MAX || pay[3] >= pay[4]) return;
            active = pay[3];
            const decoder = new TextDecoder('utf-8', { fatal: true });
            let at = 5;
            for (let k = 0; k < pay[4]; k++) {
                const n = pay[at];
                if (!(n >= 1 && n <= MOUSE_NAME_MAX) || at + 1 + n + 3 * MOUSE_GESTURES > len) return;
                let name;
                try { name = decoder.decode(pay.slice(at + 1, at + 1 + n)); } catch { return; }
                const gestures = uiMouse.readGestures(pay.slice(at + 1 + n, at + 1 + n + 3 * MOUSE_GESTURES));
                if (!gestures) return;
                presets.push({ name, gestures });
                at += 1 + n + 3 * MOUSE_GESTURES;
            }
            if (at !== len) return;
        }
        this.tapClick = !!(pay[0] & MOUSE_TAP_CLICK);
        this.reverse = !!(pay[0] & MOUSE_SCROLL_REVERSE);
        this.hidden = pay[0] & MOUSE_HIDE_ALL;
        this.noThree = !!(pay[0] & MOUSE_NO_THREE);
        this.speed = pay[1];
        this.step = pay[2];
        this.presets = presets;
        this.active = active;
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(bitsSetting('Show', MOUSE_PARTS, () => ~this.hidden & MOUSE_HIDE_ALL,
            (shown) => this.setShown(shown)));
        settings.appendChild(choiceSetting('Tap to click', { on: 'On', off: 'Off' },
            () => this.tapClick ? 'on' : 'off', (v) => { this.tapClick = v === 'on'; }));
        settings.appendChild(choiceSetting('Scrolling', { traditional: 'Traditional', natural: 'Natural' },
            () => this.reverse ? 'natural' : 'traditional', (v) => { this.reverse = v === 'natural'; }));
        settings.appendChild(choiceSetting('Three fingers', { on: 'On', off: 'Off' },
            () => this.noThree ? 'off' : 'on', (v) => this.setThree(v === 'on')));
        settings.appendChild(editableSetting('Pointer speed', () => (this.speed / 10).toFixed(1),
            (v) => this.setSpeed(v), {type: 'number', min: MOUSE_SPEED_MIN / 10, max: MOUSE_SPEED_MAX / 10, step: 0.1}));
        settings.appendChild(editableSetting('Scroll step (px)', () => String(this.step),
            (v) => this.setStep(v), {type: 'number', min: MOUSE_STEP_MIN, max: 255, step: 1}));

        // The presets: the one in use, its name, and adding and deleting them.
        settings.appendChild(selectSetting('Preset', this.presets.map((p, k) => [String(k), p.name]),
            () => String(this.active), (k) => this.setActive(k)));
        settings.appendChild(editableSetting('Preset name', () => this.presets[this.active].name,
            (name) => this.renamePreset(name), {maxlength: MOUSE_NAME_MAX}));
        settings.appendChild(actionsSetting('Presets', [
            ['Copy', () => this.addPreset(), this.presets.length < MOUSE_PRESETS_MAX],
            ...Object.keys(MOUSE_TEMPLATES).map(t => ['+ ' + t, () => this.addPreset(t), this.presets.length < MOUSE_PRESETS_MAX]),
            ['Delete', () => this.deletePreset(), this.presets.length > 1],
        ]));

        // Each gesture of the preset in use: what it does, then the keys it holds, and
        // the buttons it holds (a move) or clicks (a tap). A scroll holds keys only.
        // Without three fingers, their gestures are left out.
        MOUSE_GESTURE_NAMES.forEach(([name], g) => {
            if (this.noThree && uiMouse.threeFinger(g)) return;
            const gesture = this.gestures[g];
            settings.appendChild(selectSetting(name, uiMouse.acts(g), () => gesture.act,
                (act) => { this.setAct(g, act); this.createSettings(); }));
            if (gesture.act === MOUSE_ACT_NONE) return;
            settings.appendChild(bitsSetting('Keys', MOUSE_MODIFIER_NAMES, () => gesture.mods,
                (mods) => this.setMods(g, mods)));
            if (gesture.act !== MOUSE_ACT_SCROLL) {
                settings.appendChild(bitsSetting('Buttons', MOUSE_BUTTON_NAMES, () => gesture.buttons,
                    (buttons) => this.setButtons(g, buttons)));
            }
        });
        settings.appendChild(orderSetting(this));
    }

    marshal() {
        const options = (this.tapClick ? MOUSE_TAP_CLICK : 0) | (this.reverse ? MOUSE_SCROLL_REVERSE : 0)
            | this.hidden | (this.noThree ? MOUSE_NO_THREE : 0);
        const pay = [options, this.speed, this.step, this.active, this.presets.length,
            ...this.presets.flatMap(({ name, gestures }) => {
                const text = [...new TextEncoder().encode(name)];
                return [text.length, ...text, ...gestures.flatMap(({ mods, buttons, act }) =>
                    [mods, act === MOUSE_ACT_SCROLL || act === MOUSE_ACT_NONE ? 0 : buttons, act])];
            })];
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
    // label, a button for each preset, the pad, a scroll strip right of it and one
    // below, and the three buttons. Each but the pad may be hidden (MOUSE_HIDE_), and
    // the pad takes its room. A new mouse shows the pad, the presets and the buttons.
    buildUI(commandInterface) {
        this.letGo();
        const control = document.createElement('div');
        control.className = 'control-mouse';
        control.innerHTML = `<span class="name"></span>
            <div class="mouse">
                <div class="mouse-presets" role="radiogroup" aria-label="Preset"></div>
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

        // The presets' buttons: a touch uses that preset, at once.
        const bar = control.querySelector('.mouse-presets');
        this.presets.forEach((preset, k) => {
            const button = document.createElement('button');
            button.type = 'button';
            button.setAttribute('role', 'radio');
            button.textContent = preset.name;
            button.addEventListener('click', () => this.setActive(k));
            bar.appendChild(button);
        });
        this.showActive = () => [...bar.children].forEach((b, k) => b.setAttribute('aria-checked', String(k === this.active)));
        this.showActive();

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

        // Leave out the hidden parts, and lay out the rest.
        const shows = (bit) => !(this.hidden & bit);
        const parts = { [MOUSE_HIDE_NAME]: '.name', [MOUSE_HIDE_PRESETS]: '.mouse-presets', [MOUSE_HIDE_VSTRIP]: '.vertical',
            [MOUSE_HIDE_HSTRIP]: '.horizontal', [MOUSE_HIDE_BUTTONS]: '.mouse-buttons' };
        // The presets' buttons only with presets to choose between.
        const bars = shows(MOUSE_HIDE_PRESETS) && this.presets.length > 1;
        Object.entries(parts).forEach(([bit, sel]) => {
            if (!shows(Number(bit)) || (Number(bit) === MOUSE_HIDE_PRESETS && !bars)) control.querySelector(sel).remove();
        });
        const v = shows(MOUSE_HIDE_VSTRIP);
        const rows = [];
        if (bars) rows.push([v ? '"p p"' : '"p"', 'auto']);
        rows.push([v ? '"pad v"' : '"pad"', '1fr']);
        if (shows(MOUSE_HIDE_HSTRIP)) rows.push([v ? '"h ."' : '"h"', '2.4em']);
        if (shows(MOUSE_HIDE_BUTTONS)) rows.push([v ? '"b b"' : '"b"', '3em']);
        const grid = control.querySelector('.mouse');
        grid.style.gridTemplateColumns = v ? '1fr 2.4em' : '1fr';
        grid.style.gridTemplateAreas = rows.map(([areas]) => areas).join(' ');
        grid.style.gridTemplateRows = rows.map(([, size]) => size).join(' ');

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
        // Whether two fingers drag or pinch; null until it is clear. A phone reports
        // each finger's moves on its own, so while two fingers drag together, the one
        // that has just moved seems for a moment to have changed the gap between them.
        // So wait until both have moved MOUSE_PINCH_PX: the same way is a drag, opposite
        // ways a pinch. If one has moved three times that and the other hardly at all (a
        // pinch with one finger still, or a drag with one lagging), whichever the gap
        // changed more by: a pinch; else a drag.
        const twoFingers = () => {
            if (!touch.from || ![...fingers.keys()].every(id => touch.from.has(id))) return null;
            const [a, b] = [...fingers].map(([id, [x, y]]) => [x - touch.from.get(id)[0], y - touch.from.get(id)[1]]);
            const la = Math.hypot(...a), lb = Math.hypot(...b);
            if (Math.min(la, lb) >= MOUSE_PINCH_PX)
                return a[0] * b[0] + a[1] * b[1] > 0 ? MOUSE_GESTURE_DRAG2 : MOUSE_GESTURE_PINCH;
            if (Math.max(la, lb) < 3 * MOUSE_PINCH_PX) return null;
            const change = Math.abs(gap() - touch.gap);
            const together = Math.hypot((a[0] + b[0]) / 2, (a[1] + b[1]) / 2);
            return change > together ? MOUSE_GESTURE_PINCH : MOUSE_GESTURE_DRAG2;
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
            if (this.noThree && fingers.size >= 2) return;      // no third finger: it is ignored
            pad.setPointerCapture(e.pointerId);
            fingers.set(e.pointerId, [e.clientX, e.clientY]);
            if (!touch) touch = { most: 0, start: performance.now(), moved: 0, kind: null, gap: 0, from: null };
            touch.most = Math.max(touch.most, fingers.size);
            // Two fingers: where each starts from, to tell a drag from a pinch.
            if (fingers.size === 2 && touch.kind === null) { touch.gap = gap(); touch.from = new Map(fingers); }
            if (fingers.size === 3 && touch.kind !== MOUSE_GESTURE_DRAG3) { end(); begin(MOUSE_GESTURE_DRAG3); }
        });

        pad.addEventListener('pointermove', (e) => {
            const was = fingers.get(e.pointerId);
            if (!was || !touch) return;
            const dx = e.clientX - was[0], dy = e.clientY - was[1];
            fingers.set(e.pointerId, [e.clientX, e.clientY]);
            touch.moved += Math.hypot(dx, dy);
            const n = fingers.size;

            // Two fingers: a drag or a pinch, once it is clear which (see twoFingers). The
            // moves that told them apart count: a pinch from the gap the fingers started
            // at, a drag by how far their middle has gone.
            if (n === 2 && touch.kind === null) {
                const kind = twoFingers();
                if (kind === null) return;
                begin(kind);
                if (kind === MOUSE_GESTURE_DRAG2) {
                    const [a, b] = [...fingers].map(([id, [x, y]]) => [x - touch.from.get(id)[0], y - touch.from.get(id)[1]]);
                    const mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2;
                    const { act } = this.gestures[kind];
                    if (act === MOUSE_ACT_MOVE) this.add(mx * this.speed / 10, my * this.speed / 10);
                    if (act === MOUSE_ACT_SCROLL) this.scroll(mx, my);
                    return;
                }
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

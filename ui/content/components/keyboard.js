// HID modifier bits (the keyboard report's first byte); only the left-hand keys.
const KEYBOARD_MODIFIERS = [
    [0x01, 'Ctrl'],
    [0x02, 'Shift'],
    [0x04, 'Alt'],
    [0x08, 'Win'],      // GUI: Windows, Command on a Mac, Super on Linux
];

// The keys a key combo may press, as [HID usage, name]. 0 presses the modifiers alone.
const KEYBOARD_KEYS = [
    [0x00, 'None'],
    ...Array.from({ length: 26 }, (_, i) => [0x04 + i, String.fromCharCode(65 + i)]),
    ...Array.from({ length: 10 }, (_, i) => [i === 0 ? 0x27 : 0x1D + i, String(i)]),
    [0x28, 'Enter'], [0x29, 'Escape'], [0x2A, 'Backspace'], [0x2B, 'Tab'], [0x2C, 'Space'],
    [0x2D, '-'], [0x2E, '='], [0x2F, '['], [0x30, ']'], [0x31, '\\'], [0x33, ';'],
    [0x34, "'"], [0x35, '`'], [0x36, ','], [0x37, '.'], [0x38, '/'],
    ...Array.from({ length: 12 }, (_, i) => [0x3A + i, 'F' + (i + 1)]),
    ...Array.from({ length: 12 }, (_, i) => [0x68 + i, 'F' + (i + 13)]),
    [0x46, 'Print Screen'], [0x47, 'Scroll Lock'], [0x48, 'Pause'], [0x39, 'Caps Lock'],
    [0x49, 'Insert'], [0x4C, 'Delete'], [0x4A, 'Home'], [0x4D, 'End'],
    [0x4B, 'Page Up'], [0x4E, 'Page Down'],
    [0x52, 'Up'], [0x51, 'Down'], [0x50, 'Left'], [0x4F, 'Right'],
    [0x65, 'Menu'],
];

// Text a text keystroke may type: printable ASCII, tab and newline (Enter).
const keyboardTypeable = (text) => /^[\x20-\x7E\t\n]*$/.test(text);

// In the settings, a text keystroke is one line: Enter is written \n, Tab \t,
// and a backslash \\.
const keyboardEscape = (text) => text.replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\t/g, '\\t');
const keyboardUnescape = (text) => text.replace(/\\([\\nt])/g, (_, c) => ({ n: '\n', t: '\t', '\\': '\\' })[c]);

// A USB keyboard: an output that types a keystroke to the PC the device is
// plugged into, once each time its input (a button) goes on. The keystroke is
// a key with modifiers (Ctrl+Alt+Delete) or a line of text, typed as on a US
// keyboard. The device keeps the keystroke and types it without the browser.
class uiKeyboard extends uiComponents {
    constructor(x = 0, y = 0) {
        super("keyboard", x, y);
        this.label = "Keyboard";

        this.tc = MSG_HW_KEYBOARD;

        this.inputs.push(
            {
                type:"bool",
                conns:[],
                max:1
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.mode = KEYBOARD_COMBO;
        this.modifiers = 0;
        this.key = 0x28;        // Enter
        this.text = 'Hello';

        this.buildGraphElement();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="12" text-anchor="middle" fill="#BFCDE7">KEY</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
            <title></title>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
        this.showStroke();
    }

    // The keystroke in words, e.g. "Ctrl+Alt+Delete" or "Types “Hello”".
    describe() {
        if (this.mode === KEYBOARD_TEXT) return `Types “${keyboardEscape(this.text)}”`;
        const names = KEYBOARD_MODIFIERS.filter(([bit]) => this.modifiers & bit).map(([, name]) => name);
        if (this.key) names.push(KEYBOARD_KEYS.find(([usage]) => usage === this.key)[1]);
        return names.join('+') || 'Nothing';
    }

    // The keystroke shows when pointing at the node.
    showStroke() {
        this.gElm.querySelector('title').textContent = this.describe();
    }

    setMode(mode) {
        mode = Number(mode);
        if (mode !== KEYBOARD_COMBO && mode !== KEYBOARD_TEXT) return false;
        this.mode = mode;
        this.showStroke();
    }

    setModifier(bit, on) {
        this.modifiers = on ? this.modifiers | bit : this.modifiers & ~bit;
        this.showStroke();
    }

    setKey(key) {
        key = Number(key);
        if (!KEYBOARD_KEYS.some(([usage]) => usage === key)) return false;
        this.key = key;
        this.showStroke();
    }

    // 1 to KEYBOARD_TEXT_MAX characters it can type.
    setText(text) {
        if (!text.length || text.length > KEYBOARD_TEXT_MAX || !keyboardTypeable(text)) return false;
        this.text = text;
        this.showStroke();
    }

    // The keystroke from a payload's bytes 2 on; an invalid one keeps the keystroke as it is.
    setStroke(bytes) {
        if (bytes[0] === KEYBOARD_COMBO && bytes.length === 3) {
            if (!KEYBOARD_KEYS.some(([usage]) => usage === bytes[2])) return false;
            this.mode = KEYBOARD_COMBO;
            this.modifiers = bytes[1];
            this.key = bytes[2];
        } else if (bytes[0] === KEYBOARD_TEXT) {
            const text = String.fromCharCode(...bytes.slice(1));
            if (this.setText(text) === false) return false;
            this.mode = KEYBOARD_TEXT;
        } else {
            return false;
        }
        this.showStroke();
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        settings.appendChild(choiceSetting('Types', {[KEYBOARD_COMBO]: 'Key', [KEYBOARD_TEXT]: 'Text'},
            () => String(this.mode), (mode) => { this.setMode(mode); this.createSettings(); }));

        if (this.mode === KEYBOARD_TEXT) {
            settings.appendChild(editableSetting('Text', () => keyboardEscape(this.text),
                (text) => this.setText(keyboardUnescape(text)), {maxlength: 2 * KEYBOARD_TEXT_MAX}));
            const hint = document.createElement('p');
            hint.textContent = `Up to ${KEYBOARD_TEXT_MAX} characters, as on a US keyboard. \\n is Enter, \\t is Tab.`;
            settings.appendChild(hint);
            return;
        }

        // The modifiers, each on or off.
        const row = document.createElement('div');
        row.className = 'editable choice';
        const label = document.createElement('span');
        label.className = 'label';
        label.textContent = 'Hold:';
        row.appendChild(label);
        KEYBOARD_MODIFIERS.forEach(([bit, name]) => {
            const button = document.createElement('button');
            button.type = 'button';
            button.textContent = name;
            button.classList.toggle('sel', !!(this.modifiers & bit));
            button.addEventListener('click', () => {
                this.setModifier(bit, !(this.modifiers & bit));
                button.classList.toggle('sel', !!(this.modifiers & bit));
            });
            row.appendChild(button);
        });
        settings.appendChild(row);

        settings.appendChild(selectSetting('Key', KEYBOARD_KEYS, () => this.key, (key) => this.setKey(key)));
    }

    buildUI(commandInterface) {}

    marshal() {
        const sourceID = this.sourceID();
        const stroke = this.mode === KEYBOARD_TEXT
            ? [KEYBOARD_TEXT, ...Array.from(this.text, c => c.charCodeAt(0))]
            : [KEYBOARD_COMBO, this.modifiers, this.key];
        return [this.tc, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 2 + stroke.length,
            (sourceID >> 8) & 0xFF, sourceID & 0xFF, ...stroke];
    }
}

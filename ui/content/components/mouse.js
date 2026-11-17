// The inputs of a USB mouse, in the order they are saved: [role, setting label, wire type].
const MOUSE_ROLES = [
    ['x', 'X', 'float'],
    ['y', 'Y', 'float'],
    ['left', 'Left', 'bool'],
    ['right', 'Right', 'bool'],
    ['middle', 'Middle', 'bool'],
];

// A USB mouse: an output that moves the pointer of the PC the device is plugged
// into, and presses its buttons. Its X and Y (numbers) are how fast the pointer
// moves each way, up to its top speed: from 0 to 1, still at 0.5 (a 2D slider's,
// the default), or from -1 to 1, still at 0. Its left, right and middle buttons
// (on/off) are pressed while their inputs are on. Each input's source is picked in
// the settings, or a wire drawn in takes the first input of its type with none
// yet. The device moves the pointer without the browser.
class uiMouse extends uiComponents {
    constructor(x = 0, y = 0) {
        super("mouse", x, y);
        this.label = "Mouse";

        this.tc = MSG_HW_MOUSE;

        this.inputs.push(
            {
                type: "float",      // and on/off: see takesType
                conns: [],
                max: MOUSE_INPUTS
            }
        );
        this.actDir = "inputs";
        this.actNum = 0;

        this.roles = MOUSE_ROLES;   // restoreGraph: a source reference for each, in order (see setRole)
        for (const [role] of MOUSE_ROLES) {
            this[role + 'Source'] = null;      // the source node, and which of its outputs
            this[role + 'Output'] = 0;
        }
        this.centred = true;        // X and Y run 0 to 1, still at 0.5; else -1 to 1, still at 0
        this.speed = 800;           // counts a second at full tilt

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
            <title></title>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
        this.showInputs();
    }

    // Its inputs in words, shown when pointing at the node, e.g. "X: 2D slider X (n1) · Left: Button (n3)".
    describe() {
        const wired = MOUSE_ROLES.filter(([role]) => this[role + 'Source']).map(([role, label]) => {
            const node = this[role + 'Source'], out = node.outputs[this[role + 'Output']];
            return `${label}: ${node.outputs.length > 1 ? `${node.label} ${out.name}` : node.label} (${node.name})`;
        });
        return wired.join(' · ') || 'Nothing wired';
    }

    showInputs() {
        this.gElm.querySelector('title').textContent = this.describe();
    }

    // ── Its inputs ──

    static roleType(role) {
        return MOUSE_ROLES.find(([r]) => r === role)[2];
    }

    // The type of an output of a source an input may take: a number or an on/off,
    // from a source whose values reach the device. Not a block's port that takes its
    // type from its first wire: which input it would be is not known. Null if none.
    static sourceType(source, output) {
        const out = source.outputs[output];
        if (!out || out.browserOnly || source.typeFromWire?.()) return null;
        return out.type === 'float' || out.type === 'bool' ? out.type : null;
    }

    // A wire is drawn in only while an input of its type has no source.
    takesType(source, output = source.actNum) {
        const type = uiMouse.sourceType(source, output);
        return !!type && !!this.freeRole(type);
    }

    // Whether an input may take an output of a source node: one of its type.
    roleTakes(role, node, output) {
        return uiMouse.sourceType(node, output) === uiMouse.roleType(role);
    }

    // The first input of a wire type with no source; undefined if none.
    freeRole(type) {
        return MOUSE_ROLES.find(([role, , t]) => t === type && !this[role + 'Source'])?.[0];
    }

    drivenBy(role, node, output) {
        return this[role + 'Source'] === node && this[role + 'Output'] === output;
    }

    // An input from an output of a source node of its type, or from none, and redraw
    // the wires. A source of another type is refused.
    setRole(role, node, output = 0) {
        if (!MOUSE_ROLES.some(([r]) => r === role)) return false;
        if (node && !this.roleTakes(role, node, output)) return false;
        this[role + 'Source'] = node;
        this[role + 'Output'] = node ? output : 0;
        this.syncWires();
        this.showInputs();
    }

    // Make the wires into the mouse match its inputs' sources: one from each.
    syncWires() {
        const refs = MOUSE_ROLES.map(([r]) => r).filter(r => this[r + 'Source'])
            .map(r => ({ node: this[r + 'Source'], output: this[r + 'Output'] }));
        const wired = (c, r) => c.start.node === r.node && c.start.output === r.output;
        this.inConns().filter(c => !refs.some(r => wired(c, r))).forEach(c => c.remove());
        refs.filter((r, i) => !refs.slice(0, i).some(q => wired({ start: q }, r)))
            .filter(r => !this.inConns().some(c => wired(c, r)))
            .forEach(r => new gEdge(r.node, this, r.output));
    }

    // A wire drawn in drives the first input of its type with no source yet.
    addEdge(e) {
        const src = e.start.node, output = e.start.output;
        if (e.end.node === this && !MOUSE_ROLES.some(([r]) => this.drivenBy(r, src, output))) {
            const role = this.freeRole(uiMouse.sourceType(src, output));
            if (role) {
                this[role + 'Source'] = src;
                this[role + 'Output'] = output;
                if (selected === this) this.createSettings();
            }
        }
        super.addEdge(e);
        this.showInputs();
    }

    // A wire taken out takes its source off the inputs it drove.
    edgeRemoved(e) {
        for (const [role] of MOUSE_ROLES) {
            if (!this.drivenBy(role, e.start.node, e.start.output)) continue;
            this[role + 'Source'] = null;
            this[role + 'Output'] = 0;
        }
        if (selected === this) this.createSettings();
        this.showInputs();
        super.edgeRemoved(e);
    }

    // ── Its settings ──

    setRange(range) {
        if (range !== 'centred' && range !== 'signed') return false;
        this.centred = range === 'centred';
    }

    // A whole number of counts a second, MOUSE_SPEED_MIN to MOUSE_SPEED_MAX.
    setSpeed(speed) {
        speed = String(speed).trim();
        if (!/^\d+$/.test(speed) || Number(speed) < MOUSE_SPEED_MIN || Number(speed) > MOUSE_SPEED_MAX) return false;
        this.speed = Number(speed);
    }

    // Its settings from a payload, after its source references; an invalid one keeps them as they are.
    restore(pay, r, len) {
        const at = MOUSE_INPUTS * r;
        if (len !== at + 3 || pay[at] & ~MOUSE_CENTRED) return;
        this.centred = !!(pay[at] & MOUSE_CENTRED);
        this.setSpeed(pay[at + 1] << 8 | pay[at + 2]);
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        const choices = (role) => Object.values(elements).filter(n => n !== this).flatMap(n => n.outputs
            .map((out, i) => [n, out, i]).filter(([n, , i]) => this.roleTakes(role, n, i))
            .map(([n, out, i]) => [`${n.name}:${i}`,
                n.outputs.length > 1 ? `${n.label} ${out.name} (${n.name})` : `${n.label} (${n.name})`]));
        for (const [role, label] of MOUSE_ROLES) {
            const source = this[role + 'Source'];
            settings.appendChild(selectSetting(label, [['', 'None'], ...choices(role)],
                () => source ? `${source.name}:${this[role + 'Output']}` : '',
                (pick) => {
                    const [name, output] = pick.split(':');
                    this.setRole(role, elements[name] ?? null, Number(output));
                    this.createSettings();
                }));
        }
        settings.appendChild(choiceSetting('X and Y', { centred: '0 to 1', signed: '−1 to 1' },
            () => this.centred ? 'centred' : 'signed', (range) => this.setRange(range)));
        settings.appendChild(editableSetting('Top speed (counts/s)', () => String(this.speed),
            (speed) => this.setSpeed(speed)));
    }

    buildUI(commandInterface) {}

    marshal() {
        const refs = MOUSE_ROLES.flatMap(([role]) => refBytes(this[role + 'Source'], this[role + 'Output']));
        const pay = [...refs, this.centred ? MOUSE_CENTRED : 0, (this.speed >> 8) & 0xFF, this.speed & 0xFF];
        return [this.tc, MSG_FLAG_OUTPUT, (this.id >> 8) & 0xFF, this.id & 0xFF, pay.length, ...pay];
    }
}

// Blocks (subroutines): a graph of logic defined once and placed any number of
// times, like a function (see PROTOCOL.md, "Blocks"). A definition (uiSubDef) is a
// graph of its own, edited in its own view, with input and output ports (uiSubIn,
// uiSubOut) where its copies' wires come in and go out. Each copy (uiSub) in the
// main graph runs its own parts: copies of the definition's logic, built again
// whenever the definition changes. The device runs the parts, which the UI saves
// for each copy (MSG_FLAG_PART); a wire from a copy's output reads whatever the
// output port is wired from inside.

const subDefs = new Map();      // definition ID → uiSubDef
const partNodes = new Map();    // a copy's part's ID → the part, for the device's telemetry

// The nodes and wires of a scope (see newScope), whether or not it is in scope.
const scopeElements = (s) => Object.values(s === scope ? elements : s.elements);
const scopeEdges = (s) => s === scope ? gEdges : s.gEdges;

// A block's definition: its name, and its graph of logic and ports.
class uiSubDef {
    constructor(name, id) {
        this.id = id ?? [...Array(255).keys()].map(i => i + 1).find(i => !subDefs.has(i));
        this.name = name;
        this.scope = newScope(this);
        subDefs.set(this.id, this);
    }

    nodes() { return scopeElements(this.scope); }
    inputs() { return this.nodes().filter(n => n instanceof uiSubIn); }
    outputs() { return this.nodes().filter(n => n instanceof uiSubOut); }

    // Whether it keeps a state from scan to scan: some logic in it does (see statelessLoop).
    holdsState() { return this.nodes().some(n => n.holdsState); }

    // Its copies in the main graph.
    copies() { return Object.values(mainElements()).filter(e => e instanceof uiSub && e.def === this); }

    // A name of 1 to 32 bytes. Copies labelled with the old name take the new one.
    rename(name) {
        name = String(name).trim();
        const len = new TextEncoder().encode(name).length;
        if (len < 1 || len > 32) return false;
        const old = this.name;
        this.name = name;
        this.copies().forEach(c => {
            if (c.label === old) c.setLabel(name);
            c.showDef();
        });
        if (editingBlock === this) showBlockBar();
        refreshSubMenu();
    }

    // Its copies take the definition as it is now, in the main graph, whose wires
    // to ports that are gone go.
    update() { withScope(mainScope, () => this.copies().forEach(c => c.build())); }

    // Its name (MSG_SUB_DEF), then each message of its graph wrapped whole in a MSG_SUB_PART.
    marshal() {
        const name = new TextEncoder().encode(this.name);
        const out = [MSG_SUB_DEF, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, name.length, ...name];
        withScope(this.scope, () => Object.values(elements).forEach(elm => {
            for (const m of configMessages(Uint8Array.from(nodeMessages(elm))))
                out.push(MSG_SUB_PART, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, m.bytes.length, ...m.bytes);
        }));
        return out;
    }
}

// A port of a definition: an input, which its graph reads, or an output, which it
// drives. Each is on/off or a number. Its index is its place among the
// definition's ports of its kind, in the order they were added.
class uiSubPort extends uiLogic {
    constructor(type, tc, text, x, y) {
        super(type, tc, text, "", { type: "bool", max: 1 }, x, y);
        this.gElmState.style.display = 'none';
        const n = Object.values(elements).filter(e => e.constructor === this.constructor).length;
        this.setLabel(`${text === 'IN' ? 'In' : 'Out'} ${n}`);
    }

    compute() { return null; }
    show() {}

    // Its place among its definition's ports of its kind.
    index() { return Object.values(elements).filter(e => e.constructor === this.constructor).indexOf(this); }

    // On/off ('bool') or a number ('float'). Its wires go: they may not take the other.
    get portType() { return (this.outputs[0] ?? this.inputs[0]).type; }
    setType(type) {
        if (type !== 'bool' && type !== 'float') return false;
        if (type === this.portType) return;
        [...this.inConns(), ...this.outputs.flatMap(o => o.conns)].forEach(c => c.remove());
        (this.outputs[0] ?? this.inputs[0]).type = type;
        this.floating = type === 'float';
    }

    createSettings() {
        uiComponents.prototype.createSettings.call(this);
        document.getElementById('settings').appendChild(choiceSetting('Type', {bool: 'On/off', float: 'Number'},
            () => this.portType, (t) => this.setType(t)));
    }
}

// An input port: a source in the definition, where a copy's input wire comes in.
// With no wire yet, it takes its type from the first wire drawn from it: on/off
// into a NOT, a number into a compare.
class uiSubIn extends uiSubPort {
    constructor(x = 0, y = 0) {
        super("input", MSG_SUB_INPUT, "IN", x, y);
        this.inputs = [];
    }

    typeFromWire() { return this.outputs[0].conns.length === 0; }

    addEdge(e) {
        if (e.start.node === this && this.typeFromWire()) this.setType(e.end.node.inputs[0].type);
        super.addEdge(e);
        if (selected === this) this.createSettings();
    }

    // Its type, then its index.
    marshal() {
        return this.message([this.portType === 'float' ? SUB_TYPE_FLOAT : SUB_TYPE_BOOL, this.index()]);
    }

    restore(pay, r, len) {
        if (len === 2) this.setType(pay[0] === SUB_TYPE_FLOAT ? 'float' : 'bool');
    }
}

// An output port: an output in the definition, which a copy's output wire reads.
class uiSubOut extends uiSubPort {
    constructor(x = 0, y = 0) {
        super("output", MSG_SUB_OUTPUT, "OUT", x, y);
        this.outputs = [];
    }

    // Its source, its type, then its index.
    marshal() {
        return this.message([...this.sourceRef(), this.portType === 'float' ? SUB_TYPE_FLOAT : SUB_TYPE_BOOL, this.index()]);
    }

    restore(pay, r, len) {
        if (len === r + 2) this.setType(pay[r] === SUB_TYPE_FLOAT ? 'float' : 'bool');
    }
}

// In a copy's parts, an input port: a source whose value is the copy's input's.
// The device reads the copy's input's source itself (deviceRef).
class uiSubInPart extends uiComponents {
    constructor(sub, k, type) {
        idCounter = 0xF000 + k;
        super("input-part", 0, 0);
        this.sub = sub;
        this.k = k;
        this.outputs.push({ type, conns: [] });
        this.actNum = 0;
        this.gElm = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        nodes.appendChild(this.gElm);
        this.last = this.outputValue(0);
    }

    roleConn() { return this.sub.roleConn('in' + this.k); }

    outputValue(output) { return uiLogic.connValue(this.roleConn()); }

    // Hand a change of the copy's input on to the parts.
    refresh() {
        const v = this.outputValue(0);
        if (v === this.last) return;
        this.last = v;
        this.emit(v, 0);
    }

    // It comes after what the copy's input reads (see graphDepth).
    depthSources() { return this.sub.inConns().filter(c => !c.feedback && c === this.roleConn()).map(c => c.start.node); }

    // What a part's reference to it stands for: the copy's input's source, fed back if
    // either wire is.
    deviceRef(output, feedback) {
        const c = this.roleConn();
        return c ? { node: c.start.node, output: c.start.output, feedback: !!(feedback || c.feedback) }
            : { node: null, output: 0, feedback: false };
    }
}

// In a copy's parts, an output port: hands what it is wired from on to the copy's output.
class uiSubOutPart extends uiComponents {
    constructor(sub, k, type) {
        idCounter = 0xF100 + k;
        super("output-part", 0, 0);
        this.sub = sub;
        this.k = k;
        this.inputs.push({ type, conns: [], max: 1 });
        this.actNum = 0;
        this.gElm = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        nodes.appendChild(this.gElm);
    }

    receive(value) { this.sub.outputChanged(this.k); }
    edgeRemoved(e) { this.sub.outputChanged(this.k); }
}

// A copy of a block in the main graph. Its inputs are the definition's input
// ports, told apart in its settings as a latch's are; a wire drawn in takes the
// first free one of its type. Its outputs are the definition's output ports; a
// wire leaves from the one picked in its settings. Double-click it, or "Edit
// block" in its settings, to edit the definition.
class uiSub extends uiRoles {
    constructor(x = 0, y = 0) {
        super("block", MSG_SUB, "", "Block", [], x, y);
        this.outputs = [];
        this.def = null;
        this.base = 0;              // the ID of its first part
        this.partCount = 0;
        this.parts = null;          // its parts' scope
        this.inParts = [];
        this.outParts = [];
        this.outs = [];             // its outputs' values
        this.refsAt = 5;            // in its payload, its inputs' references come after its definition
        this.gElmState.style.display = 'none';
        this.gElm.addEventListener('dblclick', () => this.def && openBlock(this.def));
    }

    // It holds a state if logic in its definition does.
    get holdsState() { return !!this.def?.holdsState(); }
    set holdsState(v) {}

    // A copy of a definition. Restored, it keeps the ID of its first part.
    setDef(def, restored = false) {
        this.def = def;
        if (this.label === 'Block') this.setLabel(def.name);
        this.showDef();
        this.build(restored);
    }

    showDef() {
        this.setText(this.def.name.length > 8 ? this.def.name.slice(0, 7) + '…' : this.def.name);
        if (selected === this) this.createSettings();
    }

    // Its inputs and outputs as the definition's ports. Wires stay on the ports that
    // are still there, with the same type.
    syncPorts() {
        const ins = this.def.inputs(), outs = this.def.outputs();
        const roles = ins.map((p, k) => ['in' + k, p.label, p.portType]);
        for (const [role] of this.roles) {
            const keep = roles.find(([r]) => r === role);
            const node = this[role + 'Source'], output = this[role + 'Output'];
            if (node && (!keep || node.outputs[output]?.type !== keep[2])) this.roleConn(role)?.remove();
        }
        this.roles = roles;
        for (const [role] of roles) this[role + 'Source'] ??= null;
        this.inputs[0].max = roles.length;

        outs.forEach((p, k) => {
            const out = this.outputs[k] ??= { type: p.portType, conns: [] };
            if (out.type !== p.portType) [...out.conns].forEach(c => c.remove());
            out.type = p.portType;
            out.name = p.label;
        });
        this.outputs.slice(outs.length).forEach(o => [...o.conns].forEach(c => c.remove()));
        this.outputs.length = outs.length;
        this.outs.length = outs.length;
        if (this.actNum >= outs.length) this.actNum = 0;
    }

    // Build its parts from the definition: a copy of each of its nodes, the logic
    // with IDs from `base`, and its ports as stand-ins (uiSubInPart, uiSubOutPart).
    // The definition's wires are copied: ordinary ones first, so each feedback wire
    // closes the loop it closes in the definition.
    build(restored = false) {
        this.dropParts();
        const def = this.def;
        this.syncPorts();
        const logic = def.nodes().filter(n => !(n instanceof uiSubPort));
        if (!restored && (!this.base || logic.length > this.partCount)) this.base = takeIds(logic.length);
        this.partCount = logic.length;
        const edges = [...scopeEdges(def.scope)];

        this.parts = newScope();
        this.parts.hidden = true;
        withScope(this.parts, () => {
            const copies = new Map();
            logic.forEach((n, i) => {
                idCounter = this.base + i;
                const c = new n.constructor(n.x, n.y);
                const m = n.marshal();
                c.restore?.(m.slice(MSG_HEADER_LEN), m[1] & MSG_FLAG_OUTPUT ? 3 : 2, m[4]);
                copies.set(n, c);
                partNodes.set(c.id, c);
            });
            this.inParts = def.inputs().map((p, k) => { const c = new uiSubInPart(this, k, p.portType); copies.set(p, c); return c; });
            this.outParts = def.outputs().map((p, k) => { const c = new uiSubOutPart(this, k, p.portType); copies.set(p, c); return c; });
            for (const e of [...edges.filter(e => !e.feedback), ...edges.filter(e => e.feedback)]) {
                const from = copies.get(e.start.node), to = copies.get(e.end.node), end = e.end.node;
                const role = end.roles?.find(([r]) => end.drivenBy(r, e.start.node, e.start.output))?.[0];
                if (role) to.setRole(role, from, e.start.output);
                else new gEdge(from, to, e.start.output);
            }
        });
        this.outParts.forEach((p, k) => this.outputChanged(k));
    }

    // Take its parts out, with their timers.
    dropParts() {
        if (!this.parts) return;
        withScope(this.parts, () => Object.values(elements).forEach(n => {
            partNodes.delete(n.id);
            n.remove();
        }));
        this.parts = null;
        this.inParts = [];
        this.outParts = [];
    }

    remove() {
        this.dropParts();
        super.remove();
    }

    // Its inputs changed: hand them on to its parts.
    evaluate() { this.inParts.forEach(p => p.refresh()); }
    show() {}

    // An output port's source changed: hand its value on from that output.
    outputChanged(k) {
        const v = uiLogic.connValue(this.outParts[k]?.inConns()[0]);
        if (v === this.outs[k]) return;
        this.outs[k] = v;
        this.emit(v, k);
    }

    outputValue(output) { return this.outs[output] ?? null; }

    // It comes after its parts (see graphDepth).
    depthSources() { return this.outParts; }

    // A source wired into it drives the first input of its type with no source yet,
    // else the last of its type, whose wire goes.
    takesType(source, output = source.actNum) {
        const out = source.outputs[output];
        return !!out && !out.browserOnly && this.roles.some(([, , type]) => type === out.type);
    }
    roleTakes(role, node, output) {
        return node.outputs[output]?.type === this.roles.find(([r]) => r === role)?.[2];
    }
    setRole(role, node, output = 0) {
        if (node && !this.roleTakes(role, node, output)) return false;
        return super.setRole(role, node, output);
    }
    addEdge(e) {
        const src = e.start.node, output = e.start.output;
        let old = null;
        if (e.end.node === this && !this.roles.some(([r]) => this.drivenBy(r, src, output))) {
            const fits = this.roles.filter(([r]) => this.roleTakes(r, src, output));
            const role = (fits.find(([r]) => !this[r + 'Source']) ?? fits.at(-1))?.[0];
            if (role) {
                old = this.roleConn(role);
                this[role + 'Source'] = src;
                this[role + 'Output'] = output;
            }
        }
        uiLogic.prototype.addEdge.call(this, e);
        old?.remove();
        if (selected === this) this.createSettings();
    }

    // The output new wires leave from.
    setWireOutput(output) {
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    createSettings() {
        uiComponents.prototype.createSettings.call(this);
        const settings = document.getElementById('settings');
        if (!this.def) return;
        settings.appendChild(editableSetting('Block', () => this.def.name, (v) => this.def.rename(v)));
        settings.appendChild(choiceSetting('Definition', {edit: 'Edit block'}, () => '', () => openBlock(this.def)));
        if (this.outputs.length > 1) {
            settings.appendChild(choiceSetting('Wire from', Object.fromEntries(this.outputs.map((o, k) => [k, o.name])),
                () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        }
        this.logicSettings(settings);
    }

    // Its definition, the ID of its first part, how many inputs and outputs, its
    // inputs' sources, then what each output stands for: its output port's source,
    // as the device reads it (see uiSubInPart.deviceRef).
    marshal() {
        const outs = this.outParts.flatMap(p => connRef(p.inConns()[0]));
        return this.message([this.def?.id ?? 0, (this.base >> 8) & 0xFF, this.base & 0xFF,
            this.roles.length, this.outputs.length, ...this.roleRefs(), ...outs]);
    }

    // Its definition, from a saved payload; its inputs are wired after.
    restore(pay, r, len) {
        const def = subDefs.get(pay[0]);
        if (r !== 3 || len < 5 || !def) return;
        this.base = pay[1] << 8 | pay[2];
        this.setDef(def, true);
    }

    // Its parts' messages, for the device: each part's control message, marked
    // MSG_FLAG_PART, without a tile.
    partMessages() {
        if (!this.parts) return [];
        return withScope(this.parts, () => Object.values(elements)
            .filter(n => !(n instanceof uiSubInPart || n instanceof uiSubOutPart))
            .flatMap(n => {
                const m = n.marshal();
                m[1] = (m[1] | MSG_FLAG_PART) & ~MSG_FLAG_SHOWN;
                return m;
            }));
    }
}

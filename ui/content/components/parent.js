const ICON_EDIT = "M15.4998 5.49994L18.3282 8.32837M3 20.9997L3.04745 20.6675C3.21536 19.4922 3.29932 18.9045 3.49029 18.3558C3.65975 17.8689 3.89124 17.4059 4.17906 16.9783C4.50341 16.4963 4.92319 16.0765 5.76274 15.237L17.4107 3.58896C18.1918 2.80791 19.4581 2.80791 20.2392 3.58896C21.0202 4.37001 21.0202 5.63634 20.2392 6.41739L8.37744 18.2791C7.61579 19.0408 7.23497 19.4216 6.8012 19.7244C6.41618 19.9932 6.00093 20.2159 5.56398 20.3879C5.07171 20.5817 4.54375 20.6882 3.48793 20.9012L3 20.9997Z";
const ICON_CONFIRM = "M4 12.6111L8.92308 17.5L20 6.5";
const ICON_CANCEL = "M6 6L18 18M18 6L6 18";

function settingIcon(path, title, onClick) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('fill', 'none');
    svg.innerHTML = `<title>${title}</title><path stroke="#BFCDE7" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="${path}"/>`;
    svg.addEventListener('click', onClick);
    return svg;
}

// An editable settings field, like the ones on the Network view: shows the value
// with an edit icon; editing shows an input with confirm and cancel. Enter
// confirms, Escape cancels. `get` reads the value, `set` stores a new one and may
// return false to reject it, which keeps the input open and outlines it in red. `attrs` are set on the
// input (e.g. {type: 'number', min: 0}).
function editableSetting(label, get, set, attrs = {}) {
    const row = document.createElement('div');
    row.className = 'editable';

    const showValue = () => {
        const value = document.createElement('span');
        value.className = 'value';
        value.textContent = get();
        row.replaceChildren(labelSpan, value, settingIcon(ICON_EDIT, 'Edit', showInput));
    };

    const showInput = () => {
        const input = document.createElement('input');
        input.type = 'text';
        Object.entries(attrs).forEach(([k, v]) => input.setAttribute(k, v));
        input.value = get();
        const confirm = () => {
            if (set(input.value) === false) {
                input.setAttribute('aria-invalid', 'true');
                input.focus();
                input.select();
                return;
            }
            showValue();
        };
        input.addEventListener('input', () => input.removeAttribute('aria-invalid'));
        input.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') confirm();
            if (e.key === 'Escape') showValue();
        });
        row.replaceChildren(labelSpan, input,
            settingIcon(ICON_CONFIRM, 'Save', confirm),
            settingIcon(ICON_CANCEL, 'Cancel', showValue));
        input.focus();
        input.select();
    };

    const labelSpan = document.createElement('span');
    labelSpan.className = 'label';
    labelSpan.textContent = label + ':';

    showValue();
    return row;
}

// A settings field that picks one of a few options, shown side by side; the
// current one is marked. `options` maps each value to its text; `set` stores
// the picked value.
function choiceSetting(label, options, get, set) {
    const row = document.createElement('div');
    row.className = 'editable choice';

    const labelSpan = document.createElement('span');
    labelSpan.className = 'label';
    labelSpan.textContent = label + ':';
    row.appendChild(labelSpan);

    const buttons = Object.entries(options).map(([value, text]) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = text;
        button.dataset.value = value;
        button.addEventListener('click', () => {
            set(value);
            buttons.forEach(b => b.classList.toggle('sel', b.dataset.value === get()));
        });
        row.appendChild(button);
        return button;
    });
    buttons.forEach(b => b.classList.toggle('sel', b.dataset.value === get()));
    return row;
}

// A settings field that picks one of a list: `options` is a list of
// [value, text] pairs; `set` stores the picked value.
function selectSetting(label, options, get, set) {
    const row = document.createElement('div');
    row.className = 'editable select';

    const labelSpan = document.createElement('span');
    labelSpan.className = 'label';
    labelSpan.textContent = label + ':';

    const select = document.createElement('select');
    options.forEach(([value, text]) => select.add(new Option(text, value)));
    select.value = get();
    select.addEventListener('change', () => set(select.value));

    row.append(labelSpan, select);
    return row;
}

// A GPIO pin a GPO or PWM may use (see PIN_USABLE_MASK), from a number or its
// text; null if it is not one.
function parsePin(pin) {
    pin = String(pin).trim();
    return /^\d+$/.test(pin) && pinUsable(Number(pin)) ? Number(pin) : null;
}

// The GPIO pins the graph's nodes use (see usedPins), but `node`'s own.
function pinsInUse(node) {
    return new Set(Object.values(elements).filter(e => e !== node).flatMap(e => e.usedPins()));
}

// A usable pin no node but `node` uses: `preferred` if it is free, else the
// lowest free one (`preferred` if none is).
function freePin(node, preferred) {
    const used = pinsInUse(node);
    if (!used.has(preferred)) return preferred;
    return USABLE_PINS.find(pin => !used.has(pin)) ?? preferred;
}

// A settings field that picks a GPIO pin for `node` from the usable ones that
// no other node uses. Its own pin is always offered.
function pinSetting(node, get, set) {
    const used = pinsInUse(node);
    const pins = USABLE_PINS.filter(pin => pin === get() || !used.has(pin));
    return selectSetting('GPIO pin', pins.map(pin => [pin, `GPIO ${pin}`]), get, set);
}

// A source reference as saved (see MSG_FLAG_OUTPUT): the source node's ID, then
// which of its outputs. ID 0 and output 0 if there is no source.
function refBytes(node, output = 0) {
    const id = node?.id ?? 0;
    return [(id >> 8) & 0xFF, id & 0xFF, node ? output : 0];
}

// The source reference at byte i of a message's payload, as {node, output}:
// three bytes in a message with MSG_FLAG_OUTPUT, otherwise two, the node's ID,
// and output 0. The node is null if it is not in the graph (or ID 0).
function refAt(m, i) {
    const len = m.flags & MSG_FLAG_OUTPUT ? 3 : 2;
    if (m.len < i + len) return { node: null, output: 0 };
    return {
        node: elements['n' + (m.pay[i] << 8 | m.pay[i + 1])] ?? null,
        output: len === 3 ? m.pay[i + 2] : 0,
    };
}

// The length of a source reference in a message: see refAt.
const refLen = (m) => m.flags & MSG_FLAG_OUTPUT ? 3 : 2;

class uiComponents {
    constructor(type,x, y) {
        this.type = type;
        this.x = x;
        this.y = y;
        this.r = 0;
        this.id = idCounter++;
        this.name = 'n' + this.id;
        this.label = type.toUpperCase();
        this.labelId = 0;
        this.inputs = [];
        this.outputs = [];
        this.actDir = null;
        this.actNum = null;
        this.marked = false;

        this.socket = socket;

        elements[this.name] = this;

        this.clearSettings();
    }

    // Whether a wire can join this node and `other`: one must be a source (an
    // input such as a button, which feeds any number of outputs) and the other
    // an output with room for another wire (most take one, a PWM two), not
    // already wired from that output of the source, that takes it (see
    // takesType). A wire leaves a source from its active output (actNum): a 2D
    // slider's X or Y, picked in its settings.
    canTakeConn(other) {
        if (other === this || other.actDir === this.actDir) return false;
        const [output, source] = this.actDir === 'inputs' ? [this, other] : [other, this];
        const input = output.inputs[output.actNum];
        return output.takesType(source)
            && input.conns.length < input.max
            && !input.conns.some(c => c.start.node === source && c.start.output === source.actNum);
    }

    // Whether this output takes output `output` of `source` (its active one by
    // default). Their types must match: a boolean source (a button) drives a
    // boolean output (an LED or GPO), and a floating one (a slider, the core
    // temperature) a floating output (a PWM or a plot). A source whose values
    // only reach the browser (the core temperature) drives only UI outputs: the
    // device never has its values.
    takesType(source, output = source.actNum) {
        if (source.actDir !== 'outputs') return false;
        const out = source.outputs[output];
        return !!out && out.type === this.inputs[this.actNum].type && !(out.browserOnly && this.tc & MSG_HW_BIT);
    }

    // The wires into this node.
    inConns() {
        return this.inputs.flatMap(input => input.conns);
    }

    // The ID of the node wired into this one, 0 if none (an output's single source).
    sourceID() {
        return this.inConns()[0]?.start.node.id ?? 0;
    }

    // The source reference of the wire into this one (an output's single
    // source), as saved: the source's ID, then its output (see MSG_FLAG_OUTPUT).
    // ID 0 if none.
    sourceRef() {
        const conn = this.inConns()[0];
        return refBytes(conn?.start.node, conn?.start.output);
    }

    // The GPIO pins this node takes, which no other node may use: none for most.
    usedPins() {
        return [];
    }

    // The value of one of this source's outputs: most have one, `value`.
    outputValue(output) {
        return this.value;
    }

    setCoords(x = 0, y = 0) {
        this.x = x;
        this.y = y;
    }

    coords() {return [this.x, this.y]}

    setName(name) {
        this.name = name;
        this.id = name;
    }

    setLabel(label) {
        label = label.trim();
        if (!label) return;
        // A label is saved as one message, whose payload is at most 255 bytes.
        if (new TextEncoder().encode(label).length > 255) return false;
        this.label = label;
        this.gElm.children[2].textContent = label;
    }

    highlight(s) {
        switch (s) {
            case 'select':
                this.marked = false;
                this.gElmHighlight.style.display = 'block';
                this.gElmHighlight.setAttribute('stroke','#BFCDE7');
                this.createSettings();

                this.outputs.forEach(output => {
                    output.conns.forEach(conn => {
                        conn.start.handleDisplay(true);
                    });
                });
                this.inConns().forEach(conn => conn.end.handleDisplay(true));
                break;

            case 'mark':
                this.marked = true;
                this.gElmHighlight.style.display = 'block';
                this.gElmHighlight.setAttribute('stroke','#4CAF50');
                break;
        
            default:
                this.marked = false;
                this.gElmHighlight.style.display = 'none';

                this.outputs.forEach(output => {
                    output.conns.forEach(conn => {
                        conn.start.handleDisplay(false);
                    });
                });
                this.inConns().forEach(conn => conn.end.handleDisplay(false));
                break;
        }
    }

    // Send this input's value to the device as a one-message command. The value
    // is a fraction from 0 to 1, sent as one byte from 0 to VALUE_FULL.
    sendValue(value) {
        this.sendValues([value]);
    }

    // Send the values of all this input's outputs as a one-message command: one
    // byte each, in output order.
    sendValues(values) {
        const bytes = values.map(v => Math.round(Math.max(0, Math.min(1, v)) * VALUE_FULL));
        const cmdPkt = Uint8Array.from([
            PKT_COMMAND, 0, MSG_HEADER_LEN + bytes.length,
            this.tc, 0, (this.id >> 8) & 0xFF, this.id & 0xFF, bytes.length, ...bytes,
        ]);

        console.log(cmdPkt) //debug
        socket.send(cmdPkt); //online

        values.forEach((value, output) => this.emit(value, output));
    }

    // Hand a new value of one of this source's outputs to the nodes wired to it.
    // UI outputs (a plot) show it in the browser; hardware outputs ignore it, as
    // the device drives them. The value may be any number, not only 0 to 1.
    emit(value, output = 0) {
        this.outputs[output].conns.forEach(conn => conn.end.node.receive(value));
    }

    // A new value from the source wired into this node. Only UI outputs show it:
    // hardware outputs get it on the device.
    receive(value) {}

    // The label as a MSG_STRING message keyed by this node's ID (see PROTOCOL.md).
    marshalLabel() {
        const text = new TextEncoder().encode(this.label);
        return [MSG_STRING, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, text.length, ...text];
    }

    // The position as a MSG_LAYOUT message keyed by this node's ID: x and y as
    // signed 16-bit big-endian SVG units (see PROTOCOL.md).
    marshalLayout() {
        const int16 = (v) => {
            const n = Math.max(-32768, Math.min(32767, Math.round(v))) & 0xFFFF;
            return [(n >> 8) & 0xFF, n & 0xFF];
        };
        return [MSG_LAYOUT, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 4, ...int16(this.x), ...int16(this.y)];
    }

    // The shape of each wire into this node, as a MSG_WIRE message keyed by this
    // node's ID: the handle angle at the source end, then at this end, each in
    // 1/65536 of a turn (see PROTOCOL.md). A node that takes more than one wire
    // adds the source reference, to tell its wires apart. Nothing if no wire comes in.
    marshalWire() {
        const turn = (angle) => {
            const n = Math.round(angle / (2 * Math.PI) * 65536) & 0xFFFF;
            return [(n >> 8) & 0xFF, n & 0xFF];
        };
        const several = this.inputs.some(input => input.max > 1);
        return this.inConns().flatMap(conn => {
            const shape = [...turn(conn.start.angle), ...turn(conn.end.angle)];
            if (several) shape.push(...refBytes(conn.start.node, conn.start.output));
            return [MSG_WIRE, several ? MSG_FLAG_OUTPUT : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF,
                shape.length, ...shape];
        });
    }

    clearSettings() {
        const settings = document.getElementById('settings');
        settings.innerHTML = "<h3>Settings</h3><p>No node selected</p>";
    }

    createSettings() {
        const settings = document.getElementById('settings');
        settings.innerHTML = `<h3>${this.type.toUpperCase()} ${this.name}</h3>`;
        settings.appendChild(editableSetting('Label', () => this.label, (label) => this.setLabel(label)));
    }

    // Take the node out of the graph, with every wire to or from it.
    remove() {
        this.outputs.forEach(output => [...output.conns].forEach(conn => conn.remove()));
        this.inConns().forEach(conn => conn.remove());
        this.gElm.remove();
        delete elements[this.name];
    }

    // A wire from or to this node: from one of its outputs, or into its input.
    addEdge(e) {
        if (e.start.node === this) this.outputs[e.start.output].conns.push(e);
        else this.inputs[this.actNum].conns.push(e);
    }

    // A wire into this node was taken out.
    edgeRemoved(e) {}

    nodePointerDown(e) {
        e.stopPropagation();
        selectById(this.id);
        this.dragging = true;
        this.draggingStart = svgPoint(e);
        this.draggingOrig = { x: this.x, y: this.y };

        this._onNodePointerMove = (e) => this.nodePointerMove(e);
        this._onNodePointerUp = (e) => this.nodePointerUp(e);

        this.gElm.addEventListener('pointermove', this._onNodePointerMove);
        this.gElm.addEventListener('pointerup', this._onNodePointerUp);
    }

    nodePointerMove(e) {
        if (!this.dragging) return;
        const p = svgPoint(e);
        const dx = p.x - this.draggingStart.x, dy = p.y - this.draggingStart.y;
        this.x = this.draggingOrig.x + dx;
        this.y = this.draggingOrig.y + dy;
        this.gElm.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        this.outputs.forEach(output => {
            output.conns.forEach(conn => {
                conn.moveStart();
            });
        });
        this.inConns().forEach(conn => conn.moveEnd());
    }

    nodePointerUp(e) {
        if (this.dragging) {
            this.gElm.removeEventListener('pointermove', this._onNodePointerMove);
            this.gElm.removeEventListener('pointerup', this._onNodePointerUp);
            delete this._onNodePointerMove;
            delete this._onNodePointerUp;
        }
        this.dragging = false;
    }
}
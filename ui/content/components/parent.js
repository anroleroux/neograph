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

    canTakeConn(connType){
        return true;
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
                this.inputs.forEach(input => {
                    if (input.conn) {
                        input.conn.end.handleDisplay(true);
                    }
                });
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
                this.inputs.forEach(input => {
                    if (input.conn) {
                        input.conn.end.handleDisplay(false);
                    }
                });
                break;
        }
    }

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

    // The shape of the wire into this node, as a MSG_WIRE message keyed by this
    // node's ID: the handle angle at the source end, then at this end, each in
    // 1/65536 of a turn (see PROTOCOL.md). Nothing if no wire comes in.
    marshalWire() {
        const conn = this.inputs[0]?.conn;
        if (!conn) return [];
        const turn = (angle) => {
            const n = Math.round(angle / (2 * Math.PI) * 65536) & 0xFFFF;
            return [(n >> 8) & 0xFF, n & 0xFF];
        };
        return [MSG_WIRE, 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 4, ...turn(conn.start.angle), ...turn(conn.end.angle)];
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

    addEdge(e) {
        if (this.actDir === 'outputs') {
            this[this.actDir][this.actNum].conns.push(e);
        } else {
            this[this.actDir][this.actNum].conn = e;
        }
    }

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
        this.inputs.forEach(input => {
            if (input.conn) {
                input.conn.moveEnd();
            }
        });
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
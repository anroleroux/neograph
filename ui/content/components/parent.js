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
        this.label = label;
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

    clearSettings() {
        const settings = document.getElementById('settings');
        settings.innerHTML = "<h3>Settings</h3><p>No node selected</p>";
    }

    createSettings() {
        const settings = document.getElementById('settings');
        settings.innerHTML = `
            <h3>Settings</h3>
            <label>Label: <input type="text" id="label-input" value="${this.label}"></label>
        `;
        document.getElementById('label-input').addEventListener('change', (e) => {
            this.label = e.target.value;
        });
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
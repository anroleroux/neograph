// A wire from an output of a source node (start) into an output node (end).
// `output` is which of the source's outputs, by default its active one.
class gEdge {
    constructor(n1, n2, output) {
        this.id = idEdgeCounter++;
        this.name = 'n' + this.id;
        this.start = {};
        this.end = {};

        if (n1.actDir === "inputs") {
            [n1,n2] = [n2,n1];
        }

        // An output with no room for another wire drops its oldest: on an output
        // that takes one wire, a new one replaces the old.
        const inp = n2.inputs[n2.actNum];
        if (inp.conns.length >= inp.max) inp.conns[0].remove();

        this.start.node = n1;
        this.start.output = output ?? n1.actNum;
        this.start.mvfunc = () => this.moveStart();
        this.start.handleDisplay = (display) => this.sHandle.classList.toggle('hide', !display);
        this.end.node = n2;
        this.end.mvfunc = () => this.moveEnd();
        this.end.handleDisplay = (display) => this.eHandle.classList.toggle('hide', !display);

        const dx = n2.x - n1.x;
        const dy = n2.y - n1.y;
        this.start.angle = Math.atan2(-dy,dx);
        this.end.angle = this.start.angle+Math.PI;

        this.elm = this.drawEdge();

        n1.addEdge(this);
        n2.addEdge(this);

        gEdges.push(this);
    }

    drawEdge() {
        const nodes = document.getElementById('nodes');

        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('edge');
        g.dataset.id = 'e'+this.id;
        g.dataset.type = 'wire';

        const [x1, y1, xh1, yh1] = this.wireCoords(this.start);
        const [x2, y2, xh2, yh2] = this.wireCoords(this.end);

        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        path.setAttribute('d', `M ${x1},${y1} C ${xh1},${yh1} ${xh2},${yh2} ${x2},${y2}`);
        path.setAttribute('stroke', '#BFCDE7');
        path.setAttribute('stroke-width', 1.8);
        path.setAttribute('fill', 'none');
        g.appendChild(path);
        this.path = path;

        // A wide, invisible copy of the wire, easier to click or tap than the wire.
        const hit = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        hit.setAttribute('d', path.getAttribute('d'));
        hit.setAttribute('stroke', 'transparent');
        hit.setAttribute('stroke-width', 16);
        hit.setAttribute('fill', 'none');
        hit.addEventListener('pointerdown', (e) => {
            e.stopPropagation();
            selectEdge(this);
        });
        g.appendChild(hit);
        this.hit = hit;

        // A source with several outputs names the one the wire leaves from.
        if (this.start.node.outputs.length > 1) {
            const name = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            name.setAttribute('font-size', 10);
            name.setAttribute('text-anchor', 'middle');
            name.setAttribute('dominant-baseline', 'central');
            name.setAttribute('fill', '#BFCDE7');
            name.textContent = this.start.node.outputs[this.start.output].name;
            g.appendChild(name);
            this.outputName = name;
            this.placeOutputName();
        }

        const sHandle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
        sHandle.setAttribute('cx', `${xh1}`);
        sHandle.setAttribute('cy', `${yh1}`);
        sHandle.setAttribute('r', `8`);
        sHandle.setAttribute('stroke', '#BFCDE7');
        sHandle.setAttribute('stroke-width', 0.8);
        sHandle.setAttribute('fill', '#BFCDE711');
        sHandle.classList.add('hide');
        sHandle.addEventListener('pointerdown', (e) => this.handlePointerDown(e,this.start));
        g.appendChild(sHandle);
        this.sHandle = sHandle;
        
        const eHandle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
        eHandle.setAttribute('cx', `${xh2}`);
        eHandle.setAttribute('cy', `${yh2}`);
        eHandle.setAttribute('r', `8`);
        eHandle.setAttribute('stroke', '#BFCDE7');
        eHandle.setAttribute('stroke-width', 0.8);
        eHandle.setAttribute('fill', '#BFCDE711');
        eHandle.classList.add('hide');
        eHandle.addEventListener('pointerdown', (e) => this.handlePointerDown(e,this.end));
        g.appendChild(eHandle);
        this.eHandle = eHandle;

        nodes.insertBefore(g,nodes.children[0]);
        return g;
    }

    // Beside the wire, just off the source.
    placeOutputName() {
        const [nx, ny] = this.start.node.coords();
        const a = this.start.angle;
        this.outputName.setAttribute('x', Math.round(10 * (nx + 60 * Math.cos(a) - 12 * Math.sin(a))) / 10);
        this.outputName.setAttribute('y', Math.round(10 * (ny - 60 * Math.sin(a) - 12 * Math.cos(a))) / 10);
    }

    // Show the wire as selected, with both handles, or not.
    highlight(on) {
        this.path.setAttribute('stroke', on ? '#4CAF50' : '#BFCDE7');
        this.start.handleDisplay(on);
        this.end.handleDisplay(on);
    }

    // Take the wire out of the graph: off both nodes, and off the page.
    remove() {
        const out = this.start.node.outputs[this.start.output];
        out.conns = out.conns.filter(c => c !== this);
        const inp = this.end.node.inputs[this.end.node.actNum];
        inp.conns = inp.conns.filter(c => c !== this);
        gEdges = gEdges.filter(e => e !== this);
        this.elm.remove();
        this.end.node.edgeRemoved(this);
    }

    setPath(d) {
        this.path.setAttribute('d', d);
        this.hit.setAttribute('d', d);
    }

    moveStart() {
        const d = this.path.getAttribute('d');
        const match = d.match(/C\s[-\d.]+,[-\d.]+\s([-\d.]+),([-\d.]+)\s([-\d.]+),([-\d.]+)/);
        if (!match) return;
        const [x, y, xh, yh] = this.wireCoords(this.start);
        this.setPath(`M ${x},${y} C ${xh},${yh} ${match[1]},${match[2]} ${match[3]},${match[4]}`);
        this.sHandle.setAttribute('cx', `${xh}`);
        this.sHandle.setAttribute('cy', `${yh}`);
        if (this.outputName) this.placeOutputName();
    }
    
    moveEnd() {
        const d = this.path.getAttribute('d');
        const match = d.match(/M\s([-\d.]+),([-\d.]+)\sC\s([-\d.]+),([-\d.]+)/);
        if (!match) return;
        const [x, y, xh, yh] = this.wireCoords(this.end);
        this.setPath(`M ${match[1]},${match[2]} C ${match[3]},${match[4]} ${xh},${yh} ${x},${y}`);
        this.eHandle.setAttribute('cx', `${xh}`);
        this.eHandle.setAttribute('cy', `${yh}`);
    }

    wireCoords(n) {
        const [nx,ny] = n.node.coords();

        return [
            Math.round(10*(nx + 50*Math.cos(n.angle)))/10,
            Math.round(10*(ny - 50*Math.sin(n.angle)))/10,
            Math.round(10*(nx + 100*Math.cos(n.angle)))/10,
            Math.round(10*(ny - 100*Math.sin(n.angle)))/10
        ]
    }

    handlePointerDown(e,n) {
        e.stopPropagation();

        this.dragging = true;
        const c = n.node.coords();
        const p = svgPoint(e);
        const [dx,dy] = [p.x-c[0], p.y-c[1]];
        this.draggingStart = Math.atan2(-dy,dx);
        this.draggingOrig = n.angle;

        this._onPointerMove = (e) => this.handlePointerMove(e, n);
        this._onPointerUp = (e) => this.handlePointerUp(e, n);

        const svgElm = document.getElementById('svg');
        svgElm.addEventListener('pointermove', this._onPointerMove);
        svgElm.addEventListener('pointerup', this._onPointerUp);
    }

    handlePointerMove(e,n) {
        if (!this.dragging) return;
        const c = n.node.coords();
        const p = svgPoint(e);
        const [dx,dy] = [p.x-c[0], p.y-c[1]];
        n.angle = Math.atan2(-dy,dx);
        n.mvfunc();
    }

    handlePointerUp(e, n) {
        const svgElm = document.getElementById('svg');

        if (this.dragging) {
            svgElm.removeEventListener('pointermove', this._onPointerMove);
            svgElm.removeEventListener('pointerup', this._onPointerUp);
            delete this._onPointerMove;
            delete this._onPointerUp;
        }
        this.dragging = false;
    }
}
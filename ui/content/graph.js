const svg = document.getElementById('svg');
const panzoom = document.getElementById('panzoom');
const nodes = document.getElementById('nodes');
const overlay = document.getElementById('overlay');

let viewBox = { x: -400, y: -300, w: 1600, h: 900 };
function setView() { svg.setAttribute('viewBox', [viewBox.x, viewBox.y, viewBox.w, viewBox.h].join(' ')); }
setView();

let elements = {}; // {id,type,x,y,r,label,value}
let gEdges = [];
let idCounter = 1;
let idEdgeCounter = 1;
let selected = null;
let wireMode = false;

function saveGraph() {
    commandInterface = document.getElementById('command');
    commandInterface.innerHTML = '';

    const output = [];

    Object.entries(elements).forEach(([key,elm]) => {
        elm.buildUI(commandInterface);
        output.push(...elm.marshal());
    })

    const binary = Uint8Array.from(output);
    const wsConfigMsg = new Uint8Array(3 + gfig.length + binary.length);
    wsConfigMsg[0] = 0x0A;
    wsConfigMsg[1] = (gfig.length + binary.length >> 8) & 0xFF;
    wsConfigMsg[2] = gfig.length + binary.length & 0xFF;
    wsConfigMsg.set(gfig, 3);
    wsConfigMsg.set(binary, 3 + gfig.length);

    console.log(wsConfigMsg) //testing
    if (!testing) { //testing
    socket.send(wsConfigMsg);
    } //testing
}

function toggleConnectMode() {
    wireButton = document.getElementById('wire');
    
    if (wireMode) {
        wireMode = false;
        wireButton.classList.remove('active')
    } else {
        wireButton.classList.add('active')
        wireMode = true;
    }

    if (selected) {
        highlightSelection();
    }
}

function createNode(id,obj) {
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    g.classList.add('node');
    g.setAttribute('transform', `translate(${obj.x} ${obj.y}) rotate(${obj.r || 0})`);
    g.dataset.id = id;
    g.dataset.type = obj.type;

    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', '#sym-' + obj.type);
    g.appendChild(use);
    const label = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    label.setAttribute('x', 0);
    label.setAttribute('y', 36);
    label.setAttribute('font-size', 10);
    label.setAttribute('text-anchor', 'middle');
    label.setAttribute('fill', '#9fbcd9');
    label.textContent = obj.name || '';
    g.appendChild(label);

    // selection bubble
    const sel = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    sel.setAttribute('x', -60);
    sel.setAttribute('y', -40);
    sel.setAttribute('width', 120);
    sel.setAttribute('height', 80);
    sel.setAttribute('rx', 10);
    sel.setAttribute('fill', 'none');
    sel.setAttribute('stroke', '#7cc4ff');
    sel.setAttribute('stroke-width', 0.8);
    sel.style.display = 'none';
    g.appendChild(sel);

    // pointer interactions
    g.addEventListener('pointerdown', nodePointerDown);
    nodes.appendChild(g);
    return g;
}

function createEdge(id1,id2) {
    let el1 = elements[id1];
    let el2 = elements[id2];

    new gEdge(el1, el2);
}

// render nodes from elements
function render() {
    nodes.innerHTML = '';

    Object.entries(elements).forEach(([key,elm]) => {
        const n = createNode(key,elm);
        const txt = n.querySelector('text');
        txt.textContent = elm.label || '';
    })
}

// selection management
function selectById(id) {
    if (elements['n'+id].marked) {
        createEdge(selected.name, 'n'+id);
    }

    selected = elements['n'+id];
    selected.id = id;
    // updatePropsPanel();
    highlightSelection();
}

function highlightSelection() {
    let connect = false;
    let connDir = 'inputs';
    let connType = 'bool';

    if (wireMode) {
        let n = elements['n'+selected.id];

        if (n.actDir === 'outputs') {
            if (n.outputs[n.actNum].conns.length === 0) {
                connect = true;
                connType = n.outputs[n.actNum].type;
            }
        } else {
            if (!n.inputs[n.actNum].conn) {
                connect = true;
                connType = n.inputs[n.actNum].type;
                connDir = 'outputs';
            }
        }
    }

    Object.entries(elements).forEach(([key,n]) => {
        if (key === selected.name) {
            n.highlight('select');
        } else {
            if (connect && n.canTakeConn(connType)) {
                n.highlight('mark');
            } else {
                n.highlight('hide');
            }
        }
    });
}

// function updateNodeTransform() {
//     if (!selected) return;
//     const g = nodes.querySelector(`[data-id='${selected.id}']`);
//     if (g) g.setAttribute('transform', `translate(${selected.x} ${selected.y}) rotate(${selected.r || 0})`);
// }

// pointer drag for nodes
// let pointerState = {
//     dragging: false,
//     start: { x: 0, y: 0 },
//     node: null,
//     orig: { x: 0, y: 0 }
// };

// converts mouse coordinates from screen/browser coordinates
// to SVG coordinate space.
function svgPoint(evt) {
    const pt = svg.createSVGPoint();
    pt.x = evt.clientX;
    pt.y = evt.clientY;
    const ctm = svg.getScreenCTM().inverse();
    const p = pt.matrixTransform(ctm);
    return p;
}

// clicking blank deselects
svg.addEventListener('pointerdown', function (e) {
    if (e.target === svg || e.target === document.getElementById('grid')) {
        selected = null;

        Object.entries(elements).forEach(([key,n]) => {
            n.highlight('hide');
            n.clearSettings();
        });
    }

    // pan start when space pressed or middle button
    if (e.button === 1 || e.code === 'Space' || e.shiftKey || e.altKey) {
        startPan(e);
    }
});

// Pan & zoom
let panning = false;
let panStart = { x: 0, y: 0 };
let viewStart = { x: 0, y: 0 };

function startPan(e) {
    panning = true;
    panStart = svgPoint(e);
    viewStart = { x: viewBox.x, y: viewBox.y };
    document.addEventListener('pointermove', panMove);
    document.addEventListener('pointerup', endPan);
}

function panMove(e) { if (!panning) return;
    const p = svgPoint(e);
    const dx = p.x - panStart.x, dy = p.y - panStart.y;
    viewBox.x = viewStart.x - dx;
    viewBox.y = viewStart.y - dy;
    setView();
    // drawGrid();

}
function endPan(e) { panning = false;
    document.removeEventListener('pointermove', panMove);
    document.removeEventListener('pointerup', endPan);

}

// wheel zoom around mouse
svg.addEventListener('wheel', function (e) {
    e.preventDefault();
    const delta = e.deltaY > 0 ? 1.12 : 0.88;
    const mouse = svgPoint(e);
    const newW = viewBox.w * delta;
    const newH = viewBox.h * delta;

    // keep mouse stable
    const mxRatio = (mouse.x - viewBox.x) / viewBox.w;
    const myRatio = (mouse.y - viewBox.y) / viewBox.h;
    viewBox.x = mouse.x - mxRatio * newW;
    viewBox.y = mouse.y - myRatio * newH;
    viewBox.w = newW;
    viewBox.h = newH;
    setView();
});

new uiButton(-200, -40);
new uiLED(100, 40);
new uiGPO(200, 0);

// saveGraph();

toGraph();

// createEdge("n2","n1")
// createEdge("n1","n3")
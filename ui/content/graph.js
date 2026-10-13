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
let selectedEdge = null;
let wireMode = false;

const NETWORK_TYPES = [MSG_DEVICE, MSG_DEVICE_FIELD, MSG_NETWORK, MSG_NETWORK_FIELD];

// The node classes a config can restore, by control message type.
const NODE_CLASSES = {
    [MSG_UI_BUTTON]: uiButton,
    [MSG_UI_SLIDER]: uiSlider,
    [MSG_HW_LED]: uiLED,
    [MSG_HW_GPO]: uiGPO,
    [MSG_HW_PWM]: uiPWM,
};

// Rebuild the Command view from the graph's UI inputs.
function buildCommandInterface() {
    commandInterface = document.getElementById('command');
    commandInterface.innerHTML = '';
    Object.values(elements).forEach(elm => elm.buildUI(commandInterface));
}

function saveGraph() {
    buildCommandInterface();

    const output = [];

    // Keep the device's networking messages. Graph messages saved before are
    // dropped: the current graph replaces them.
    configMessages(gfig)
        .filter(m => NETWORK_TYPES.includes(m.type))
        .forEach(m => output.push(...m.bytes));

    Object.values(elements).forEach(elm => {
        output.push(...elm.marshal());
        output.push(...elm.marshalLabel());
        output.push(...elm.marshalLayout());
        output.push(...elm.marshalWire());
    })

    const binary = Uint8Array.from(output);
    if (PKT_HEADER_LEN + binary.length > PKT_CONFIG_MAX) {
        alert(`The graph is too large to save: ${PKT_HEADER_LEN + binary.length} bytes, `
            + `and the device holds at most ${PKT_CONFIG_MAX}. Remove some nodes or shorten labels.`);
        return;
    }
    const wsConfigMsg = new Uint8Array(3 + binary.length);
    wsConfigMsg[0] = PKT_CONFIG;
    wsConfigMsg[1] = (binary.length >> 8) & 0xFF;
    wsConfigMsg[2] = binary.length & 0xFF;
    wsConfigMsg.set(binary, 3);

    console.log(wsConfigMsg) //debug
    socket.send(wsConfigMsg); //online
    offlineConfig = wsConfigMsg; //offline
}

// Replace the graph with the one saved in a config's messages (see PROTOCOL.md,
// "The graph"). A config without control messages leaves the graph as it is.
function restoreGraph(msgs) {
    // The last message for an ID wins: configs saved by older UIs repeat controls.
    const controls = new Map();
    const labels = new Map();
    const layouts = new Map();
    const wires = new Map();
    for (const m of msgs) {
        if (NODE_CLASSES[m.type] && m.id !== 0) controls.set(m.id, m);
        else if (m.type === MSG_STRING && m.len > 0) labels.set(m.id, m);
        else if (m.type === MSG_LAYOUT && m.len === 4) layouts.set(m.id, m);
        // A wire into an output that takes several carries its source's ID.
        else if (m.type === MSG_WIRE && m.len === 4) wires.set(m.id, m);
        else if (m.type === MSG_WIRE && m.len === 6) wires.set(m.id + ':' + (m.pay[4] << 8 | m.pay[5]), m);
    }
    if (controls.size === 0) return;

    nodes.innerHTML = '';
    elements = {};
    gEdges = [];
    idEdgeCounter = 1;
    selected = null;
    selectedEdge = null;

    // Nodes saved without a position go in the first free slot of their column.
    const decoder = new TextDecoder();
    const int16 = (hi, lo) => ((hi << 8 | lo) << 16) >> 16;

    for (const [id, m] of controls) {
        idCounter = id;   // the constructor takes the next ID
        const elm = new NODE_CLASSES[m.type]();

        const layout = layouts.get(id);
        if (layout) {
            elm.x = int16(layout.pay[0], layout.pay[1]);
            elm.y = int16(layout.pay[2], layout.pay[3]);
        } else {
            ({ x: elm.x, y: elm.y } = freeSlot(nodeColumn(elm), elm));
        }
        elm.gElm.setAttribute('transform', `translate(${elm.x} ${elm.y}) rotate(${elm.r || 0})`);

        if (m.type === MSG_UI_SLIDER && m.len >= 1) {
            elm.setOrientation(m.pay[0] & SLIDER_VERTICAL ? 'vertical' : 'horizontal');
        }
        if (m.type === MSG_HW_GPO && m.len >= 3) elm.setPin(m.pay[2]);
        if (m.type === MSG_HW_PWM && m.len >= 15) {
            const u16 = (i) => m.pay[i] << 8 | m.pay[i + 1];
            elm.setPin(m.pay[2]);
            elm.setFreq(u16(3));
            elm.dutyFromInput = !!(m.pay[5] & PWM_DUTY_INPUT);
            elm.freqFromInput = !!(m.pay[5] & PWM_FREQ_INPUT);
            elm.freqLog = !!(m.pay[5] & PWM_FREQ_LOG);
            elm.setDuty(m.pay[6]);
            elm.setDutyMin(m.pay[7]);
            elm.setDutyMax(m.pay[8]);
            elm.setFreqMin(u16(11));
            elm.setFreqMax(u16(13));
        }
        if (labels.has(id)) elm.setLabel(decoder.decode(labels.get(id).pay));
    }
    idCounter = Math.max(...controls.keys()) + 1;

    // Wires: each output's source references (a PWM's duty and frequency
    // sources, another output's one source), shaped by their MSG_WIRE messages
    // if there are any (otherwise drawn straight between the nodes).
    const angle = (hi, lo) => (hi << 8 | lo) / 65536 * 2 * Math.PI;
    for (const [id, m] of controls) {
        const elm = elements['n' + id];
        if (elm.actDir !== 'inputs') continue;
        const ref = (i) => elements['n' + (m.len >= i + 2 ? (m.pay[i] << 8 | m.pay[i + 1]) : 0)] ?? null;

        if (m.type === MSG_HW_PWM) {
            if (m.len < 15) continue;
            // A source only drives a parameter that is from the input, and only
            // if it is floating: graphs saved before types were checked may wire
            // a button to a PWM.
            const source = (src, fromInput) => fromInput && src && elm.takesType(src) ? src : null;
            elm.dutySource = source(ref(0), elm.dutyFromInput);
            elm.freqSource = source(ref(9), elm.freqFromInput);
            elm.syncWires();
        } else if (ref(0)) {
            createEdge(ref(0).name, elm.name);     // none if the types don't match
        }

        for (const edge of elm.inConns()) {
            const wire = wires.get(id + ':' + edge.start.node.id) ?? wires.get(id);
            if (!wire) continue;
            edge.start.angle = angle(wire.pay[0], wire.pay[1]);
            edge.end.angle = angle(wire.pay[2], wire.pay[3]);
            edge.moveStart();
            edge.moveEnd();
        }
    }

    buildCommandInterface();
}

// Show or hide the add-node buttons.
function toggleAddMenu() {
    document.getElementById('add').classList.toggle('open');
}

// New nodes are laid out in two columns: the GUI inputs (buttons, sliders) on the
// left, the hardware outputs (LED, GPO, PWM) on the right, each filled from the top.
const COLUMN_X = { left: -200, right: 150 };
const ROW_TOP = -100;
const ROW_STEP = 100;

// The column a node belongs in: left for a source, right for an output.
function nodeColumn(elm) {
    return elm.actDir === 'outputs' ? 'left' : 'right';
}

// The first slot from the top of a column that no other node overlaps. A node
// covers about 120 × 80 units with its label, so nearer than that overlaps.
function freeSlot(column, except) {
    const x = COLUMN_X[column];
    for (let y = ROW_TOP; ; y += ROW_STEP) {
        const taken = Object.values(elements).some(e => e !== except
            && Math.abs(e.x - x) < 120 && Math.abs(e.y - y) < 80);
        if (!taken) return { x, y };
    }
}

// Bring a point into view, panning as little as needed, with a margin around it.
function panTo(x, y) {
    const m = 100;
    if (x - m < viewBox.x) viewBox.x = x - m;
    if (x + m > viewBox.x + viewBox.w) viewBox.x = x + m - viewBox.w;
    if (y - m < viewBox.y) viewBox.y = y - m;
    if (y + m > viewBox.y + viewBox.h) viewBox.y = y + m - viewBox.h;
    setView();
}

// Add a node of a control message type in the first free slot of its column,
// bring it into view, and select it.
function addNode(type) {
    const elm = new NODE_CLASSES[type]();
    const { x, y } = freeSlot(nodeColumn(elm), elm);
    elm.x = x;
    elm.y = y;
    elm.gElm.setAttribute('transform', `translate(${x} ${y}) rotate(${elm.r || 0})`);
    panTo(x, y);
    selectById(elm.id);
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

// Wire two nodes, a source and an output whose types match; null if they don't.
function createEdge(id1,id2) {
    let el1 = elements[id1];
    let el2 = elements[id2];
    const output = el1.actDir === 'inputs' ? el1 : el2;
    const source = output === el1 ? el2 : el1;
    if (!output.takesType(source)) return null;

    return new gEdge(el1, el2);
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
    if (selectedEdge) {
        selectedEdge.highlight(false);
        selectedEdge = null;
    }

    selected = elements['n'+id];
    selected.id = id;
    // updatePropsPanel();
    highlightSelection();
}

// Highlight the selected node. In wire mode, also mark the nodes it can be
// wired to: clicking a marked node adds the wire.
function highlightSelection() {
    Object.entries(elements).forEach(([key,n]) => {
        if (key === selected.name) {
            n.highlight('select');
        } else {
            if (wireMode && n.canTakeConn(selected)) {
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

// Select a wire, and nothing else.
function selectEdge(edge) {
    clearSelection();
    selectedEdge = edge;
    edge.highlight(true);
}

function clearSelection() {
    selected = null;
    Object.values(elements).forEach(n => {
        n.highlight('hide');
        n.clearSettings();
    });
    if (selectedEdge) {
        selectedEdge.highlight(false);
        selectedEdge = null;
    }
}

// Delete the selected wire, or the selected node with its wires.
function deleteSelected() {
    if (selectedEdge) {
        selectedEdge.remove();
        selectedEdge = null;
    } else if (selected) {
        selected.remove();
        clearSelection();
    }
}

// Delete or Backspace deletes the selection while the graph is shown, unless
// the key is for a text field.
document.addEventListener('keydown', function (e) {
    if (e.key !== 'Delete' && e.key !== 'Backspace') return;
    if (container.className !== 'graph') return;
    if (e.target.closest('input, textarea, select, [contenteditable]')) return;
    e.preventDefault();
    deleteSelected();
});

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
        clearSelection();
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

// zoom by factor around a screen point
function zoomAt(clientX, clientY, factor) {
    const mouse = svgPoint({ clientX, clientY });
    const newW = viewBox.w * factor;
    const newH = viewBox.h * factor;

    // keep mouse stable
    const mxRatio = (mouse.x - viewBox.x) / viewBox.w;
    const myRatio = (mouse.y - viewBox.y) / viewBox.h;
    viewBox.x = mouse.x - mxRatio * newW;
    viewBox.y = mouse.y - myRatio * newH;
    viewBox.w = newW;
    viewBox.h = newH;
    setView();
}

// wheel zoom around mouse
svg.addEventListener('wheel', function (e) {
    e.preventDefault();
    zoomAt(e.clientX, e.clientY, e.deltaY > 0 ? 1.12 : 0.88);
});

// touch: one finger on the background pans, two fingers pinch-zoom
const touches = new Map();
let touchPanning = false;

function touchMidDist() {
    const [a, b] = [...touches.values()];
    return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, d: Math.hypot(a.x - b.x, a.y - b.y) };
}

function panByScreen(dx, dy) {
    const scale = svg.getScreenCTM().a;
    viewBox.x -= dx / scale;
    viewBox.y -= dy / scale;
    setView();
}

svg.addEventListener('pointerdown', function (e) {
    if (e.pointerType !== 'touch') return;
    touches.set(e.pointerId, { x: e.clientX, y: e.clientY });

    if (touches.size === 1) {
        touchPanning = e.target === svg || e.target === document.getElementById('grid');
    } else {
        touchPanning = false;
        if (selected) selected.dragging = false;
    }
}, true);

svg.addEventListener('pointermove', function (e) {
    const t = touches.get(e.pointerId);
    if (!t) return;
    const before = touches.size === 2 ? touchMidDist() : null;
    const dx = e.clientX - t.x, dy = e.clientY - t.y;
    t.x = e.clientX;
    t.y = e.clientY;

    if (touchPanning) {
        panByScreen(dx, dy);
    } else if (before) {
        const after = touchMidDist();
        panByScreen(after.x - before.x, after.y - before.y);
        if (after.d > 0) zoomAt(after.x, after.y, before.d / after.d);
    }
}, true);

function touchEnd(e) {
    touches.delete(e.pointerId);
    touchPanning = false;
}
svg.addEventListener('pointerup', touchEnd, true);
svg.addEventListener('pointercancel', touchEnd, true);

// The demo graph: the button drives the onboard LED.
new uiButton(-200, -40);
new uiLED(100, 40);
new uiGPO(200, 0);
createEdge('n1', 'n2');

toGraph();

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
    [MSG_UI_SLIDER_2D]: uiSlider2D,
    [MSG_HW_LED]: uiLED,
    [MSG_HW_GPO]: uiGPO,
    [MSG_HW_PWM]: uiPWM,
    [MSG_UI_GRAPH]: uiPlot,
    [MSG_UI_INDICATOR]: uiIndicator,
    [MSG_HW_CORE]: uiCore,
    [MSG_HW_ADC]: uiADC,
    [MSG_HW_LM35]: uiLM35,
    [MSG_HW_GPI]: uiGPI,
    [MSG_HW_KEYBOARD]: uiKeyboard,
    [MSG_LOGIC_COMPARE]: uiCompare,
    [MSG_LOGIC_AND]: uiAnd,
    [MSG_LOGIC_OR]: uiOr,
    [MSG_LOGIC_NOT]: uiNot,
    [MSG_LOGIC_DELAY]: uiDelay,
    [MSG_LOGIC_SCALE]: uiScale,
    [MSG_LOGIC_MIN]: uiMin,
    [MSG_LOGIC_MAX]: uiMax,
    [MSG_LOGIC_PULSE]: uiPulse,
    [MSG_LOGIC_BLINK]: uiBlink,
    [MSG_LOGIC_TOGGLE]: uiToggle,
    [MSG_LOGIC_LATCH]: uiLatch,
};

// Whether a control message type is a logic element's.
const isLogic = (type) => type >= MSG_LOGIC_COMPARE && type <= MSG_LOGIC_LAST;

// Rebuild the Command view from the graph's UI inputs and outputs.
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
        // A wire into an output that takes several carries its source reference.
        else if (m.type === MSG_WIRE && m.len === 4) wires.set(m.id, m);
        else if (m.type === MSG_WIRE && m.len === 4 + refLen(m)) {
            const output = m.flags & MSG_FLAG_OUTPUT ? m.pay[6] : 0;
            wires.set(m.id + ':' + (m.pay[4] << 8 | m.pay[5]) + ':' + output, m);
        }
    }
    if (controls.size === 0) return;

    nodes.innerHTML = '';
    elements = {};
    gEdges = [];
    idEdgeCounter = 1;
    selected = null;
    selectedEdge = null;

    // Nodes saved without a position are stacked: inputs left, logic in the
    // middle, outputs right.
    const stacked = { inputs: 0, logic: 0, outputs: 0 };
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
            const side = !elm.inputs.length ? 'inputs' : !elm.outputs.length ? 'outputs' : 'logic';
            elm.x = { inputs: -200, logic: -25, outputs: 150 }[side];
            elm.y = -100 + 100 * stacked[side]++;
        }
        elm.gElm.setAttribute('transform', `translate(${elm.x} ${elm.y}) rotate(${elm.r || 0})`);

        // An output's settings come after its source reference (see refAt).
        const r = refLen(m);
        if (m.type === MSG_UI_SLIDER && m.len >= 1) {
            elm.setOrientation(m.pay[0] & SLIDER_VERTICAL ? 'vertical' : 'horizontal');
        }
        if (m.type === MSG_UI_SLIDER_2D && m.len >= 1) {
            elm.setRelease(m.pay[0] & SLIDER_RETURN ? 'centre' : 'stay');
            const ease = Object.keys(SLIDER_2D_EASES)
                .find(e => SLIDER_2D_EASES[e].bits === (m.pay[0] & SLIDER_EASE_MASK));
            elm.setReturnEase(ease);                            // an unknown one keeps the default
            if (m.len >= 2) elm.setReturnSpeed(m.pay[1]);       // 0 keeps the default
            else elm.setReturnEase('immediate');                // saved before returns had a speed
            const home = [VALUE_HALF, VALUE_HALF];
            if (m.len >= 4) home.splice(0, 2, m.pay[2], m.pay[3]);   // without one, the centre
            home.forEach((b, axis) => elm.setHome(axis, b / VALUE_FULL));
        }
        if (m.type === MSG_UI_BUTTON && m.len >= 1) elm.setAction(m.pay[0] & BUTTON_MOMENTARY ? 'momentary' : 'toggle');
        if (m.type === MSG_HW_GPO && m.len >= r + 1) elm.setPin(m.pay[r]);
        if (m.type === MSG_HW_KEYBOARD && m.len >= r + 1) elm.setStroke(m.pay.slice(r));
        if (m.type === MSG_UI_GRAPH && m.len >= r + 1) {
            elm.setWindow(m.pay[r]);
            elm.setUnit(decoder.decode(m.pay.slice(r + 1)));
        }
        if ((m.type === MSG_HW_ADC || m.type === MSG_HW_LM35) && m.len >= 1) elm.setChannel(m.pay[0]);
        if ([MSG_HW_CORE, MSG_HW_ADC, MSG_HW_LM35, MSG_HW_GPI].includes(m.type) || isLogic(m.type)) elm.shown = !!(m.flags & MSG_FLAG_SHOWN);
        // Logic with settings takes them from after its source reference (an invalid payload keeps the defaults).
        if (isLogic(m.type) && elm.restore) elm.restore(m.pay, r, m.len);
        if (m.type === MSG_HW_GPI && m.len >= 2) {
            elm.setPin(m.pay[0]);
            elm.setPull(m.pay[1] & GPI_PULL_MASK);
            elm.setActive(m.pay[1] & GPI_INVERT ? 'low' : 'high');
        }
        if (m.type === MSG_HW_PWM && m.len === 11 + 2 * r) {
            // After the duty source, and after the frequency source.
            const q = (i) => m.pay[r + i], f = (i) => m.pay[2 * r + 7 + i];
            elm.setPin(q(0));
            elm.setFreq(q(1) << 8 | q(2));
            elm.dutyFromInput = !!(q(3) & PWM_DUTY_INPUT);
            elm.freqFromInput = !!(q(3) & PWM_FREQ_INPUT);
            elm.freqLog = !!(q(3) & PWM_FREQ_LOG);
            elm.setDuty(q(4));
            elm.setDutyMin(q(5));
            elm.setDutyMax(q(6));
            elm.setFreqMin(f(0) << 8 | f(1));
            elm.setFreqMax(f(2) << 8 | f(3));
        }
        if (labels.has(id)) elm.setLabel(decoder.decode(labels.get(id).pay));
    }
    idCounter = Math.max(...controls.keys()) + 1;

    // Wires: each output's source references (a PWM's duty and frequency
    // sources, an AND's or OR's several, another output's or logic's one source),
    // shaped by their MSG_WIRE messages if there are any (otherwise drawn
    // straight between the nodes).
    const angle = (hi, lo) => (hi << 8 | lo) / 65536 * 2 * Math.PI;
    for (const [id, m] of controls) {
        const elm = elements['n' + id];
        if (!elm.inputs.length) continue;

        if (m.type === MSG_HW_PWM) {
            const r = refLen(m);
            if (m.len !== 11 + 2 * r) continue;
            // A source only drives a parameter that is from the input, and only
            // if it is floating: graphs saved before types were checked may wire
            // a button to a PWM.
            for (const [param, at] of [['duty', 0], ['freq', r + 7]]) {
                const { node, output } = refAt(m, at);
                if (elm[param + 'FromInput'] && node && elm.takesType(node, output)) {
                    elm[param + 'Source'] = node;
                    elm[param + 'Output'] = output;
                }
            }
            elm.syncWires();
        } else if (m.type === MSG_LOGIC_LATCH) {
            // Its set source, then its reset source.
            const r = refLen(m);
            if (m.len !== 2 * r) continue;
            ['set', 'reset'].forEach((role, k) => {
                const { node, output } = refAt(m, k * r);
                if (node) elm.setRole(role, node, output);
            });
        } else {
            // An AND, OR, min or max has a reference for each wire in; the others one.
            const r = refLen(m);
            const several = [MSG_LOGIC_AND, MSG_LOGIC_OR, MSG_LOGIC_MIN, MSG_LOGIC_MAX].includes(m.type);
            const refs = several ? Math.floor(m.len / r) : 1;
            for (let k = 0; k < refs; k++) {
                const { node, output } = refAt(m, k * r);
                // None if the types don't match, or (in a config not saved by the UI) it would close a loop.
                if (node) createEdge(node.name, elm.name, output);
            }
        }

        for (const edge of elm.inConns()) {
            const wire = wires.get(id + ':' + edge.start.node.id + ':' + edge.start.output) ?? wires.get(id);
            if (!wire) continue;
            edge.start.angle = angle(wire.pay[0], wire.pay[1]);
            edge.end.angle = angle(wire.pay[2], wire.pay[3]);
            edge.moveStart();
            edge.moveEnd();
        }
    }

    buildCommandInterface();
}

// The add-node menu: the plus shows the four groups of nodes, and a group shows
// its nodes. Folded, a button sits on the button that opens it, so before each
// change every button's offset from that one is measured (layout offsets, which
// ignore the transform), without a transition, so it folds to where that is now.
function measureAddMenu(menu) {
    const plus = menu.querySelector('.toggle');
    const buttons = [...menu.children].filter(b => b !== plus);
    buttons.forEach(b => {
        const from = b.classList.contains('node') ? menu.querySelector(`.group[data-group="${b.dataset.group}"]`) : plus;
        b.style.transition = 'none';
        b.style.setProperty('--dx', `${from.offsetLeft - b.offsetLeft}px`);
        b.style.setProperty('--dy', `${from.offsetTop - b.offsetTop}px`);
    });
    void menu.offsetWidth;      // apply the offsets before the transition comes back
    buttons.forEach(b => b.style.transition = '');
}

// Show a group's nodes, folding the open group's; the open group, or null, folds them.
function showAddGroup(menu, group) {
    menu.querySelectorAll('.group').forEach(b => b.classList.toggle('sel', b.dataset.group === group));
    menu.querySelectorAll('.node').forEach(b => b.classList.toggle('shown', b.dataset.group === group));
}

// The plus: show the groups, or fold the whole menu.
function toggleAddMenu() {
    const menu = document.getElementById('add');
    measureAddMenu(menu);
    menu.classList.toggle('open');
    if (!menu.classList.contains('open')) showAddGroup(menu, null);
}

// A group: show its nodes, or fold them if they are showing.
function toggleAddGroup(group) {
    const menu = document.getElementById('add');
    measureAddMenu(menu);
    const open = menu.querySelector(`.group[data-group="${group}"]`).classList.contains('sel');
    showAddGroup(menu, open ? null : group);
}

// Add a node of a control message type in the middle of the view, and select it.
// A node already there pushes the new one down and to the right.
function addNode(type) {
    let x = Math.round(viewBox.x + viewBox.w / 2);
    let y = Math.round(viewBox.y + viewBox.h / 2);
    while (Object.values(elements).some(e => e.x === x && e.y === y)) {
        x += 30;
        y += 30;
    }
    const elm = new NODE_CLASSES[type](x, y);
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

// Wire two nodes, a source and an output whose types match; null if they don't,
// or if the wire would close a loop. Between two logic nodes it goes from the
// first to the second (see wireEnds).
// The wire leaves the source from `sourceOutput`, by default its active output.
function createEdge(id1, id2, sourceOutput) {
    const ends = wireEnds(elements[id1], elements[id2]);
    if (!ends) return null;
    const [source, output] = ends;
    sourceOutput ??= source.actNum;
    if (!output.takesType(source, sourceOutput) || closesLoop(source, output)) return null;

    return new gEdge(source, output, sourceOutput);
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

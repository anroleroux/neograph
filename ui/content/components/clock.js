// A clock: the device's time of day, as three numbers, its hour (0 to 23), minute and
// second (0 to 59), each a whole number. The device has one clock, which every clock
// node reads; setting the time on any of them sets it, at once, with a command
// (PROTOCOL.md, "Clocks"), not in the saved graph. It starts at 00:00:00 when the device
// powers up. While the device reports it, the node shows the device's time; otherwise
// the browser keeps its own, from the computer's time or the time last set, so a graph
// can be tried without a device.
class uiClock extends uiComponents {
    constructor(x = 0, y = 0) {
        super("clock", x, y);
        this.label = "Clock";

        this.tc = MSG_HW_CLOCK;

        this.outputs.push(
            { type: "float", name: "Hour", conns: [] },
            { type: "float", name: "Minute", conns: [] },
            { type: "float", name: "Second", conns: [] },
        );
        this.actDir = "outputs";
        this.actNum = 0;            // the output the next wire leaves from
        this.shown = false;         // a tile in the Command view

        // The browser's clock: `ms` from midnight at performance.now() `at`.
        this.base = { ms: uiClock.computerMs(), at: performance.now() };
        this.device = [null, null, null];   // the device's last report of each part
        this.deviceAt = 0;                  // when, by performance.now(); 0 never
        this.parts = [null, null, null];    // the time shown

        this.buildGraphElement();
        this.timer = setInterval(() => this.tick(), 250);
        this.tick();
    }

    buildGraphElement() {
        const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
        g.classList.add('node');
        g.setAttribute('transform', `translate(${this.x} ${this.y}) rotate(${this.r || 0})`);
        g.innerHTML = `
            <rect x="-34" y="-18" width="68" height="36" rx="4" fill="#BFCDE740" stroke="#BFCDE7" stroke-width="1.5" />
            <text x="0" y="4" font-size="11" text-anchor="middle" fill="#BFCDE7">CLK</text>
            <text x="0" y="36" font-size="10" text-anchor="middle" fill="#BFCDE7">${this.label}</text>
            <rect x="-60" y="-40" width="120" height="80" rx="10" fill="none" stroke="#BFCDE7" stroke-width="0.8" style="display:none"/>
            <text x="0" y="47" font-size="8" text-anchor="middle" fill="#BFCDE7" opacity="0.6">${this.name}</text>
        `
        g.addEventListener('pointerdown', (e) => this.nodePointerDown(e));
        nodes.appendChild(g);

        this.gElm = g;
        this.gElmHighlight = g.children[3];
    }

    // The computer's time of day, in milliseconds from midnight.
    static computerMs() {
        const d = new Date();
        return ((d.getHours() * 60 + d.getMinutes()) * 60 + d.getSeconds()) * 1000 + d.getMilliseconds();
    }

    static DAY_MS = 86400000;

    // Whether the device has reported its clock lately (it reports each second).
    fromDevice() {
        return this.deviceAt > 0 && performance.now() - this.deviceAt < LOGIC_DEVICE_FRESH_MS
            && this.device.every(v => v !== null);
    }

    // The time now: the device's while it reports it, else the browser's. [h, m, s].
    now() {
        if (this.fromDevice()) return [...this.device];
        const s = Math.floor(((this.base.ms + performance.now() - this.base.at) % uiClock.DAY_MS) / 1000);
        return [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60];
    }

    // Show the time, and hand each part that changed on to the nodes wired to it. A
    // node no longer in the graph (a restore replaced it) stops.
    tick() {
        if (mainElements()[this.name] !== this) {
            clearInterval(this.timer);
            return;
        }
        const now = this.now();
        now.forEach((v, k) => {
            if (v === this.parts[k]) return;
            this.parts[k] = v;
            this.emit(v, k);
        });
        this.gElm.children[1].textContent = this.reading();
        if (this.tileState?.isConnected) this.tileState.textContent = this.reading();
    }

    reading() {
        return this.parts.every(v => v !== null) ? this.parts.map(v => String(v).padStart(2, '0')).join(':') : '–';
    }

    outputValue(output) {
        return this.parts[output];
    }

    // Its signals from the device (PROTOCOL.md, "Telemetry text frames"): its hour,
    // minute and second.
    signal(output, value) {
        if (output > 2) return;
        this.device[output] = Math.round(value);
        this.deviceAt = performance.now();
        this.tick();
    }

    // Set the clock to h:m:s: the device's, with a command, and every clock node's in
    // the browser. False, leaving it, if that is not a time of day.
    setTime(h, m, s) {
        const parts = [h, m, s].map(v => String(v).trim());
        if (!parts.every(v => /^\d{1,2}$/.test(v))) return false;
        [h, m, s] = parts.map(Number);
        if (h >= CLOCK_HOURS || m >= CLOCK_MINUTES || s >= CLOCK_SECONDS) return false;
        const base = { ms: ((h * 60 + m) * 60 + s) * 1000, at: performance.now() };
        for (const c of Object.values(mainElements()).filter(e => e instanceof uiClock)) {
            c.base = { ...base };
            c.deviceAt = 0;     // until the device reports its new time
            c.tick();
        }
        const pkt = Uint8Array.from([PKT_COMMAND, 0, MSG_HEADER_LEN + CLOCK_SET_LEN,
            this.tc, 0, (this.id >> 8) & 0xFF, this.id & 0xFF, CLOCK_SET_LEN, h, m, s]);
        console.log(pkt) //debug
        socket.send(pkt); //online
    }

    // One part of the time (0 hour, 1 minute, 2 second), the others as they are now.
    setPart(k, v) {
        const now = this.now();
        now[k] = v;
        return this.setTime(...now);
    }

    // The computer's time, to the second.
    setToComputer() {
        const s = Math.floor(uiClock.computerMs() / 1000);
        return this.setTime(Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60);
    }

    setWireOutput(output) {
        if (![0, 1, 2].includes(output)) return false;
        this.actNum = output;
        if (wireMode && selected) highlightSelection();     // mark what that output can be wired to
    }

    createSettings() {
        super.createSettings();
        const settings = document.getElementById('settings');
        const again = () => this.createSettings();
        const note = document.createElement('p');
        note.className = 'setting-note';
        note.textContent = 'The device\'s clock: every clock node shows the same time. It starts at 00:00:00 '
            + 'when the device powers up; setting it here sets it on the device at once.';
        settings.appendChild(note);
        [['Hours', CLOCK_HOURS], ['Minutes', CLOCK_MINUTES], ['Seconds', CLOCK_SECONDS]].forEach(([label, top], k) =>
            settings.appendChild(editableSetting(label, () => String(this.now()[k]),
                (v) => this.setPart(k, v) ?? again(), {type: 'number', min: 0, max: top - 1, step: 1})));
        const row = document.createElement('div');
        row.className = 'editable choice';
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = 'Set to this computer\'s time';
        button.addEventListener('click', () => { this.setToComputer(); again(); });
        row.appendChild(button);
        settings.appendChild(row);
        settings.appendChild(choiceSetting('Wire from', {0: 'Hour', 1: 'Minute', 2: 'Second'},
            () => String(this.actNum), (o) => this.setWireOutput(Number(o))));
        settings.appendChild(shownSetting(this));
    }

    // In the Command view, if shown: a tile with the label and the time.
    buildUI(commandInterface) {
        if (!this.shown) return;
        const control = document.createElement('div');
        control.className = 'control-reading';
        control.innerHTML = `<span class="name"></span><span class="state"></span>`;
        control.querySelector('.name').textContent = this.label;
        control.title = this.label;
        this.tileState = control.querySelector('.state');
        this.tileState.textContent = this.reading();
        commandInterface.appendChild(control);
    }

    // No payload: the time is the device's, set with a command.
    marshal() {
        return [this.tc, this.shown ? MSG_FLAG_SHOWN : 0x00, (this.id >> 8) & 0xFF, this.id & 0xFF, 0x00];
    }

    remove() {
        clearInterval(this.timer);
        super.remove();
    }
}

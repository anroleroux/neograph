// Drive the hardware UI (make uihw) in a headless browser against a fake device,
// and write every packet the UI sends to a file, one hex string per line, for
// check_packets to verify with the firmware's own parsing code.
//
//   node tests/ui_packets.cjs ui/dist/index.html ui/dist/packets.txt
//
// The page is the one the firmware serves: offline stand-ins are stripped, so
// it talks to window.WebSocket, which is replaced here by a fake that records
// what is sent and lets the test play the device's side.

const path = require('path');
const fs = require('fs');
const { chromium } = require('playwright');

const [page_file, out_file] = process.argv.slice(2);

// What the device sends on connect: its DID, then its stored config (the
// first-boot config from PROTOCOL.md, "Storage").
const DID = [0xBD, 0xFC, 0xEA, 0xC1, 0x4E, 0x1E, 0x79, 0x4D];
const DEVICE_ID = [0x0D, 0x00, 0x08, ...DID];
const FIRST_BOOT_CONFIG = [
    0x0A, 0x00, 0x3A,
    0xB0, 0x00, 0x00, 0x00, 0x09, 0x00, ...DID,
    0xB1, 0x00, 0x00, 0x00, 0x05, 0x01, 0x4C, 0x38, 0x53, 0x47,
    0xB1, 0x00, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00,
    0xB2, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
    0xB3, 0x00, 0x00, 0x00, 0x05, 0x01, 0x4C, 0x38, 0x53, 0x47,
    0xB3, 0x00, 0x00, 0x00, 0x04, 0x02, 0x4C, 0x52, 0x58,
];

function fail(msg) {
    console.error('ui-test: ' + msg);
    process.exit(1);
}

function expect(cond, msg) {
    if (!cond) fail(msg);
}

const hex = (bytes) => bytes.map(b => b.toString(16).padStart(2, '0')).join('');

// The graph the tests start from, in place of the demo graph: button n1, LED n2 ← n1
// and GPO n3 on pin 1, where the demo graph used to put them.
const TEST_GRAPH = [
    0x10, 0x00, 0x00, 0x01, 0x00,                                   // button n1
    0xA1, 0x00, 0x00, 0x01, 0x04, 0xFF, 0x38, 0xFF, 0xD8,           //   at (-200, -40)
    0x20, 0x01, 0x00, 0x02, 0x03, 0x00, 0x01, 0x00,                 // LED n2 ← n1
    0xA1, 0x00, 0x00, 0x02, 0x04, 0x00, 0x64, 0x00, 0x28,           //   at (100, 40)
    0x21, 0x01, 0x00, 0x03, 0x04, 0x00, 0x00, 0x00, 0x01,           // GPO n3, pin 1
    0xA1, 0x00, 0x00, 0x03, 0x04, 0x00, 0xC8, 0x00, 0x00,           //   at (200, 0)
];

// Open the page; with `demo` false, replace the demo graph with TEST_GRAPH.
async function open(browser, demo = false) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.addInitScript(() => {
        window.__sent = [];
        class FakeWebSocket {
            constructor(url) {
                this.url = url;
                this.readyState = FakeWebSocket.CONNECTING;
                window.__ws = this;
                // window.__wsRefuse: the device can't be reached, as the browser
                // reports it: a close with code 1006 and no open.
                setTimeout(() => {
                    if (window.__wsRefuse) {
                        this.readyState = FakeWebSocket.CLOSED;
                        if (this.onclose) this.onclose({ code: 1006 });
                        return;
                    }
                    this.readyState = FakeWebSocket.OPEN;
                    if (this.onopen) this.onopen();
                }, 50);
            }
            send(data) {
                if (this.readyState !== FakeWebSocket.OPEN) throw new Error('send while not open');
                const bytes = data instanceof ArrayBuffer
                    ? new Uint8Array(data)
                    : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
                window.__sent.push(Array.from(bytes));
            }
            close() {
                this.readyState = FakeWebSocket.CLOSED;
                if (this.onclose) this.onclose({ code: 1005 });
            }
            // The device's side: a binary frame, or a text frame for a string.
            receive(data) {
                this.onmessage({ data: typeof data === 'string' ? data : Uint8Array.from(data).buffer });
            }
        }
        FakeWebSocket.CONNECTING = 0;
        FakeWebSocket.OPEN = 1;
        FakeWebSocket.CLOSED = 3;
        window.WebSocket = FakeWebSocket;
    });
    await page.goto('file://' + path.resolve(page_file));
    if (!demo) await page.evaluate((g) => restoreGraph(configMessages(Uint8Array.from(g))), TEST_GRAPH);
    return { page, errors };
}

// Connect from the Network view and play the device's greeting.
async function connect(page, config) {
    await page.locator('.device-connect').first().dispatchEvent('click');
    await page.waitForFunction(() => window.__ws && window.__ws.readyState === 1);
    await page.evaluate(([id, cfg]) => {
        window.__ws.receive(id);
        window.__ws.receive(cfg);
        window.__ws.receive('uptime 5');     // telemetry text frame
    }, [DEVICE_ID, config]);
}

// Show the Network view, once it has slid in.
async function toNetworkView(page) {
    await page.evaluate(() => toNetwork());
    await page.waitForTimeout(500);
}

// Show the Command view, and give the plots a frame to draw.
async function toCommandView(page) {
    await page.evaluate(() => toCommand());
    await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
}

const sent = (page) => page.evaluate(() => window.__sent.splice(0));

(async () => {
    const browser = await chromium.launch();
    const packets = [];

    // ── The demo graph: the button drives the LED; below, a second button starts a
    // loop that blinks the light three times ──
    let { page, errors } = await open(browser, true);
    const demo = await page.evaluate(async () => {
        const wires = gEdges.map(e => e.start.node.name + '>' + e.end.node.name + (e.feedback ? '~' : '')).join(' ');
        const blinks = [];
        const base = elements.n5.emit.bind(elements.n5);
        elements.n5.emit = (v, o = 0) => { if (v) blinks.push(v); base(v, o); };
        const b = elements.n3;
        b.value = true; b.emit(1); b.value = false; b.emit(0);
        await new Promise(r => setTimeout(r, 1700));
        return { wires, types: Object.values(elements).map(e => e.type).join(), blinks: blinks.length,
            after: [elements.n4.value, elements.n5.value, elements.n8.count].join() };
    });
    expect(demo.wires === 'n1>n2 n3>n4 n4>n5 n5>n6 n5>n8 n4>n7 n7>n8 n8>n4~'
        && demo.types === 'button,led,button,latch,blink,light,not,counter', 'the demo graph is not as expected: ' + JSON.stringify(demo));
    expect(demo.blinks === 3 && demo.after === 'false,false,0', 'the demo loop did not blink three times: ' + JSON.stringify(demo));
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // ── First connect: a first-boot device, then edit and save the graph ──
    ({ page, errors } = await open(browser));
    await connect(page, FIRST_BOOT_CONFIG);
    expect(await page.evaluate(() => networks[0].ssid) === 'LRX', 'the first-boot config was not read');
    expect((await sent(page)).length === 0, 'the UI sent something on connect');
    expect(await page.evaluate(() => gEdges.map(e => e.start.node.name + '>' + e.end.node.name).join()) === 'n1>n2',
        'the test graph does not wire n1 to n2');

    // Wire mode marks only the nodes a wire can join: from a source, the outputs
    // without a wire; from a free output, the sources.
    const marks = await page.evaluate(() => {
        const marked = () => Object.values(elements).filter(e => e.marked).map(e => e.name).join();
        toggleConnectMode();
        selectById(1);      // the button, already wired to the LED
        const fromButton = marked();
        clearSelection();
        selectById(3);      // the GPO, without a wire
        const fromGpo = marked();
        clearSelection();
        toggleConnectMode();
        return { fromButton, fromGpo };
    });
    expect(marks.fromButton === 'n3', 'wire mode from the button marks ' + marks.fromButton);
    expect(marks.fromGpo === 'n1', 'wire mode from the GPO marks ' + marks.fromGpo);

    await page.evaluate(() => {
        createEdge('n1', 'n3');      // n1 -> n2 is wired by default
        elements.n1.setLabel('Start');
        elements.n2.setLabel('Pompe à eau');
        elements.n3.setPin('7');
        const led = elements.n2;
        led.x = 260; led.y = -80;
        led.gElm.setAttribute('transform', `translate(${led.x} ${led.y}) rotate(0)`);
        gEdges[0].start.angle = Math.PI / 2;
        gEdges[0].start.mvfunc();
    });

    // The add menu: the plus shows the four groups; a group shows its nodes, one
    // group at a time, and the plus folds it all.
    const menu = () => page.evaluate(() => {
        const showing = (sel) => [...document.querySelectorAll(sel)]
            .filter(b => getComputedStyle(b).pointerEvents !== 'none').map(b => b.textContent.trim()).join(' ');
        return showing('#add .group') + ' | ' + showing('#add .node');
    });
    expect(await menu() === ' | ', 'the folded menu shows ' + await menu());
    await page.locator('#add .toggle').dispatchEvent('click');
    expect(await menu() === 'UII HWO HWI UIO USB TM LGC SUB | ', 'the plus shows ' + await menu());
    const groups = [];
    for (const g of ['uii', 'hwo', 'hwi', 'uio', 'usb', 'tm', 'lgc', 'sub']) {
        await page.locator(`#add .group[data-group="${g}"]`).dispatchEvent('click');
        groups.push((await menu()).split(' | ')[1]);
    }
    expect(groups.join(', ') === 'BTN SLD XY, LED GPO PWM, TMP ADC GPI, PLT LGT, KEY, DLY PLS BLK, CMP AND OR NOT SCL MIN MAX TGL LCH CNT, NEW', 'the groups show ' + groups.join(', '));
    await page.locator('#add .group[data-group="sub"]').dispatchEvent('click');
    expect(await menu() === 'UII HWO HWI UIO USB TM LGC SUB | ', 'clicking the open group again left ' + await menu());
    await page.locator('#add .group[data-group="hwo"]').dispatchEvent('click');

    // Add a PWM output from the add menu, set it up and wire the button to it.
    await page.locator('#add [title="Add a PWM output"]').dispatchEvent('click');
    const rows = () => page.$$eval('#settings .editable .label', ls => ls.slice(2).map(l => l.textContent).join('|'));
    const select = (label) => page.locator('#settings .editable.select', { has: page.locator(`.label:text-is("${label}:")`) })
        .locator('select');
    const choice = (label, option) => page.locator('#settings .choice', { has: page.locator(`.label:text-is("${label}:")`) })
        .locator('button', { hasText: option }).dispatchEvent('click');
    expect(await rows() === 'Frequency:|Frequency (Hz):|Duty cycle:|Duty cycle (%):',
        'a new PWM does not show a constant frequency and duty: ' + await rows());
    await choice('Duty cycle', 'From input');
    expect(await rows() === 'Frequency:|Frequency (Hz):|Duty cycle:|Input:|Duty cycle at 0 (%):|Duty cycle at 1 (%):',
        'duty from the input does not show its input, min and max: ' + await rows());
    await page.evaluate(() => {
        const pwm = elements.n4;
        if (pwm.setFreq('5') !== false || pwm.setDuty('101') !== false || pwm.setPin('29') !== false
            || pwm.setDutyMin('101') !== false || pwm.setDutyMax('-1') !== false
            || pwm.setFreqMin('9') !== false || pwm.setFreqMax('65536') !== false) {
            throw new Error('PWM settings out of range were accepted');
        }
        // The pins that drive the WiFi chip, and those off the header, are refused.
        for (const pin of ['23', '24', '25', '29', '30', '-1', '2.5', 'x']) {
            if (pwm.setPin(pin) !== false || elements.n3.setPin(pin) !== false) {
                throw new Error('GPIO pin ' + pin + ' was accepted');
            }
        }
        pwm.setPin('15');
        pwm.setFreq('20000');
        pwm.setDuty('75');
        pwm.setDutyMin('20');
        pwm.setDutyMax('80');
    });

    // The pin is picked from the usable pins only, less those another node uses:
    // the GPO is on GPIO 7.
    const pinOptions = () => page.evaluate(() => [...[...document.querySelectorAll('#settings .editable.select')]
        .find(row => row.querySelector('.label').textContent === 'GPIO pin:')
        .querySelectorAll('option')].map(o => o.value).join());
    const pins = await pinOptions();
    expect(pins === [...Array(23).keys(), 26, 27, 28].filter(p => p !== 7).join(), 'the PWM offers GPIO pins ' + pins);
    await select('GPIO pin').selectOption('26');
    expect(await page.evaluate(() => elements.n4.pin) === 26, 'picking GPIO 26 did not set the PWM pin');
    await page.evaluate(() => elements.n4.setPin('15'));

    // Add a slider, make it vertical from its settings, and wire it to the PWM.
    await page.locator('#add [title="Add a slider"]').dispatchEvent('click');
    await page.locator('#settings .choice button', { hasText: 'Vertical' }).dispatchEvent('click');
    expect(await page.evaluate(() => elements.n5.vertical), 'the orientation choice did not make the slider vertical');
    await page.evaluate(() => {
        elements.n5.setLabel('Speed');
        createEdge('n5', 'n4');     // a wire into the PWM drives its duty, which has no input yet
    });
    expect(await page.evaluate(() => elements.n4.dutySource?.name) === 'n5', 'the wire into the PWM does not drive its duty');

    // Add a second slider, and pick it in the PWM's settings to drive its frequency.
    await page.locator('#add [title="Add a slider"]').dispatchEvent('click');
    await page.evaluate(() => { elements.n6.setLabel('Pitch'); selectById(4); });
    await choice('Frequency', 'From input');
    await select('Input').first().selectOption('n6:0');    // the frequency's; the duty's follows
    await page.evaluate(() => { elements.n4.setFreqMin('200'); elements.n4.setFreqMax('2000'); });
    expect(await page.evaluate(() => elements.n4.freqLog) === false, 'a frequency from an input does not start linear');
    await choice('Scale', 'Logarithmic');
    expect(await page.evaluate(() => elements.n4.freqLog), 'the Scale choice did not make the frequency logarithmic');
    const freqWire = await page.evaluate(() => [elements.n4.freqSource?.name,
        elements.n4.inConns().map(c => c.start.node.name).join()].join('|'));
    expect(freqWire === 'n6|n5,n6', 'picking the frequency input did not wire it: ' + freqWire);

    // Add a plot, set its window, and wire the first slider to it. A plot takes
    // only a floating source.
    await page.locator('#add [title="Add a plot"]').dispatchEvent('click');
    const plot = await page.evaluate(() => {
        const plot = elements.n7;
        const rejected = plot.setWindow('0') === false && plot.setWindow('256') === false && plot.setWindow('2.5') === false;
        plot.setWindow('20');
        plot.setLabel('Speed plot');
        return {
            rejected,
            buttonToPlot: createEdge('n1', 'n7') === null,
            sliderToPlot: createEdge('n5', 'n7') !== null && plot.sourceID() === 5,
        };
    });
    expect(plot.rejected, 'plot windows out of range were accepted');
    expect(plot.buttonToPlot, 'a button was wired to a plot');
    expect(plot.sliderToPlot, 'the slider was not wired to the plot');

    // Add the core temperature and a second plot for it. Its values are on the
    // device too, so it drives a PWM as well as a plot; it is floating, so not a GPO.
    await page.locator('#add [title="Add the core temperature"]').dispatchEvent('click');
    // A hardware input's tile is hidden until its settings show it.
    const coreRows = await page.$$eval('#settings .editable .label', ls => ls.map(l => l.textContent).join('|'));
    expect(coreRows === 'Label:|Command view:' && await page.evaluate(() => elements.n8.shown === false),
        'the core temperature does not start hidden: ' + coreRows);
    await choice('Command view', 'Show');
    await page.locator('#add [title="Add a plot"]').dispatchEvent('click');
    const core = await page.evaluate(() => {
        elements.n9.setLabel('Temperature');
        clearSelection();
        toggleConnectMode();
        selectById(8);
        const marked = Object.values(elements).filter(e => e.marked).map(e => e.name).join();
        clearSelection();
        toggleConnectMode();
        return {
            marked,
            forPwm: elements.n4.takesType(elements.n8),
            toLed: createEdge('n8', 'n3') === null,
            toPlot: createEdge('n8', 'n9') !== null && elements.n9.sourceID() === 8,
        };
    });
    expect(core.marked === 'n9', 'wire mode from the core temperature marks ' + core.marked);
    expect(core.forPwm, 'a PWM does not take the core temperature');
    expect(core.toLed, 'the core temperature was wired to a GPO');
    expect(core.toPlot, 'the core temperature was not wired to the plot');

    // Add an ADC input on ADC1, and a plot for it. Its values are on the device,
    // so it drives a PWM as well as a plot; it is floating, so not a GPO.
    await page.locator('#add [title="Add an ADC input"]').dispatchEvent('click');
    const channels = await page.evaluate(() => [...document.querySelectorAll('#settings .editable.select option')]
        .map(o => o.textContent).join('|'));
    expect(channels === 'ADC0 · GPIO 26|ADC1 · GPIO 27|ADC2 · GPIO 28', 'the ADC offers channels ' + channels);
    await select('Channel').selectOption('1');
    await choice('Command view', 'Show');
    await page.locator('#add [title="Add a plot"]').dispatchEvent('click');
    const adc = await page.evaluate(() => {
        const adc = elements.n10;
        adc.setLabel('Level sensor');
        elements.n11.setLabel('Level');
        return {
            channel: adc.channel,
            rejected: adc.setChannel('3') === false && adc.setChannel('-1') === false && adc.setChannel('x') === false,
            toGpo: createEdge('n10', 'n3') === null,
            forPwm: elements.n4.takesType(adc),
            toPlot: createEdge('n10', 'n11') !== null && elements.n11.sourceID() === 10,
        };
    });
    expect(adc.channel === 1, 'picking ADC1 did not set the channel');
    expect(adc.rejected, 'ADC channels out of range were accepted');
    expect(adc.toGpo, 'the ADC was wired to a GPO');
    expect(adc.forPwm, 'a PWM does not take the ADC');
    expect(adc.toPlot, 'the ADC was not wired to the plot');

    // Add a USB keyboard typing Ctrl+Alt+Delete, set from its settings, and a
    // momentary button to press it. A keyboard takes a button, not a slider.
    await page.locator('#add .group[data-group="usb"]').dispatchEvent('click');
    await page.locator('#add [title="Add a USB keyboard"]').dispatchEvent('click');
    const keyRows = () => page.$$eval('#settings .editable .label', ls => ls.map(l => l.textContent).join('|'));
    expect(await keyRows() === 'Label:|Types:|Hold:|Key:', 'a new keyboard does not show a key combo: ' + await keyRows());
    await choice('Hold', 'Ctrl');
    await choice('Hold', 'Alt');
    await select('Key').selectOption(String(0x4C));
    expect(await page.evaluate(() => elements.n12.describe()) === 'Ctrl+Alt+Delete',
        'the keyboard settings made ' + await page.evaluate(() => elements.n12.describe()));
    await choice('Types', 'Text');
    expect(await keyRows() === 'Label:|Types:|Text:', 'text does not show its text: ' + await keyRows());
    const text = await page.evaluate(() => {
        const kb = elements.n12;
        const rejected = kb.setText('') === false && kb.setText('é') === false && kb.setText('\r') === false
            && kb.setText('x'.repeat(KEYBOARD_TEXT_MAX + 1)) === false && kb.setKey('256') === false;
        kb.setText(keyboardUnescape('a\\tb\\\\'));
        return { rejected, text: JSON.stringify(kb.text), shown: keyboardEscape(kb.text) };
    });
    expect(text.rejected, 'keystrokes the keyboard cannot type were accepted');
    expect(text.text === '"a\\tb\\\\"' && text.shown === 'a\\tb\\\\', 'escaped text reads as ' + JSON.stringify(text));
    await choice('Types', 'Key');
    await page.locator('#add [title="Add a button"]').dispatchEvent('click');
    await choice('Action', 'Momentary');
    await page.locator('#add [title="Add a USB keyboard"]').dispatchEvent('click');
    const keyboard = await page.evaluate(() => {
        elements.n12.setLabel('Lock');
        elements.n13.setLabel('Press');
        elements.n14.setLabel('Greeting');
        elements.n14.setMode(KEYBOARD_TEXT);
        elements.n14.setText('hello\n');
        return {
            momentary: elements.n13.momentary,
            fromSlider: createEdge('n5', 'n12') === null,
            fromButton: createEdge('n13', 'n12') !== null && elements.n12.sourceID() === 13,
            fromToggle: createEdge('n1', 'n14') !== null,
            marshal: [elements.n12, elements.n13, elements.n14, elements.n1].map(n => n.marshal()),
        };
    });
    expect(keyboard.momentary, 'the Action choice did not make the button momentary');
    expect(keyboard.fromSlider, 'a slider was wired to the keyboard');
    expect(keyboard.fromButton && keyboard.fromToggle, 'the buttons were not wired to the keyboards');
    // A source reference is the source's ID, then its output (MSG_FLAG_OUTPUT).
    const marshalled = keyboard.marshal.map(hex).join();
    expect(marshalled === '2301000c06000d0000054c,1000000d0101,2301000e0a00010001' + '68656c6c6f0a,1000000100',
        'the keyboards and buttons marshal as ' + marshalled);

    // Add a 2D slider that returns to the centre, wire its Y to a plot, and drive
    // a PWM's duty from its X and frequency from its Y: two wires from one node.
    await page.locator('#add .group[data-group="uii"]').dispatchEvent('click');
    await page.locator('#add [title="Add a 2D slider"]').dispatchEvent('click');
    expect(await keyRows() === 'Label:|Wire from:|Home X:|Home Y:|When let go:', 'a 2D slider does not show its settings: ' + await keyRows());
    // Its home: where it starts and returns to. The centre unless set.
    const home = await page.evaluate(() => {
        const s = elements.n15;
        const centre = s.home.join() + '|' + s.values.map(v => v.toFixed(2)).join();
        const rejected = s.setHome(0, '1.1') === false && s.setHome(1, '-0.1') === false && s.setHome(0, 'x') === false;
        s.setHome(0, '0.25');
        s.setHome(1, '.75');
        s.createSettings();
        const shown = [...document.querySelectorAll('#settings .editable')].slice(2, 4)
            .map(row => row.querySelector('.value').textContent).join();
        return { centre, rejected, home: s.home.join(), shown };
    });
    expect(home.centre === '128,128|0.50,0.50' && home.rejected && home.home === '64,191' && home.shown === '0.25,0.75',
        'the home settings gave ' + JSON.stringify(home));
    await choice('When let go', 'Return home');
    expect(await keyRows() === 'Label:|Wire from:|Home X:|Home Y:|When let go:|Return:|Speed (%/s):',
        'returning to the centre does not show its shape and speed: ' + await keyRows());
    // The speed's input has room to type in.
    await page.locator('#settings .editable', { has: page.locator('.label:text-is("Speed (%/s):")') })
        .locator('svg').first().dispatchEvent('click');
    const speedInput = await page.locator('#settings .editable input').evaluate(i => [i.value, i.getBoundingClientRect().width]);
    expect(speedInput[0] === '100' && speedInput[1] >= 50, 'the speed input is ' + JSON.stringify(speedInput));
    await page.locator('#settings .editable input').fill('120');
    await page.locator('#settings .editable input').press('Enter');
    await page.evaluate(() => { document.body.scrollLeft = 0; });     // typing scrolled the settings into view
    const speeds = await page.evaluate(() => {
        const s = elements.n15;
        const typed = s.returnSpeed;
        const rejected = s.setReturnSpeed('256') === false && s.setReturnSpeed('0') === false && s.setReturnSpeed('-1') === false
            && s.setReturnSpeed('1.5') === false && s.setReturnSpeed('x') === false && s.setReturnEase('bounce') === false;
        return { typed, rejected, ease: s.returnEase };
    });
    expect(speeds.typed === 120 && speeds.rejected && speeds.ease === 'linear', 'the return settings gave ' + JSON.stringify(speeds));
    await choice('Return', 'Immediate');        // for the exact commands below
    expect(await keyRows() === 'Label:|Wire from:|Home X:|Home Y:|When let go:|Return:', 'an immediate return shows ' + await keyRows());
    await choice('Wire from', 'Y');
    await page.locator('#add .group[data-group="uio"]').dispatchEvent('click');
    await page.locator('#add [title="Add a plot"]').dispatchEvent('click');
    const pad = await page.evaluate(() => {
        elements.n15.setLabel('Stick');
        elements.n16.setLabel('Stick Y');
        const marked = () => Object.values(elements).filter(e => e.marked).map(e => e.name).join();
        clearSelection();
        toggleConnectMode();
        selectById(15);
        const fromY = marked();
        clearSelection();
        toggleConnectMode();
        return {
            returns: elements.n15.returns,
            output: elements.n15.actNum,
            fromY,
            toLed: createEdge('n15', 'n2') === null,
            toPlot: createEdge('n15', 'n16') !== null && hex(elements.n16.sourceRef()) === '000f01',
            next: elements.n15.actNum,      // wired from Y, the next wire is from X
            again: createEdge('n15', 'n16', 0) !== null && hex(elements.n16.sourceRef()) === '000f00'
                && createEdge('n15', 'n16') !== null && hex(elements.n16.sourceRef()) === '000f01',
            label: gEdges.at(-1).outputName.textContent,
        };
        function hex(bytes) { return bytes.map(b => b.toString(16).padStart(2, '0')).join(''); }
    });
    expect(pad.returns, 'the When let go choice did not make the 2D slider return');
    expect(pad.output === 1, 'the Wire from choice did not pick Y');
    expect(pad.next === 0, 'a wire from Y did not turn Wire from to X');
    expect(pad.fromY === 'n16', 'wire mode from the 2D slider\'s Y marks ' + pad.fromY);
    expect(pad.toLed, 'the 2D slider was wired to an LED');
    expect(pad.toPlot && pad.again, 'the 2D slider\'s Y was not wired to the plot, or X did not replace it');
    expect(pad.label === 'Y', 'the wire from Y is labelled ' + pad.label);

    await page.locator('#add .group[data-group="hwo"]').dispatchEvent('click');
    await page.locator('#add [title="Add a PWM output"]').dispatchEvent('click');
    await choice('Duty cycle', 'From input');
    const inputs = await page.$$eval('#settings .editable.select', rows => rows
        .filter(row => row.querySelector('.label').textContent === 'Input:')
        .map(row => [...row.querySelectorAll('option')].map(o => o.value + '=' + o.textContent).join(',')));
    expect(inputs[0] === '=None,n5:0=Speed (n5),n6:0=Pitch (n6),n8:0=Core temperature (n8),n10:0=Level sensor (n10),n15:0=Stick X (n15),n15:1=Stick Y (n15)',
        'the PWM offers inputs ' + inputs[0]);
    await select('Input').first().selectOption('n15:0');
    await choice('Frequency', 'From input');
    await select('Input').first().selectOption('n15:1');       // the frequency's
    const xy = await page.evaluate(() => {
        const pwm = elements.n17;
        pwm.setLabel('Motor');
        return {
            sources: [pwm.dutySource?.name, pwm.dutyOutput, pwm.freqSource?.name, pwm.freqOutput].join(),
            wires: pwm.inConns().map(c => c.start.node.name + ':' + c.outputName.textContent).join(),
            marshal: pwm.marshal().map(b => b.toString(16).padStart(2, '0')).join(''),
        };
    });
    expect(xy.sources === 'n15,0,n15,1', 'the PWM\'s sources are ' + xy.sources);
    expect(xy.wires === 'n15:X,n15:Y', 'the PWM\'s wires are ' + xy.wires);
    expect(xy.marshal === '22010011' + '11' + '000f00' + '02' + '03e8' + '03' + '320064' + '000f01' + '00642710',
        'the PWM marshals as ' + xy.marshal);

    // Add a light, and wire the momentary button to it. A light takes a button,
    // not a slider.
    await page.locator('#add .group[data-group="uio"]').dispatchEvent('click');
    await page.locator('#add [title="Add a light"]').dispatchEvent('click');
    const light = await page.evaluate(() => {
        const light = elements.n18;
        const unwired = light.on;
        return {
            unwired,
            fromSlider: createEdge('n5', 'n18') === null,
            fromButton: createEdge('n13', 'n18') !== null && light.sourceID() === 13,
            wired: light.on,
            marshal: light.marshal().map(b => b.toString(16).padStart(2, '0')).join(''),
        };
    });
    expect(light.unwired === null && light.fromSlider && light.fromButton && light.wired === false,
        'the light was wired as ' + JSON.stringify(light));
    expect(light.marshal === '01010012' + '03' + '000d00', 'the light marshals as ' + light.marshal);

    // Add a GPIO input on GPIO 8, pulled up and on while low, and a second light
    // for it. A GPIO input is boolean: it drives a light, not a PWM.
    await page.locator('#add .group[data-group="hwi"]').dispatchEvent('click');
    await page.locator('#add [title="Add a GPIO input"]').dispatchEvent('click');
    expect(await keyRows() === 'Label:|GPIO pin:|Pull:|On when:|Command view:', 'a GPIO input does not show its settings: ' + await keyRows());
    // Its default, GPIO 3, is free; the pins of the GPO (7), the PWMs (15 and 2)
    // and the ADC (ADC1, GPIO 27) are not offered.
    const gpiPins = await pinOptions();
    expect(await page.evaluate(() => elements.n19.pin) === 3
        && gpiPins === [...Array(23).keys(), 26, 28].filter(p => ![2, 7, 15].includes(p)).join(),
        'the GPIO input offers GPIO pins ' + gpiPins);
    await select('GPIO pin').selectOption('8');
    await choice('Pull', 'Up');
    await choice('On when', 'Low');
    await choice('Command view', 'Show');
    await page.locator('#add .group[data-group="uio"]').dispatchEvent('click');
    await page.locator('#add [title="Add a light"]').dispatchEvent('click');
    const gpi = await page.evaluate(() => {
        const [input, light] = [elements.n19, elements.n20];
        light.setLabel('Door');
        return {
            settings: [input.pin, input.pull, input.invert].join(),
            rejected: input.setPin('23') === false && input.setPull(3) === false && input.setActive('x') === false,
            // A new node takes its default pin if free, else the lowest free one.
            free: [freePin(null, 4), freePin(null, 7), freePin(null, 2)].join(),
            toPwm: createEdge('n19', 'n4') === null,
            toLight: createEdge('n19', 'n20') !== null && light.sourceID() === 19 && light.on === null,
            marshal: input.marshal().map(b => b.toString(16).padStart(2, '0')).join(''),
        };
    });
    expect(gpi.settings === '8,1,true' && gpi.rejected, 'the GPIO input settings gave ' + JSON.stringify(gpi));
    expect(gpi.free === '4,0,0', 'free pins are ' + JSON.stringify(gpi));
    expect(gpi.toPwm, 'a GPIO input was wired to a PWM');
    expect(gpi.toLight, 'the GPIO input was not wired to the light, or the light shows a state before any reading');
    expect(gpi.marshal === '31020013' + '02' + '0805', 'the GPIO input marshals as ' + gpi.marshal);

    // A plot shows its source's unit, unless it is given its own.
    const units = await page.evaluate(() => {
        const [speed, temp, level] = [elements.n7, elements.n9, elements.n11];
        const auto = [speed.unit(), temp.unit(), level.unit()].join('|');
        const tooLong = speed.setUnit('x'.repeat(17)) === false;
        speed.setUnit('rpm');
        level.setUnit('mV');
        level.setUnit('auto');
        selectById(9);
        const shown = [...document.querySelectorAll('#settings .editable')]
            .find(row => row.querySelector('.label').textContent === 'Unit:').querySelector('.value').textContent;
        return { auto, tooLong, set: [speed.unit(), level.unit(), level.unitOverride].join('|'), shown };
    });
    expect(units.auto === '|°C|V', 'the plots\' units from their sources are ' + units.auto);
    expect(units.tooLong, 'a unit over 16 bytes was accepted');
    expect(units.set === 'rpm|V|', 'setting the units gave ' + units.set);
    expect(units.shown === 'Auto (°C)', 'the Unit setting shows ' + units.shown);

    await page.locator('#graph .write svg').dispatchEvent('click');
    const saves = await sent(page);
    expect(saves.length === 1 && saves[0][0] === 0x0A, 'saving did not send one config packet');
    const saved = saves[0];
    packets.push(...saves);

    // ── Press the button in the Command view, twice ──
    const button = page.locator('#command .control-btn').first();
    // Read once the light's colour transition has finished.
    const state = () => page.waitForTimeout(300).then(() => button.evaluate(b => b.getAttribute('aria-pressed') + ' ' + b.querySelector('.state').textContent
        + ' ' + getComputedStyle(b.querySelector('.light')).backgroundColor));
    expect(await state() === 'false Off rgb(0, 0, 0)', 'the button does not start off: ' + await state());
    await button.dispatchEvent('click');
    expect(await state() === 'true On rgb(75, 172, 79)', 'the button does not show on, with a green light: ' + await state());
    await button.dispatchEvent('click');
    expect(await state() === 'false Off rgb(0, 0, 0)', 'the button does not show off again: ' + await state());
    const commands = await sent(page);
    expect(commands.length === 2 && commands.every(p => p[0] === 0x01), 'pressing the button did not send two commands');
    expect(commands[0][8] === 0xFF && commands[1][8] === 0x00, 'the button did not send 1 then 0');
    packets.push(...commands);

    // ── Hold the momentary button: on while held, with the mouse, then Space ──
    await toCommandView(page);
    await page.waitForTimeout(500);     // the view slides in
    const held = page.locator('#command .control-btn').nth(1);
    const box = await held.boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    const lamp = () => page.locator('#command .control-light').first().evaluate(l => l.dataset.on + ' ' + l.querySelector('.state').textContent);
    expect(await lamp() === 'false Off', 'the light does not start off: ' + await lamp());
    await page.mouse.down();
    expect(await held.getAttribute('aria-pressed') === 'true', 'the momentary button is not on while held');
    expect(await lamp() === 'true On', 'the light is not on while the button is held: ' + await lamp());
    await page.mouse.up();
    expect(await held.getAttribute('aria-pressed') === 'false', 'the momentary button is not off when let go');
    expect(await lamp() === 'false Off', 'the light is not off when the button is let go: ' + await lamp());
    await held.press(' ');
    const holds = await sent(page);
    expect(holds.map(hex).join() === ['0100061000000d01ff', '0100061000000d0100', '0100061000000d01ff', '0100061000000d0100'].join(),
        'holding the momentary button sent ' + holds.map(hex));
    packets.push(...holds);

    // ── Move the slider: 0.75, the same again (no command), then 0 ──
    const slider = page.locator('#command .control-slider input').first();
    expect(await page.locator('#command .control-slider.vertical').count() === 1, 'the Command view has no vertical slider');
    const move = (v) => slider.evaluate((el, v) => { el.value = v; el.dispatchEvent(new Event('input')); }, v);
    await move('191');
    expect(await page.locator('#command .control-slider .state').first().textContent() === '0.75',
        'the slider does not show 0.75');
    await move('191');
    await move('0');
    const moves = await sent(page);
    expect(moves.length === 2 && hex(moves[0]) === '010006' + '1200000501bf' && moves[1][8] === 0x00,
        'moving the slider did not send 0.75 then 0: ' + moves.map(hex));
    expect(await page.locator('#command .control-slider .state').first().textContent() === '0.00', 'the slider does not show its value');
    packets.push(...moves);

    // The plot took each value the slider sent, after the 0 it started from, and shows the latest.
    const trace = await page.evaluate(() => elements.n7.samples.map(([, v]) => v.toFixed(2)).join());
    expect(trace === '0.00,0.75,0.00', 'the plot did not follow the slider: ' + trace);
    expect(await page.locator('#command .control-plot canvas').count() === 4, 'the Command view has no plot');
    await toCommandView(page);
    expect(await page.locator('#command .control-plot .state').first().textContent() === '0 rpm', 'the plot does not show its value and unit');

    // A plot takes any number, not only 0 to 1, and its axis fits what it shows.
    const anyRange = await page.evaluate(() => {
        const axis = (values) => { const a = plotAxis(values); return a.ticks.map(v => a.format(v)).join(' '); };
        elements.n5.emit(-250);
        elements.n5.emit(1200.5);
        elements.n5.emit(NaN);
        return {
            samples: elements.n7.samples.slice(-3).map(([, v]) => v).join(),
            wide: axis([-250, 1200.5]),
            flat: axis([3]),
            none: axis([]),
        };
    });
    expect(anyRange.samples === '-250,1200.5,', 'the plot did not take any number: ' + anyRange.samples);
    expect(anyRange.wide === '-500 0 500 1000 1500', 'the axis for -250 to 1200.5 is ' + anyRange.wide);
    expect(anyRange.flat === '2.6 2.8 3.0 3.2 3.4', 'the axis for a flat 3 is ' + anyRange.flat);
    expect(anyRange.none === '0.0 0.5 1.0', 'the axis with no values is ' + anyRange.none);

    // ── Drag the 2D slider: from the middle out past its top-right corner (1, 1),
    // and let go: it returns home. Then nudge it right with a key. ──
    const padEl = page.locator('#command .control-pad .pad');
    expect(await page.locator('#command .control-pad .state').textContent() === '0.25, 0.75', 'the 2D slider does not start at home');
    const padBox = await padEl.boundingBox();
    await page.mouse.move(padBox.x + padBox.width / 2, padBox.y + padBox.height / 2);
    await page.mouse.down();
    await page.mouse.move(padBox.x + padBox.width + 40, padBox.y - 40);
    const corner = await page.locator('#command .control-pad .state').textContent();
    await page.mouse.up();
    await padEl.press('ArrowRight');
    const drags = await sent(page);
    const values = drags.map(p => hex(p).slice(0, 16) === '0100071300000f02' ? hex(p).slice(16) : hex(p));
    expect(corner === '1.00, 1.00', 'dragging past the corner shows ' + corner);
    expect(values.length === 5 && values.slice(1).join() === 'ffff,40bf,41bf,40bf',
        'dragging and nudging the 2D slider sent ' + values);
    expect(await page.locator('#command .control-pad .state').textContent() === '0.25, 0.75', 'the 2D slider did not return home');
    packets.push(...drags);
    const yTrace = await page.evaluate(() => elements.n16.samples.slice(-4).map(([, v]) => v.toFixed(2)).join());
    expect(yTrace === '1.00,0.75,0.75,0.75', 'the plot did not follow Y: ' + yTrace);

    // A linear or sigmoid return takes it back home in steps, each nearer than
    // the last. Linear steps are even; sigmoid ones grow, then shrink.
    for (const ease of ['linear', 'sigmoid']) {
        await page.evaluate((ease) => { elements.n15.setReturnEase(ease); elements.n15.setReturnSpeed('100'); }, ease);
        await page.mouse.move(padBox.x + padBox.width / 2, padBox.y + padBox.height / 2);
        await page.mouse.down();
        await page.mouse.move(padBox.x - 40, padBox.y + padBox.height + 40);     // past the bottom-left corner: 0, 0
        const released = await page.evaluate(() => performance.now());
        await page.mouse.up();
        await page.waitForFunction(() => elements.n15.values.every((v, i) => v === elements.n15.home[i] / VALUE_FULL));
        const took = await page.evaluate((t) => performance.now() - t, released);
        const glides = await sent(page);
        const glide = glides.map(p => [p[8], p[9]]);
        const back = glide.slice(glide.findIndex(([x, y]) => x === 0 && y === 0));
        const nearer = back.every(([x, y], i) => i === 0 || (x >= back[i - 1][0] && y >= back[i - 1][1]));
        expect(back.length > 3 && nearer && back.at(-1).join() === '64,191',
            `the ${ease} return sent ` + JSON.stringify(glide));
        // From the corner to (0.25, 0.75) at 100 %/s: about 0.8 s, whatever the shape.
        expect(took > 500 && took < 1500, `the ${ease} return took ${took} ms`);
        const third = Math.floor(back.length / 3);
        const gain = (a, b) => back[b][1] - back[a][1];      // Y, the longer way
        const [start, middle, end] = [gain(0, third), gain(third, 2 * third), gain(2 * third, back.length - 1)];
        if (ease === 'sigmoid') expect(middle > start && middle > end, `the sigmoid return moved ${start}, ${middle}, ${end} by thirds`);
        packets.push(...glides);
    }

    // The device's temperature telemetry reaches the core node, its tile and its plot.
    await page.evaluate(() => window.__ws.receive('sig 8 0 31.27'));
    const temperature = await page.evaluate(() => ({
        tile: document.querySelector('#command .control-reading .state').textContent,
        plot: elements.n9.samples.map(([, v]) => v).join(),
    }));
    expect(temperature.tile === '31.3 °C', 'the core temperature tile shows ' + temperature.tile);
    expect(temperature.plot === ',31.27', 'the temperature plot holds ' + temperature.plot);

    // A signal reaches its own node only: the ADC n10's, in volts. A signal of a
    // node the page doesn't have is ignored.
    await page.evaluate(() => { window.__ws.receive('sig 99 0 3.3'); window.__ws.receive('sig 10 0 1.6504'); });
    const reading = await page.evaluate(() => ({
        tile: document.querySelectorAll('#command .control-reading .state')[1].textContent,
        plot: elements.n11.samples.map(([, v]) => v === null ? '' : v.toFixed(4)).join(),
    }));
    expect(reading.tile === '1.65 V', 'the ADC tile shows ' + reading.tile);

    // A GPIO input's signal reaches it, its tile and its light.
    const door = () => page.evaluate(() => [
        [...document.querySelectorAll('#command .control-reading')].find(t => t.title.startsWith('GPIO input')).querySelector('.state').textContent,
        document.querySelectorAll('#command .control-light')[1].querySelector('.state').textContent,
        document.querySelectorAll('#command .control-light')[1].dataset.on].join(' '));
    expect(await door() === '– – false', 'before any reading the GPIO input and its light show ' + await door());
    await page.evaluate(() => { window.__ws.receive('sig 19 0 1'); window.__ws.receive('sig 20 0 0'); });
    expect(await door() === 'On On true', 'pin 8 on shows ' + await door());
    await page.evaluate(() => window.__ws.receive('sig 19 0 0'));
    expect(await door() === 'Off Off false', 'pin 8 off shows ' + await door());
    expect(reading.plot === ',1.6504', 'the ADC plot holds ' + reading.plot);
    await toCommandView(page);
    const plotStates = await page.$$eval('#command .control-plot .state', ss => ss.map(s => s.textContent).join('|'));
    expect(plotStates.endsWith('|31.27 °C|1.65 V|0.749'), 'the plots show ' + plotStates);

    // ── Network fields: both options, then name, SSID and password ──
    for (const nth of [0, 1]) {
        await page.locator('.netconn-buttons > div').nth(nth).dispatchEvent('click');
    }
    for (const [field, value] of [['name', 'Home'], ['ssid', 'MyWiFi'], ['password', 'secret12']]) {
        const row = page.locator('.editable', { has: page.locator(`text=${field === 'ssid' ? 'SSID' : field[0].toUpperCase() + field.slice(1)}:`) });
        await row.locator('svg').first().dispatchEvent('click');      // edit
        await page.locator(`#input-0-${field}`).fill(value);
        await page.locator('.editable', { has: page.locator(`#input-0-${field}`) }).locator('svg').first().dispatchEvent('click');   // confirm
    }
    const updates = await sent(page);
    expect(updates.length === 5, `expected 5 field updates, got ${updates.length}`);
    expect(updates.filter(p => p[0] === 0x0B).length === 4 && updates.filter(p => p[0] === 0x0C).length === 1,
        'field updates are not 4 config and 1 secret');
    packets.push(...updates);

    // ── Addresses: joining, a fixed address and its gateway; as an access point,
    // its address, refusing one its DHCP server leases. The first-boot config has
    // none of them, so the device adds each. ──
    const editField = async (label, field, value) => {
        await page.locator('.editable', { has: page.locator(`text=${label}:`) }).locator('svg').first().dispatchEvent('click');
        await page.locator(`#input-0-${field}`).fill(value);
        await page.locator('.editable', { has: page.locator(`#input-0-${field}`) }).locator('svg').first().dispatchEvent('click');
    };
    expect(await page.locator('.editable', { has: page.locator('text=Gateway:') }).count() === 0,
        'the gateway shows without a fixed address');
    await editField('Static IP', 'staticAddress', ' 192.168.1.50 ');
    await editField('Gateway', 'gateway', '192.168.1.1');
    await page.locator('.netconn-buttons > div').nth(0).dispatchEvent('click');
    expect(await page.locator('.editable .value', { hasText: '192.168.4.1 (default)' }).count() === 1,
        'the access point does not show its default address');
    await editField('IP address', 'apAddress', '192.168.4.20');
    const refused = await page.locator('#input-0-apAddress').getAttribute('aria-invalid');
    await page.locator('#input-0-apAddress').fill('10.0.0.1');
    await page.locator('.editable', { has: page.locator('#input-0-apAddress') }).locator('svg').first().dispatchEvent('click');
    const addresses = await sent(page);
    expect(refused === 'true', 'an address the access point leases was not refused');
    expect(addresses.map(hex).join() === [
        '0b000ab300000005' + '05c0a80132',     // fixed address 192.168.1.50
        '0b000ab300000005' + '06c0a80101',     // gateway 192.168.1.1
        '0b0007b200000002' + '0000',           // access point
        '0b000ab300000005' + '040a000001',     // access point address 10.0.0.1
    ].join(), 'the address updates were ' + addresses.map(hex));
    packets.push(...addresses);
    expect(await page.locator('.editable .value', { hasText: '10.0.0.1' }).count() === 1,
        'the access point does not show its new address');

    // Saving the graph sends the networking messages back: with the updates in them.
    await page.locator('#graph .write svg').dispatchEvent('click');
    const withAddresses = await sent(page);
    const fields = hex(withAddresses[0]);
    expect(withAddresses.length === 1 && fields.includes('b300000005' + '05c0a80132')
        && fields.includes('b300000005' + '06c0a80101') && fields.includes('b300000005' + '040a000001')
        && fields.includes('b2000000020000'), 'saving the graph lost the network updates: ' + fields);
    packets.push(...withAddresses);

    // Wires join only matching types: a button (boolean) drives an LED or GPO, a
    // slider (floating) a PWM.
    const types = await page.evaluate(() => {
        const pwm = new uiPWM(400, 300);      // n21, no wires yet
        new uiButton(-400, 300);              // n22
        // Its pin is free and on a slice no other PWM uses: not the slices of
        // the PWMs on GPIO 15 (14, 15) and GPIO 2 (2, 3, 18, 19).
        selectById(21);
        const pwmPins = pwm.pin + '|' + [...[...document.querySelectorAll('#settings .editable.select')]
            .find(row => row.querySelector('.label').textContent === 'GPIO pin:')
            .querySelectorAll('option')].map(o => o.value).join();
        clearSelection();
        const marked = () => Object.values(elements).filter(e => e.marked).map(e => e.name).join();
        toggleConnectMode();
        selectById(21);
        const fromPwm = marked();
        clearSelection();
        toggleConnectMode();
        selectById(21);
        pwm.setMode('duty', 'input');
        pwm.createSettings();
        const input = [...document.querySelectorAll('#settings .editable.select')]
            .find(row => row.querySelector('.label').textContent === 'Input:');
        const picks = [...input.querySelectorAll('option')].map(o => o.value).join();
        return {
            pwmPins,
            fromPwm,
            picks,
            sliderToLed: createEdge('n5', 'n2') === null && elements.n2.inConns()[0].start.node.name === 'n1',
            buttonToPwm: createEdge('n22', 'n21') === null && pwm.setSource('duty', elements.n22) === false
                && pwm.inConns().length === 0,
        };
    });
    // Less those other nodes use: the GPO's 7, the GPIO input's 8 and the ADC's 27.
    expect(types.pwmPins === '0|' + [...Array(23).keys(), 26, 28].filter(p => ![2, 3, 7, 8, 14, 15, 18, 19].includes(p)).join(),
        'a new PWM, with PWMs on GPIO 15 and 2, takes and offers ' + types.pwmPins);
    expect(types.fromPwm === 'n5,n6,n8,n10,n15', 'wire mode from a PWM marks ' + types.fromPwm);
    expect(types.picks === ',n5:0,n6:0,n8:0,n10:0,n15:0,n15:1', 'the PWM offers inputs ' + types.picks);
    expect(types.sliderToLed, 'a slider was wired to the LED');
    expect(types.buttonToPwm, 'a button was wired to a PWM');

    // A wire into an output that has one replaces it, on both nodes.
    const replaced = await page.evaluate(() => {
        createEdge('n22', 'n2');
        return {
            led: elements.n2.inConns()[0].start.node.name,
            button: elements.n1.outputs[0].conns.map(c => c.end.node.name).join(),
            wires: gEdges.filter(e => e.end.node === elements.n2).length,
        };
    });
    expect(replaced.led === 'n22' && replaced.button === 'n3,n14' && replaced.wires === 1,
        'a second wire into the LED did not replace the first: ' + JSON.stringify(replaced));
    // An ADC channel is a pin too: with a GPO on GPIO 26, the ADC (on ADC1) is
    // not offered ADC0.
    const channelsLeft = await page.evaluate(() => {
        const gpo = new uiGPO(400, 400);
        gpo.setPin('26');
        selectById(10);
        const offered = [...document.querySelectorAll('#settings .editable.select option')].map(o => o.value).join();
        gpo.remove();
        clearSelection();
        return offered;
    });
    expect(channelsLeft === '1,2', 'with GPIO 26 in use the ADC offers channels ' + channelsLeft);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // ── Reconnect: the device sends the saved config back ──
    ({ page, errors } = await open(browser));
    await connect(page, saved);
    const restored = await page.evaluate(() => ({
        labels: Object.values(elements).map(e => e.label),
        pin: elements.n3.pin,
        pwm: [elements.n4.pin, elements.n4.freq, elements.n4.dutyFromInput, elements.n4.duty,
            elements.n4.dutyMin, elements.n4.dutyMax, elements.n4.freqFromInput,
            elements.n4.freqMin, elements.n4.freqMax, elements.n4.freqLog].join(),
        vertical: elements.n5.vertical,
        plot: [elements.n7.window, elements.n7.sourceID(), elements.n9.sourceID(), elements.n11.sourceID()].join(),
        adc: elements.n10.channel,
        units: [elements.n7.unit(), elements.n7.unitOverride, elements.n9.unit(), elements.n9.unitOverride, elements.n11.unit()].join('|'),
        plots: document.querySelectorAll('#command .control-plot').length,
        readings: document.querySelectorAll('#command .control-reading').length,
        wires: gEdges.length,
        pwmSources: [elements.n4.dutySource?.name, elements.n4.freqSource?.name].join(),
        buttons: document.querySelectorAll('#command .control-btn').length,
        keyboards: [elements.n12.describe(), elements.n12.sourceID(), elements.n13.momentary, elements.n1.momentary,
            JSON.stringify(elements.n14.text), elements.n14.sourceID()].join(),
        pad: [elements.n15.home.join(':'), elements.n15.returns, elements.n15.returnEase, elements.n15.returnSpeed, elements.n16.sourceRef().join(':'), elements.n17.dutySource?.name, elements.n17.dutyOutput,
            elements.n17.freqSource?.name, elements.n17.freqOutput, elements.n17.inConns().map(c => c.outputName.textContent).join('')].join(),
        pads: document.querySelectorAll('#command .control-pad').length,
        light: [elements.n18.sourceID(), elements.n18.on, document.querySelectorAll('#command .control-light').length].join(),
        gpi: [elements.n19.pin, elements.n19.pull, elements.n19.invert, elements.n20.sourceID()].join(),
    }));
    expect(restored.labels.join('|') === 'Start|Pompe à eau|GPO|PWM|Speed|Pitch|Speed plot|Core temperature|Temperature|Level sensor|Level|Lock|Press|Greeting|Stick|Stick Y|Motor|Light|GPIO input|Door', 'labels not restored: ' + restored.labels);
    expect(restored.pin === 7 && restored.pwm === '15,20000,true,75,20,80,true,200,2000,true' && restored.vertical && restored.wires === 14
        && restored.plot === '20,5,8,10' && restored.adc === 1 && restored.units === 'rpm|rpm|°C||V' && restored.plots === 4 && restored.readings === 3
        && restored.pad === '64:191,true,immediate,120,0:15:1,n15,0,n15,1,XY' && restored.pads === 1 && restored.light === '13,false,2' && restored.gpi === '8,1,true,19'
        && restored.pwmSources === 'n5,n6'
        && restored.buttons === 2
        && restored.keyboards === 'Ctrl+Alt+Delete,13,true,false,"hello\\n",1',
        'graph not restored: ' + JSON.stringify(restored));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const resaved = await sent(page);
    expect(resaved.length === 1 && hex(resaved[0]) === hex(saved), 'saving the restored graph changed the config');
    packets.push(...resaved);

    // Hiding a hardware input takes its tile out of the Command view at once.
    const tilesShown = await page.evaluate(() => [elements.n8.shown, elements.n10.shown, elements.n19.shown].join());
    const tiles = () => page.evaluate(() => document.querySelectorAll('#command .control-reading').length);
    await page.evaluate(() => selectById(10));
    await page.locator('#settings .choice', { has: page.locator('.label:text-is("Command view:")') })
        .locator('button', { hasText: 'Hide' }).dispatchEvent('click');
    expect(tilesShown === 'true,true,true' && await tiles() === 2 && await page.evaluate(() => elements.n10.shown === false),
        `restored shown ${tilesShown}; hiding the ADC left ${await tiles()} reading tiles`);
    await page.evaluate(() => clearSelection());

    // A config with addresses shows them; one without shows none, not the last one's.
    const shown = await page.evaluate(([cfg, firstBoot]) => {
        const read = () => [networks[0].options, networks[0].apAddress, networks[0].staticAddress, networks[0].gateway].join('|');
        unmarshalConfigs(Uint8Array.from(cfg).slice(3));
        const withAddresses = read();
        unmarshalConfigs(Uint8Array.from(firstBoot).slice(3));
        return [withAddresses, read()];
    }, [withAddresses[0], FIRST_BOOT_CONFIG]);
    expect(shown[0] === '0|10.0.0.1|192.168.1.50|192.168.1.1' && shown[1] === '1|||',
        'the addresses read from configs are ' + JSON.stringify(shown));

    // A config saved before wire types were checked may wire a slider to an LED or a
    // button to a PWM: restoring it drops those wires and keeps the matching one.
    const mixed = await page.evaluate(() => {
        restoreGraph(configMessages(Uint8Array.from([
            0x10, 0, 0, 1, 0,                                  // button n1
            0x12, 0, 0, 2, 1, 0,                               // slider n2
            0x20, 0, 0, 3, 2, 0, 2,                            // LED n3 ← slider n2
            0x22, 0, 0, 4, 15, 0, 1, 2, 0x03, 0xE8, 0x03,      // PWM n4: duty ← button n1,
            50, 0, 100, 0, 2, 0, 100, 0x27, 0x10,              //   frequency ← slider n2
        ])));
        return gEdges.map(e => e.start.node.name + '>' + e.end.node.name).join()
            + '|' + [elements.n4.dutySource?.name, elements.n4.freqSource?.name].join();
    });
    expect(mixed === 'n2>n4|,n2', 'restoring mismatched wires gave ' + mixed);

    // ── The connection switch: on while connected, off with no message when
    // switched off, pulsing while connecting, and a message when it fails or drops ──
    await toNetworkView(page);
    const toggle = page.locator('.device-connect');
    const switchState = () => page.evaluate(() => [
        document.querySelector('.device-connect').className.replace('device-connect ', ''),
        document.querySelector('.device-message')?.textContent ?? ''].join('|'));
    expect(await switchState() === 'connected|', 'the switch is not on while connected: ' + await switchState());
    await toggle.dispatchEvent('click');
    expect(await switchState() === 'disconnected|', 'switching off left ' + await switchState());
    await page.evaluate(() => { window.__wsRefuse = true; });
    await toggle.dispatchEvent('click');
    const connecting = await switchState();
    const pulse = await page.evaluate(() => getComputedStyle(document.querySelector('.device-connect .light')).animationName);
    await page.waitForFunction(() => document.querySelector('.device-connect.disconnected'));
    const unreachable = await switchState();
    expect(connecting === 'connecting|' && pulse === 'connecting-pulse', `connecting shows ${connecting}, animation ${pulse}`);
    expect(unreachable.startsWith("disconnected|Couldn't connect to "), 'an unreachable device shows ' + unreachable);
    await page.evaluate(() => { window.__wsRefuse = false; });
    await toggle.dispatchEvent('click');
    await page.waitForFunction(() => document.querySelector('.device-connect.connected'));
    expect(await switchState() === 'connected|', 'connecting again did not clear the message: ' + await switchState());
    await page.evaluate(() => window.__ws.close());       // the device goes away
    expect((await switchState()).startsWith('disconnected|Lost the connection to '), 'a dropped connection shows ' + await switchState());
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await page.close();

    // ── Logic: the core temperature above 40 °C, and a switch, run a fan on the
    // GPO through a compare, an AND and a delay; a NOT lights the LED while it is off ──
    ({ page, errors } = await open(browser));
    await connect(page, FIRST_BOOT_CONFIG);          // the demo graph: button n1, LED n2 ← n1, GPO n3
    await page.locator('#add .toggle').dispatchEvent('click');
    await page.locator('#add .group[data-group="lgc"]').dispatchEvent('click');
    await page.locator('#add [title="Add a compare"]').dispatchEvent('click');       // n4
    const compareRows = await page.$$eval('#settings .editable .label', ls => ls.map(l => l.textContent).join('|'));
    expect(compareRows === 'Label:|On while:|Threshold:|Hysteresis:|Command view:', 'a compare\'s settings are ' + compareRows);
    const logic = await page.evaluate(() => {
        const cmp = elements.n4;
        const rejected = cmp.setThreshold('x') === false && cmp.setThreshold('1e39') === false
            && cmp.setHysteresis('-1') === false && cmp.setCondition('over') === false;
        addNode(MSG_HW_CORE);                       // n5
        addNode(MSG_HW_GPI);                        // n6, GPIO 3
        addNode(MSG_LOGIC_AND);                     // n7
        addNode(MSG_LOGIC_DELAY);                   // n8
        addNode(MSG_LOGIC_NOT);                     // n9
        addNode(MSG_UI_INDICATOR);                  // n10
        cmp.setThreshold('40');
        cmp.setHysteresis('1');
        cmp.shown = true;
        const delay = elements.n8;
        const delays = delay.setOffDelay('86401') === false && delay.setOnDelay('0.0005') === false
            && delay.setOffDelay('5') !== false;
        const wired = [
            createEdge('n5', 'n4'),                 // temperature → compare
            createEdge('n4', 'n7'),                 // logic to logic: from the first to the second
            createEdge('n6', 'n7'),                 // the switch → AND
            createEdge('n7', 'n8'),                 // AND → delay
            createEdge('n8', 'n9'),                 // delay → NOT
            createEdge('n8', 'n3'),                 // delay → GPO
            createEdge('n9', 'n2'),                 // NOT → LED, in place of the button
            createEdge('n4', 'n10'),                // compare → light
        ].every(e => e !== null);
        buildCommandInterface();
        return {
            rejected, delays, wired,
            text: cmp.gElm.children[1].textContent,
            and: elements.n7.inConns().map(c => c.start.node.name).join(),
            led: elements.n2.inConns().map(c => c.start.node.name).join(),
            // A wire back up the chain closes a loop through the delay, which holds a
            // state: offered (as a feedback wire). Not drawn here; see the loop below.
            loop: elements.n7.canTakeConn(elements.n9)
                && createEdge('n8', 'n4') === null && createEdge('n4', 'n4') === null,
            // A slider is floating: it drives a compare, not an AND; a compare
            // takes no boolean source.
            types: createEdge('n1', 'n4') === null && elements.n7.takesType(elements.n4)
                && !elements.n4.takesType(elements.n6),
            free: new uiOr(0, 300).canTakeConn(elements.n9),
        };
    });
    expect(logic.rejected && logic.delays, 'logic settings out of range were accepted: ' + JSON.stringify(logic));
    expect(logic.wired && logic.text === '> 40' && logic.and === 'n4,n6' && logic.led === 'n9',
        'the logic was not wired: ' + JSON.stringify(logic));
    expect(logic.loop, 'a wire back through a delay was not offered, or a wrong one was drawn');
    expect(logic.types, 'logic took a source of the wrong type');
    expect(logic.free, 'a new OR does not take the NOT');
    await page.evaluate(() => elements.n11.remove());

    // Not saved yet, so the device doesn't run it: the browser does, from the
    // values its sources have in the browser. A dashed dot marks it.
    const chain = () => page.evaluate(() => [elements.n4, elements.n7, elements.n8, elements.n9]
        .map(e => e.value ? 1 : 0).join('') + ',' + elements.n10.on + ',' + elements.n4.gElm.children[5].getAttribute('stroke-dasharray'));
    expect(await chain() === '0001,false,2 2', 'with no readings the logic shows ' + await chain());
    await page.evaluate(() => window.__ws.receive('sig 5 0 45'));         // the core temperature, 45 °C
    expect(await chain() === '1001,true,2 2', 'at 45 °C the logic shows ' + await chain());
    await page.evaluate(() => window.__ws.receive('sig 6 0 1'));          // the switch on
    expect(await chain() === '1110,true,2 2', 'with the switch on the logic shows ' + await chain());
    await page.evaluate(() => window.__ws.receive('sig 5 0 39.5'));       // within the hysteresis
    expect(await chain() === '1110,true,2 2', 'at 39.5 °C the logic shows ' + await chain());
    await page.evaluate(() => { elements.n8.setOffDelay('0.2'); window.__ws.receive('sig 5 0 38.9'); });
    expect(await chain() === '0010,false,2 2', 'below 39 °C the delay did not hold on: ' + await chain());
    await page.waitForTimeout(300);
    expect(await chain() === '0001,false,2 2', 'the off delay did not end: ' + await chain());
    await page.evaluate(() => elements.n8.setOffDelay('5'));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const logicSaved = await sent(page);
    const logicHex = hex(logicSaved[0]);
    expect(logicSaved.length === 1
        && logicHex.includes('400300040c' + '000500' + '00' + '42200000' + '3f800000')
        && logicHex.includes('4101000706' + '000400' + '000600')
        && logicHex.includes('440100080b' + '000700' + '00000000' + '00001388')
        && logicHex.includes('4301000903' + '000800')
        && logicHex.includes('2101000304' + '000800' + '01')
        && logicHex.includes('2001000203' + '000900'),
        'the logic was not saved as expected: ' + logicHex);
    packets.push(...logicSaved);

    // The device sends each logic element's state: its node, its tile and the light wired to it show it.
    const compareState = () => page.evaluate(() => [elements.n4.value, elements.n10.on,
        elements.n4.gElm.children[5].getAttribute('fill'),
        [...document.querySelectorAll('#command .control-reading')].find(t => t.title === 'Compare')?.querySelector('.state').textContent].join());
    await page.evaluate(() => window.__ws.receive('sig 4 0 1'));
    expect(await compareState() === 'true,true,#4CAF50,On', 'the compare on shows ' + await compareState());
    await page.evaluate(() => window.__ws.receive('sig 4 0 0'));
    expect(await compareState() === 'false,false,none,Off', 'the compare off shows ' + await compareState());
    // While the device reports it, its state wins; once it goes quiet, the browser's.
    await page.evaluate(() => window.__ws.receive('sig 5 0 45'));
    expect(await compareState() === 'false,false,none,Off', 'the browser overrode the device: ' + await compareState());
    await page.waitForTimeout(2700);
    expect(await compareState() === 'true,true,#4CAF50,On', 'after the device went quiet the compare shows ' + await compareState());
    expect(await page.evaluate(() => elements.n4.gElm.children[5].getAttribute('stroke-dasharray')) === '2 2',
        'the browser\'s state is not marked');
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // Reconnecting restores the logic, its settings and wires; saving it again changes nothing.
    ({ page, errors } = await open(browser));
    await connect(page, logicSaved[0]);
    const logicRestored = await page.evaluate(() => ({
        compare: [elements.n4.below, elements.n4.threshold, elements.n4.hysteresis, elements.n4.shown, elements.n4.sourceID()].join(),
        and: elements.n7.inConns().map(c => c.start.node.name).join(),
        delay: [elements.n8.onMs, elements.n8.offMs, elements.n8.sourceID()].join(),
        not: elements.n9.sourceID(),
        outputs: [elements.n3.sourceID(), elements.n2.sourceID(), elements.n10.sourceID()].join(),
        wires: gEdges.length,
    }));
    expect(JSON.stringify(logicRestored) === JSON.stringify({
        compare: 'false,40,1,true,5', and: 'n4,n6', delay: '0,5000,7', not: 8, outputs: '8,9,4', wires: 8,
    }), 'the logic was not restored: ' + JSON.stringify(logicRestored));
    await page.locator('#graph .write svg').dispatchEvent('click');
    const logicResaved = await sent(page);
    expect(logicResaved.length === 1 && hex(logicResaved[0]) === logicHex, 'saving the restored logic changed the config');
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await page.close();

    // ── More logic: a scale for a fan's PWM, an over-temperature latch that blinks
    // the LED until reset, a toggle and a pulse from one button, and a max ──
    ({ page, errors } = await open(browser));
    await connect(page, FIRST_BOOT_CONFIG);          // button n1, LED n2 ← n1, GPO n3
    const more = await page.evaluate(() => {
        [MSG_HW_CORE, MSG_LOGIC_SCALE, MSG_HW_PWM, MSG_LOGIC_COMPARE, MSG_LOGIC_LATCH, MSG_LOGIC_BLINK,
         MSG_UI_BUTTON, MSG_LOGIC_TOGGLE, MSG_UI_BUTTON, MSG_LOGIC_MAX, MSG_HW_ADC, MSG_LOGIC_PULSE,
         MSG_UI_INDICATOR].forEach(addNode);        // n4 to n16
        const [scale, cmp, latch, blink, toggle, max, pulse] = ['n5', 'n7', 'n8', 'n9', 'n11', 'n13', 'n15'].map(n => elements[n]);
        const rejected = scale.setEnd(0, '1') === false && scale.setEnd(1, 'x') === false
            && pulse.setTime('0') === false && blink.setOffTime('0') === false;
        scale.setEnd(0, '30'); scale.setEnd(1, '60');
        cmp.setThreshold('50');
        blink.setOnTime('0.1'); blink.setOffTime('0.1');
        pulse.setTime('0.2');
        const wired = [
            createEdge('n4', 'n5'), createEdge('n5', 'n6'),       // temperature → scale → PWM duty
            createEdge('n4', 'n7'), createEdge('n7', 'n8'),       // → compare → latch: set
            createEdge('n10', 'n8'),                              // button → latch: reset
            createEdge('n8', 'n9'), createEdge('n9', 'n2'),       // latch → blink → LED
            createEdge('n12', 'n11'), createEdge('n11', 'n3'),    // button → toggle → GPO
            createEdge('n4', 'n13'), createEdge('n14', 'n13'),    // temperature, ADC → max
            createEdge('n12', 'n15'), createEdge('n15', 'n16'),   // button → pulse → light
        ].every(e => e !== null);
        return {
            rejected, wired,
            pwm: elements.n6.dutySource?.name,
            roles: [latch.setSource?.name, latch.resetSource?.name].join(),
            types: createEdge('n12', 'n5') === null && createEdge('n5', 'n8') === null    // a button into a scale; a scale into a latch
                && elements.n6.takesType(max) && !elements.n2.takesType(scale),
            unit: max.unit,
            outputs: [scale, max].map(n => n.outputs[0].type).join() + ',' + latch.outputs[0].type,
        };
    });
    expect(more.rejected, 'out-of-range settings were accepted');
    expect(more.wired && more.pwm === 'n5' && more.roles === 'n7,n10', 'the logic was not wired: ' + JSON.stringify(more));
    expect(more.types && more.unit === '°C' && more.outputs === 'float,float,bool', 'logic types are wrong: ' + JSON.stringify(more));

    // In the browser: 45 °C is half way from 30 to 60; the max picks it over the ADC's 1.2 V.
    const logicValues = () => page.evaluate(() => ['n5', 'n7', 'n8', 'n13'].map(n => elements[n].value).join());
    await page.evaluate(() => { window.__ws.receive('sig 4 0 45'); window.__ws.receive('sig 14 0 1.2'); });
    expect(await logicValues() === '0.5,false,false,45', 'at 45 °C: ' + await logicValues());
    // Above 50 °C the latch sets and the blink starts; back below, the latch holds.
    await page.evaluate(() => window.__ws.receive('sig 4 0 55'));
    expect(await logicValues() === '0.8333333333333334,true,true,55', 'at 55 °C: ' + await logicValues());
    await page.evaluate(() => window.__ws.receive('sig 4 0 20'));
    expect(await logicValues() === '0,false,true,20', 'at 20 °C the latch did not hold, or the scale did not clamp: ' + await logicValues());
    const blinks = await page.evaluate(async () => {
        const seen = [];
        for (let i = 0; i < 6; i++) { seen.push(elements.n9.value ? 1 : 0); await new Promise(r => setTimeout(r, 70)); }
        return seen.join('');
    });
    expect(/^1+0+1+/.test(blinks), 'the blink did not turn over: ' + blinks);
    // The reset button resets the latch, which stops the blink.
    await page.evaluate(() => { const b = elements.n10; b.value = true; b.emit(1); b.value = false; b.emit(0); });
    expect(await page.evaluate(() => [elements.n8.value, elements.n9.value].join()) === 'false,false', 'reset left the latch or blink on');
    // One button: the toggle turns over on each press; the pulse is on for 0.2 s from each.
    const press = () => page.evaluate(() => { const b = elements.n12; b.value = true; b.emit(1); b.value = false; b.emit(0);
        return [elements.n11.value, elements.n15.value, elements.n16.on].join(); });
    expect(await press() === 'true,true,true', 'the first press');
    await page.waitForTimeout(300);
    expect(await page.evaluate(() => [elements.n15.value, elements.n16.on].join()) === 'false,false', 'the pulse did not end');
    expect(await press() === 'false,true,true', 'the second press');
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const moreSaved = await sent(page);
    const moreHex = hex(moreSaved[0]);
    expect(moreSaved.length === 1
        && moreHex.includes('4501000514' + '000400' + '01' + '41f00000' + '42700000' + '00000000' + '3f800000')
        && moreHex.includes('4b01000806' + '000700' + '000a00')
        && moreHex.includes('490100090b' + '000800' + '00000064' + '00000064')
        && moreHex.includes('4a01000b03' + '000c00')
        && moreHex.includes('4701000d06' + '000400' + '000e00')
        && moreHex.includes('4801000f07' + '000c00' + '000000c8'),
        'the logic was not saved as expected: ' + moreHex);
    packets.push(...moreSaved);
    await page.close();

    // Reconnecting restores it all; saving it again changes nothing. A latch saved
    // with only a reset source keeps it as its reset.
    ({ page, errors } = await open(browser));
    await connect(page, moreSaved[0]);
    const moreRestored = await page.evaluate(() => ({
        scale: [...elements.n5.range, elements.n5.clamp, elements.n5.sourceID()].join(),
        latch: [elements.n8.setSource?.name, elements.n8.resetSource?.name].join(),
        times: [elements.n9.onMs, elements.n9.offMs, elements.n15.onMs].join(),
        max: elements.n13.inConns().map(c => c.start.node.name).join(),
        pwm: elements.n6.dutySource?.name,
    }));
    expect(JSON.stringify(moreRestored) === JSON.stringify({ scale: '30,60,0,1,true,4', latch: 'n7,n10', times: '100,100,200', max: 'n4,n14', pwm: 'n5' }),
        'the logic was not restored: ' + JSON.stringify(moreRestored));
    await page.locator('#graph .write svg').dispatchEvent('click');
    const moreResaved = await sent(page);
    expect(moreResaved.length === 1 && hex(moreResaved[0]) === moreHex, 'saving the restored logic changed the config');
    const resetOnly = await page.evaluate(() => {
        restoreGraph(configMessages(Uint8Array.from([
            0x10, 0, 0, 1, 0,                                   // button n1
            0x4B, 1, 0, 2, 6, 0, 0, 0, 0, 1, 0,                 // latch n2: no set, reset ← n1
        ])));
        return [elements.n2.setSource?.name, elements.n2.resetSource?.name].join();
    });
    expect(resetOnly === ',n1', 'a latch with only a reset source restored as ' + resetOnly);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // ── A loop: a button blinks the LED three times. The latch runs the blink; the
    // counter counts the blinks, and its done output, wired back to the latch's
    // reset, closes the loop as a feedback wire. The counter is reset while the
    // latch is off. A loop through nothing that holds a state isn't drawn. ──
    ({ page, errors } = await open(browser));
    await connect(page, FIRST_BOOT_CONFIG);          // button n1, LED n2 ← n1, GPO n3
    const loop = await page.evaluate(() => {
        [MSG_UI_BUTTON, MSG_LOGIC_LATCH, MSG_LOGIC_BLINK, MSG_LOGIC_COUNTER, MSG_LOGIC_NOT, MSG_UI_GRAPH,
         MSG_LOGIC_NOT, MSG_LOGIC_NOT].forEach(addNode);          // n4 to n11
        const [latch, blink, counter] = ['n5', 'n6', 'n7'].map(n => elements[n]);
        const rejected = counter.setPreset('0') === false && counter.setPreset('1.5') === false
            && counter.setPreset(String(COUNTER_MAX + 1)) === false;
        counter.setPreset('3');
        blink.setOnTime('0.05'); blink.setOffTime('0.05');
        const wired = [
            createEdge('n4', 'n5'),                 // button → latch: set
            createEdge('n5', 'n6'),                 // latch → blink
            createEdge('n6', 'n7'),                 // blink → counter: count
            createEdge('n5', 'n8'),                 // latch → NOT
            createEdge('n8', 'n7'),                 // NOT → counter: reset
            createEdge('n7', 'n5'),                 // counter done → latch: reset, closing the loop
            createEdge('n6', 'n2'),                 // blink → LED, in place of the button
            createEdge('n10', 'n11'),               // NOT → NOT
        ].every(e => e !== null);
        counter.setWireOutput(1);
        const count = createEdge('n7', 'n9');       // the count → plot
        return {
            rejected, wired,
            roles: [latch.setSource?.name, latch.resetSource?.name, counter.countSource?.name, counter.resetSource?.name].join(),
            feedback: gEdges.filter(e => e.feedback).map(e => e.start.node.name + '>' + e.end.node.name).join(),
            dashed: gEdges.find(e => e.feedback)?.path.getAttribute('stroke-dasharray'),
            count: count?.start.output === 1 && count.outputName?.textContent === 'Count',
            // The count is floating: a plot takes it, a GPO doesn't.
            types: createEdge('n7', 'n3') === null && counter.outputs.map(o => o.type).join() === 'bool,float',
            // NOT → NOT → back again would flip every scan: not offered, not drawn.
            stateless: !elements.n10.canTakeConn(elements.n11) && createEdge('n11', 'n10') === null,
        };
    });
    expect(loop.rejected, 'a counter preset out of range was accepted');
    expect(loop.wired && loop.roles === 'n4,n7,n6,n8' && loop.count && loop.types,
        'the loop was not wired: ' + JSON.stringify(loop));
    expect(loop.feedback === 'n7>n5' && loop.dashed, 'the wire closing the loop is not a feedback wire: ' + JSON.stringify(loop));
    expect(loop.stateless, 'a loop through nothing that holds a state was offered or drawn');

    // In the browser: a press blinks three times, then the loop stops and the counter is back at 0.
    const run = await page.evaluate(async () => {
        const blinks = [], counts = [];
        const emit = (node, seen, output) => {
            const base = node.emit.bind(node);
            node.emit = (v, o = 0) => { if (o === output) seen.push(Number(v)); base(v, o); };
        };
        emit(elements.n6, blinks, 0);
        emit(elements.n7, counts, 1);
        const b = elements.n4;
        b.value = true; b.emit(1); b.value = false; b.emit(0);
        await new Promise(r => setTimeout(r, 600));
        return { blinks: blinks.filter(v => v === 1).length, counts: counts.join(),
            after: [elements.n5.value, elements.n6.value, elements.n7.value, elements.n7.count].join() };
    });
    expect(run.blinks === 3 && run.counts === '1,2,3,0' && run.after === 'false,false,false,0',
        'the loop did not blink three times: ' + JSON.stringify(run));

    // The device reports the counter's two outputs: its node and tile show them.
    const reported = await page.evaluate(() => {
        elements.n7.shown = true;
        buildCommandInterface();
        window.__ws.receive('sig 7 0 0');
        window.__ws.receive('sig 7 1 2');
        return [elements.n7.count, elements.n7.gElm.children[1].textContent, elements.n7.tileState.textContent].join();
    });
    expect(reported === '2,2/3,2 of 3', 'the device\'s count was not shown: ' + reported);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const loopSaved = await sent(page);
    const loopHex = hex(loopSaved[0]);
    expect(loopSaved.length === 1
        && loopHex.includes('4b01000506' + '000400' + '000780')                     // latch: set ← n4, reset ← n7 fed back
        && loopHex.includes('4c0300070a' + '000600' + '000800' + '00000003')        // counter: count ← n6, reset ← n8, preset 3
        && loopHex.includes('0201000904' + '000701'),                               // plot ← n7's count
        'the loop was not saved as expected: ' + loopHex);
    packets.push(...loopSaved);
    await page.close();

    // Reconnecting restores the loop with its feedback wire; saving it again changes
    // nothing. Taking out a wire that the loop needs makes the feedback wire an
    // ordinary one.
    ({ page, errors } = await open(browser));
    await connect(page, loopSaved[0]);
    const loopRestored = await page.evaluate(() => ({
        feedback: gEdges.filter(e => e.feedback).map(e => e.start.node.name + '>' + e.end.node.name).join(),
        counter: [elements.n7.preset, elements.n7.countSource?.name, elements.n7.resetSource?.name].join(),
        plot: elements.n9.inConns().map(c => c.start.node.name + ':' + c.start.output).join(),
    }));
    expect(JSON.stringify(loopRestored) === JSON.stringify({ feedback: 'n7>n5', counter: '3,n6,n8', plot: 'n7:1' }),
        'the loop was not restored: ' + JSON.stringify(loopRestored));
    await page.locator('#graph .write svg').dispatchEvent('click');
    const loopResaved = await sent(page);
    expect(loopResaved.length === 1 && hex(loopResaved[0]) === loopHex, 'saving the restored loop changed the config');
    const broken = await page.evaluate(() => {
        const out = (from, to) => gEdges.find(e => e.start.node.name === from && e.end.node.name === to);
        out('n5', 'n6').remove();               // latch → blink: the loop still goes through the NOT
        const still = out('n7', 'n5').feedback;
        out('n8', 'n7').remove();               // NOT → counter: no loop left
        return [still, out('n7', 'n5').feedback, out('n7', 'n5').path.hasAttribute('stroke-dasharray')].join();
    });
    expect(broken === 'true,false,false', 'a feedback wire was not made ordinary once its loop was broken: ' + broken);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // ── Blocks: "Flip" turns over each time its input goes on, and counts how often.
    // Defined once, it is placed twice: the second copy flips on the first's output. ──
    ({ page, errors } = await open(browser));
    await connect(page, FIRST_BOOT_CONFIG);          // button n1, LED n2 ← n1, GPO n3
    await page.locator('#add .toggle').dispatchEvent('click');
    await page.locator('#add .group[data-group="sub"]').dispatchEvent('click');
    await page.locator('#add [title="Make a new block"]').dispatchEvent('click');    // copy n4 of Block 1
    await page.locator('#settings .choice button', { hasText: 'Edit block' }).dispatchEvent('click');
    const inBlock = await page.evaluate(() => {
        const shown = (sel) => [...document.querySelectorAll(sel)].filter(b => getComputedStyle(b).display !== 'none')
            .map(b => b.textContent.trim()).join(' ');
        toggleAddMenu();
        toggleAddGroup('sub');
        return { bar: document.querySelector('#block-bar').hidden ? '' : document.querySelector('#block-bar .name').textContent,
            groups: shown('#add .group'), sub: shown('#add .node.shown'),
            led: addNode(MSG_HW_LED) === null, nodes: Object.values(elements).map(e => e.label).join() };
    });
    expect(JSON.stringify(inBlock) === JSON.stringify({ bar: 'Block 1', groups: 'TM LGC SUB', sub: 'IN OUT', led: true, nodes: 'In 1,Out 1' }),
        'the block\'s view is not as expected: ' + JSON.stringify(inBlock));
    const defined = await page.evaluate(() => {
        const [inp, out] = Object.values(elements);
        const out2 = addNode(MSG_SUB_OUTPUT);
        out2.setType('float');
        const toggle = addNode(MSG_LOGIC_TOGGLE), counter = addNode(MSG_LOGIC_COUNTER);
        counter.setPreset('100');
        const wired = [createEdge(inp.name, toggle.name), createEdge(toggle.name, out.name),
            createEdge(inp.name, counter.name)].every(e => e);
        counter.setWireOutput(1);
        const typed = createEdge(counter.name, out.name) === null && createEdge(counter.name, out2.name) !== null;
        editingBlock.rename('Flip');
        return { wired, typed, ids: Object.values(elements).map(e => e.name).join() };
    });
    expect(defined.wired && defined.typed, 'the block was not defined: ' + JSON.stringify(defined));
    await page.locator('#block-bar .back').dispatchEvent('click');
    const placed = await page.evaluate(() => {
        const flip = elements.n4;
        const button = addNode(MSG_UI_BUTTON);
        button.setAction('momentary');
        const plot = addNode(MSG_UI_GRAPH);
        const flip2 = addBlock(flip.def);
        const light = addNode(MSG_UI_INDICATOR);
        window.t = { flip: flip.name, button: button.name, plot: plot.name, flip2: flip2.name, light: light.name };
        flip.setWireOutput(1);
        const wired = [createEdge(button.name, 'n4'), createEdge('n4', plot.name), createEdge('n4', flip2.name, 0),
            createEdge(flip2.name, light.name)].every(e => e) && new gEdge(flip, elements.n2, 0);
        return {
            t: window.t, label: flip.label + ',' + flip.text,
            ports: [flip.roles.map(r => r[1] + ':' + r[2]).join(), flip.outputs.map(o => o.name + ':' + o.type).join()].join(' | '),
            wired: !!wired, led: elements.n2.inConns().map(c => c.start.node.name + ':' + c.start.output).join(),
            types: createEdge('n4', 'n3') === null,     // a number into a GPO: no (output 1 is active)
            parts: [flip.partCount, flip2.partCount, flip.base !== flip2.base].join(),
        };
    });
    const t = placed.t;
    expect(placed.label === 'Flip,Flip' && placed.ports === 'In 1:bool | Out 1:bool,Out 2:float'
        && placed.wired && placed.led === 'n4:0' && placed.types && placed.parts === '2,2,true',
        'the block was not placed: ' + JSON.stringify(placed));

    // In the browser: each press flips the first copy and counts; the second copy flips
    // each time the first goes on.
    const flips = await page.evaluate(() => {
        const press = () => { const b = elements[t.button]; b.value = true; b.emit(1); b.value = false; b.emit(0);
            return [elements.n4.outs.join('/'), elements[t.flip2].outs[0], elements[t.light].on].join(); };
        return [press(), press(), press()].join(' ');
    });
    expect(flips === '1/1,1,true 0/2,1,true 1/3,0,false', 'the blocks did not flip: ' + flips);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const blockSaved = await sent(page);
    const blockInfo = await page.evaluate((bytes) => {
        const msgs = configMessages(Uint8Array.from(bytes).slice(3));
        const of = (type) => msgs.filter(m => m.type === type);
        return {
            def: of(MSG_SUB_DEF).map(m => m.id + ':' + new TextDecoder().decode(m.pay)).join(),
            wrapped: of(MSG_SUB_PART).length,
            copies: of(MSG_SUB).map(m => [m.id, ...m.pay.slice(0, 5)].join('.')).join(),
            parts: msgs.filter(m => m.flags & MSG_FLAG_PART).map(m => m.type.toString(16) + '@' + m.id).join(),
            bases: [elements.n4.base, elements[t.flip2].base].join(),
            led: [...of(MSG_HW_LED)[0].pay].join('.'),
        };
    }, blockSaved[0]);
    const [b4, b7] = blockInfo.bases.split(',').map(Number);
    const id7 = Number(t.flip2.slice(1));
    expect(blockSaved.length === 1 && blockInfo.def === '1:Flip' && blockInfo.wrapped > 0
        && blockInfo.copies === `4.1.${b4 >> 8}.${b4 & 255}.1.2,${id7}.1.${b7 >> 8}.${b7 & 255}.1.2`
        && blockInfo.parts === `4a@${b4},4c@${b4 + 1},4a@${b7},4c@${b7 + 1}` && blockInfo.led === '0.4.0',
        'the blocks were not saved as expected: ' + JSON.stringify(blockInfo));
    packets.push(...blockSaved);

    // The device reports each copy's parts: the first's toggle off, the second's on.
    const reportedFlip = await page.evaluate(([a, b]) => {
        window.__ws.receive(`sig ${a} 0 0`);
        window.__ws.receive(`sig ${b} 0 1`);
        return [elements.n4.outs[0], elements[t.flip2].outs[0]].join();
    }, [b4, b7]);
    expect(reportedFlip === '0,1', 'the device\'s part was not shown on the block\'s output: ' + reportedFlip);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // Reconnecting restores the blocks; saving again changes nothing. Taking a port out
    // of the definition takes it, and its wires, off each copy.
    ({ page, errors } = await open(browser));
    await connect(page, blockSaved[0]);
    const blockRestored = await page.evaluate((t) => ({
        defs: [...subDefs.values()].map(d => d.name + ':' + d.nodes().map(n => n.label).join('/')).join(),
        copies: ['n4', t.flip2].map(n => [elements[n].def?.name, elements[n].base, elements[n].roles.length,
            elements[n].outputs.length].join('.')).join(),
        wires: gEdges.map(e => e.start.node.name + ':' + e.start.output + '>' + e.end.node.name).join(),
    }), t);
    const wanted = [`${t.button}:0>n4`, `n4:1>${t.plot}`, `n4:0>${t.flip2}`, `${t.flip2}:0>${t.light}`, 'n4:0>n2'];
    expect(blockRestored.defs === 'Flip:In 1/Out 1/Out 2/Toggle/Counter'
        && blockRestored.copies === `Flip.${b4}.1.2,Flip.${b7}.1.2`
        && wanted.every(w => blockRestored.wires.split(',').includes(w)),
        'the blocks were not restored: ' + JSON.stringify(blockRestored));
    await page.locator('#graph .write svg').dispatchEvent('click');
    const blockResaved = await sent(page);
    expect(blockResaved.length === 1 && hex(blockResaved[0]) === hex(blockSaved[0]), 'saving the restored blocks changed the config');
    // Saving from inside the block updates its copies in the main graph.
    const portGone = await page.evaluate((t) => {
        openBlock(elements.n4.def);
        Object.values(elements).find(e => e.label === 'Out 2').remove();
        saveGraph();
        closeBlock();
        return [elements.n4.outputs.length, elements[t.plot].inConns().length, elements[t.flip2].outputs.length,
            gEdges.every(e => !e.removed)].join();
    }, t);
    packets.push(...await sent(page));
    expect(portGone === '1,0,1,true', 'a port taken out of the definition was left on its copies: ' + portGone);
    // The block-config example in PROTOCOL.md restores as it says.
    const example = await page.evaluate(() => {
        restoreGraph(configMessages(Uint8Array.from([
            0xA3, 0, 0, 1, 4, 0x46, 0x6C, 0x69, 0x70,
            0xA4, 0, 0, 1, 7, 0x61, 1, 0, 1, 2, 0, 0,
            0xA4, 0, 0, 1, 10, 0x62, 1, 0, 2, 5, 0, 3, 0, 0, 0,
            0xA4, 0, 0, 1, 8, 0x4A, 1, 0, 3, 3, 0, 1, 0,
            0x10, 0, 0, 1, 1, 1,
            0x60, 1, 0, 2, 11, 1, 0, 10, 1, 1, 0, 1, 0, 0, 10, 0,
            0x4A, 5, 0, 10, 3, 0, 1, 0,
            0x20, 1, 0, 3, 3, 0, 2, 0,
        ])));
        const copy = elements.n2;
        return [Object.keys(elements).join(' '), copy.def.name, copy.base, copy.in0Source?.name,
            elements.n3.inConns().map(c => c.start.node.name).join(), copy.partMessages().join('.'), idCounter].join();
    });
    expect(example === 'n1 n2 n3,Flip,10,n1,n2,74.5.0.10.3.0.1.0,11', 'the block-config example restored as ' + example);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await browser.close();
    fs.writeFileSync(out_file, packets.map(hex).join('\n') + '\n');
    console.log(`ui-test: ${packets.length} packets from the UI → ${out_file}`);
})().catch(e => fail(e.stack || String(e)));

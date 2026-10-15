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

async function open(browser) {
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

    // ── First connect: a first-boot device, then edit and save the graph ──
    let { page, errors } = await open(browser);
    await connect(page, FIRST_BOOT_CONFIG);
    expect(await page.evaluate(() => networks[0].ssid) === 'LRX', 'the first-boot config was not read');
    expect((await sent(page)).length === 0, 'the UI sent something on connect');
    expect(await page.evaluate(() => gEdges.map(e => e.start.node.name + '>' + e.end.node.name).join()) === 'n1>n2',
        'the demo graph does not wire n1 to n2');

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
    expect(await menu() === 'UII HWO HWI UIO | ', 'the plus shows ' + await menu());
    const groups = [];
    for (const g of ['uii', 'hwo', 'hwi', 'uio']) {
        await page.locator(`#add .group[data-group="${g}"]`).dispatchEvent('click');
        groups.push((await menu()).split(' | ')[1]);
    }
    expect(groups.join(', ') === 'BTN SLD, LED GPO PWM KEY, TMP ADC, PLT', 'the groups show ' + groups.join(', '));
    await page.locator('#add .group[data-group="uio"]').dispatchEvent('click');
    expect(await menu() === 'UII HWO HWI UIO | ', 'clicking the open group again left ' + await menu());
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

    // The pin is picked from the usable pins only.
    const pins = await page.evaluate(() => [...document.querySelectorAll('#settings .editable.select')]
        .find(row => row.querySelector('.label').textContent === 'GPIO pin:')
        .querySelectorAll('option').length + '|' + USABLE_PINS.join());
    expect(pins === '26|' + [...Array(23).keys(), 26, 27, 28].join(), 'the PWM offers GPIO pins ' + pins);
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
    await select('Input').first().selectOption('n6');      // the frequency's; the duty's follows
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

    // Add the core temperature and a second plot for it. Its values reach only
    // the browser, so it drives a plot but no hardware output.
    await page.locator('#add [title="Add the core temperature"]').dispatchEvent('click');
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
            toPwm: createEdge('n8', 'n4') === null && elements.n4.setSource('freq', elements.n8) === false,
            toLed: createEdge('n8', 'n3') === null,
            toPlot: createEdge('n8', 'n9') !== null && elements.n9.sourceID() === 8,
        };
    });
    expect(core.marked === 'n9', 'wire mode from the core temperature marks ' + core.marked);
    expect(core.toPwm, 'the core temperature was wired to a PWM');
    expect(core.toLed, 'the core temperature was wired to a GPO');
    expect(core.toPlot, 'the core temperature was not wired to the plot');

    // Add an ADC input on ADC1, and a plot for it. Its values are on the device,
    // so it drives a PWM as well as a plot; it is floating, so not a GPO.
    await page.locator('#add [title="Add an ADC input"]').dispatchEvent('click');
    const channels = await page.evaluate(() => [...document.querySelectorAll('#settings .editable.select option')]
        .map(o => o.textContent).join('|'));
    expect(channels === 'ADC0 · GPIO 26|ADC1 · GPIO 27|ADC2 · GPIO 28', 'the ADC offers channels ' + channels);
    await select('Channel').selectOption('1');
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
    const marshalled = keyboard.marshal.map(hex).join();
    expect(marshalled === '2300000c05000d00054c,1000000d0101,2300000e09000101' + '68656c6c6f0a,1000000100',
        'the keyboards and buttons marshal as ' + marshalled);

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
    await page.mouse.down();
    expect(await held.getAttribute('aria-pressed') === 'true', 'the momentary button is not on while held');
    await page.mouse.up();
    expect(await held.getAttribute('aria-pressed') === 'false', 'the momentary button is not off when let go');
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
    expect(await page.locator('#command .control-plot canvas').count() === 3, 'the Command view has no plot');
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

    // The device's temperature telemetry reaches the core node, its tile and its plot.
    await page.evaluate(() => window.__ws.receive('core 31.27'));
    const temperature = await page.evaluate(() => ({
        tile: document.querySelector('#command .control-reading .state').textContent,
        plot: elements.n9.samples.map(([, v]) => v).join(),
    }));
    expect(temperature.tile === '31.3 °C', 'the core temperature tile shows ' + temperature.tile);
    expect(temperature.plot === ',31.27', 'the temperature plot holds ' + temperature.plot);

    // ADC readings reach the ADC nodes on their channel only, in volts.
    await page.evaluate(() => { window.__ws.receive('adc 0 4095'); window.__ws.receive('adc 1 2048'); });
    const reading = await page.evaluate(() => ({
        tile: document.querySelectorAll('#command .control-reading .state')[1].textContent,
        plot: elements.n11.samples.map(([, v]) => v === null ? '' : v.toFixed(4)).join(),
    }));
    expect(reading.tile === '1.65 V', 'the ADC tile shows ' + reading.tile);
    expect(reading.plot === ',1.6504', 'the ADC plot holds ' + reading.plot);
    await toCommandView(page);
    const plotStates = await page.$$eval('#command .control-plot .state', ss => ss.map(s => s.textContent).join('|'));
    expect(plotStates.endsWith('|31.27 °C|1.65 V'), 'the plots show ' + plotStates);

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
        const pwm = new uiPWM(400, 300);      // n15, no wires yet
        new uiButton(-400, 300);              // n16
        const marked = () => Object.values(elements).filter(e => e.marked).map(e => e.name).join();
        toggleConnectMode();
        selectById(15);
        const fromPwm = marked();
        clearSelection();
        toggleConnectMode();
        selectById(15);
        pwm.setMode('duty', 'input');
        pwm.createSettings();
        const input = [...document.querySelectorAll('#settings .editable.select')]
            .find(row => row.querySelector('.label').textContent === 'Input:');
        const picks = [...input.querySelectorAll('option')].map(o => o.value).join();
        return {
            fromPwm,
            picks,
            sliderToLed: createEdge('n5', 'n2') === null && elements.n2.inConns()[0].start.node.name === 'n1',
            buttonToPwm: createEdge('n16', 'n15') === null && pwm.setSource('duty', elements.n16) === false
                && pwm.inConns().length === 0,
        };
    });
    expect(types.fromPwm === 'n5,n6,n10', 'wire mode from a PWM marks ' + types.fromPwm);
    expect(types.picks === ',n5,n6,n10', 'the PWM offers inputs ' + types.picks);
    expect(types.sliderToLed, 'a slider was wired to the LED');
    expect(types.buttonToPwm, 'a button was wired to a PWM');

    // A wire into an output that has one replaces it, on both nodes.
    const replaced = await page.evaluate(() => {
        createEdge('n16', 'n2');
        return {
            led: elements.n2.inConns()[0].start.node.name,
            button: elements.n1.outputs[0].conns.map(c => c.end.node.name).join(),
            wires: gEdges.filter(e => e.end.node === elements.n2).length,
        };
    });
    expect(replaced.led === 'n16' && replaced.button === 'n3,n14' && replaced.wires === 1,
        'a second wire into the LED did not replace the first: ' + JSON.stringify(replaced));
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
    }));
    expect(restored.labels.join('|') === 'Start|Pompe à eau|GPO|PWM|Speed|Pitch|Speed plot|Core temperature|Temperature|Level sensor|Level|Lock|Press|Greeting', 'labels not restored: ' + restored.labels);
    expect(restored.pin === 7 && restored.pwm === '15,20000,true,75,20,80,true,200,2000,true' && restored.vertical && restored.wires === 9
        && restored.plot === '20,5,8,10' && restored.adc === 1 && restored.units === 'rpm|rpm|°C||V' && restored.plots === 3 && restored.readings === 2
        && restored.pwmSources === 'n5,n6'
        && restored.buttons === 2
        && restored.keyboards === 'Ctrl+Alt+Delete,13,true,false,"hello\\n",1',
        'graph not restored: ' + JSON.stringify(restored));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const resaved = await sent(page);
    expect(resaved.length === 1 && hex(resaved[0]) === hex(saved), 'saving the restored graph changed the config');
    packets.push(...resaved);

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

    await browser.close();
    fs.writeFileSync(out_file, packets.map(hex).join('\n') + '\n');
    console.log(`ui-test: ${packets.length} packets from the UI → ${out_file}`);
})().catch(e => fail(e.stack || String(e)));

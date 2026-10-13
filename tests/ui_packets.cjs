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
                setTimeout(() => {
                    this.readyState = FakeWebSocket.OPEN;
                    if (this.onopen) this.onopen();
                }, 0);
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
                if (this.onclose) this.onclose();
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

    // Add a PWM output from the add menu, set it up and wire the button to it.
    await page.locator('#add .toggle').dispatchEvent('click');
    await page.locator('#add [title="Add a PWM output"]').dispatchEvent('click');
    const rows = () => page.$$eval('#settings .editable .label', ls => ls.slice(2).map(l => l.textContent).join('|'));
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
        pwm.setPin('15');
        pwm.setFreq('20000');
        pwm.setDuty('75');
        pwm.setDutyMin('20');
        pwm.setDutyMax('80');
    });

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
    await page.locator('#settings .editable.select select').first().selectOption('n6');
    await page.evaluate(() => { elements.n4.setFreqMin('200'); elements.n4.setFreqMax('2000'); });
    expect(await page.evaluate(() => elements.n4.freqLog) === false, 'a frequency from an input does not start linear');
    await choice('Scale', 'Logarithmic');
    expect(await page.evaluate(() => elements.n4.freqLog), 'the Scale choice did not make the frequency logarithmic');
    const freqWire = await page.evaluate(() => [elements.n4.freqSource?.name,
        elements.n4.inConns().map(c => c.start.node.name).join()].join('|'));
    expect(freqWire === 'n6|n5,n6', 'picking the frequency input did not wire it: ' + freqWire);
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

    // A wire into an output that has one replaces it, on both nodes.
    const replaced = await page.evaluate(() => {
        createEdge('n5', 'n2');
        return {
            led: elements.n2.inConns()[0].start.node.name,
            button: elements.n1.outputs[0].conns.map(c => c.end.node.name).join(),
            wires: gEdges.filter(e => e.end.node === elements.n2).length,
        };
    });
    expect(replaced.led === 'n5' && replaced.button === 'n3' && replaced.wires === 1,
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
        wires: gEdges.length,
        pwmSources: [elements.n4.dutySource?.name, elements.n4.freqSource?.name].join(),
        buttons: document.querySelectorAll('#command .control-btn').length,
    }));
    expect(restored.labels.join('|') === 'Start|Pompe à eau|GPO|PWM|Speed|Pitch', 'labels not restored: ' + restored.labels);
    expect(restored.pin === 7 && restored.pwm === '15,20000,true,75,20,80,true,200,2000,true' && restored.vertical && restored.wires === 4
        && restored.pwmSources === 'n5,n6'
        && restored.buttons === 1,
        'graph not restored: ' + JSON.stringify(restored));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const resaved = await sent(page);
    expect(resaved.length === 1 && hex(resaved[0]) === hex(saved), 'saving the restored graph changed the config');
    packets.push(...resaved);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await browser.close();
    fs.writeFileSync(out_file, packets.map(hex).join('\n') + '\n');
    console.log(`ui-test: ${packets.length} packets from the UI → ${out_file}`);
})().catch(e => fail(e.stack || String(e)));

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

    await page.evaluate(() => {
        createEdge('n1', 'n2');
        createEdge('n1', 'n3');
        elements.n1.setLabel('Start');
        elements.n2.setLabel('Pompe à eau');
        elements.n3.setPin('7');
        const led = elements.n2;
        led.x = 260; led.y = -80;
        led.gElm.setAttribute('transform', `translate(${led.x} ${led.y}) rotate(0)`);
        gEdges[0].start.angle = Math.PI / 2;
        gEdges[0].start.mvfunc();
    });
    await page.locator('#graph .write svg').dispatchEvent('click');
    const saves = await sent(page);
    expect(saves.length === 1 && saves[0][0] === 0x0A, 'saving did not send one config packet');
    const saved = saves[0];
    packets.push(...saves);

    // ── Press the button in the Command view, twice ──
    await page.locator('#command .control-btn').first().dispatchEvent('click');
    await page.locator('#command .control-btn').first().dispatchEvent('click');
    const commands = await sent(page);
    expect(commands.length === 2 && commands.every(p => p[0] === 0x01), 'pressing the button did not send two commands');
    packets.push(...commands);

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
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));
    await page.close();

    // ── Reconnect: the device sends the saved config back ──
    ({ page, errors } = await open(browser));
    await connect(page, saved);
    const restored = await page.evaluate(() => ({
        labels: Object.values(elements).map(e => e.label),
        pin: elements.n3.pin,
        wires: gEdges.length,
        buttons: document.querySelectorAll('#command .control-btn').length,
    }));
    expect(restored.labels.join('|') === 'Start|Pompe à eau|GPO', 'labels not restored: ' + restored.labels);
    expect(restored.pin === 7 && restored.wires === 2 && restored.buttons === 1, 'graph not restored: ' + JSON.stringify(restored));

    await page.locator('#graph .write svg').dispatchEvent('click');
    const resaved = await sent(page);
    expect(resaved.length === 1 && hex(resaved[0]) === hex(saved), 'saving the restored graph changed the config');
    packets.push(...resaved);
    expect(errors.length === 0, 'page errors: ' + errors.join('; '));

    await browser.close();
    fs.writeFileSync(out_file, packets.map(hex).join('\n') + '\n');
    console.log(`ui-test: ${packets.length} packets from the UI → ${out_file}`);
})().catch(e => fail(e.stack || String(e)));

let socket, gfig;
let offlineConfig = null; //offline
let offlineTelemetry = null; //offline
let offlineConnect = null; //offline

const escapeHtml = (text) => String(text).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);

function networkingTemplate(state) {
    // `placeholder` is shown while the field has no value.
    const editableField = (obj, type, field, label, nindex=-1, secret=false, dindex=-1, placeholder='') => {
        const value = obj[field];
        const editing = obj._editing === field;
        const packettype = secret?PKT_UPDATE_SECRET:PKT_UPDATE_CONFIG;

        if (!editing) {
            return `
            <div class="editable">
                <span class="label">${label}:</span>
                <span class="value">${value || placeholder}</span>
                <svg onclick="networks[${nindex}]._draft='${value}';networks[${nindex}]._editing='${field}'" viewBox="0 0 24 24" fill="none" preserveAspectRatio="xMidYMid meet" xmlns="http://www.w3.org/2000/svg">
                    <path stroke="#BFCDE7" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M15.4998 5.49994L18.3282 8.32837M3 20.9997L3.04745 20.6675C3.21536 19.4922 3.29932 18.9045 3.49029 18.3558C3.65975 17.8689 3.89124 17.4059 4.17906 16.9783C4.50341 16.4963 4.92319 16.0765 5.76274 15.237L17.4107 3.58896C18.1918 2.80791 19.4581 2.80791 20.2392 3.58896C21.0202 4.37001 21.0202 5.63634 20.2392 6.41739L8.37744 18.2791C7.61579 19.0408 7.23497 19.4216 6.8012 19.7244C6.41618 19.9932 6.00093 20.2159 5.56398 20.3879C5.07171 20.5817 4.54375 20.6882 3.48793 20.9012L3 20.9997Z"/>
                </svg>
            </div>
            `;
        } else {
            return `
            <div class="editable">
                <span class="label">${label}:</span>
                <input id="input-${nindex}-${field}" value="${networks[nindex]._draft}" type="text" autofocus />
                <svg onclick="confirmNetworkField(${packettype},${type},${nindex},'${field}')" viewBox="0 0 24 24" fill="none" preserveAspectRatio="xMidYMid meet" xmlns="http://www.w3.org/2000/svg">
                    <path stroke="#BFCDE7" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M4 12.6111L8.92308 17.5L20 6.5"/>
                </svg>
                <svg onclick="networks[${nindex}]._editing=''" viewBox="0 0 24 24" fill="none" preserveAspectRatio="xMidYMid meet" xmlns="http://www.w3.org/2000/svg">
                    <path stroke="#BFCDE7" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M6 6L18 18M18 6L6 18"/>
                </svg>
            </div>
            `;
        }
    }

    // The connection switch: red and off when disconnected, pulsing between red and
    // green with the knob halfway while connecting, green and on when connected. A
    // failed or lost connection leaves a message below the device.
    const SWITCH_TITLES = {disconnected: "Connect", connecting: "Connecting… (click to cancel)", connected: "Disconnect"};
    const KNOB = {disconnected: "translate(0 3)", connecting: "translate(2.9 3)", connected: "translate(5.8 3)"};
    const deviceElement = (nindex,dindex,device) => `
        <div class="device-elm">
            <div>
                <h4>Device: ${device.name}</h4>
                <p>IP: ${device.ip}</p>
                ${device.message ? `<p class="device-message" role="alert">${escapeHtml(device.message)}</p>` : ""}
            </div>
            <div class="device-connect ${device.state}" title="${SWITCH_TITLES[device.state]}" onclick="toggleWebSocket(${nindex},${dindex})">
                <svg width="100%" height="100%" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                    <path class="light" opacity="0.8" stroke="#BFCDE7" fill=${device.state === "connected"?"#4CAF5080":"#E7BFBF80"} stroke-width="1.5" d="M2 12C2 7.28595 2 4.92893 3.46447 3.46447C4.92893 2 7.28595 2 12 2C16.714 2 19.0711 2 20.5355 3.46447C22 4.92893 22 7.28595 22 12C22 16.714 22 19.0711 20.5355 20.5355C19.0711 22 16.714 22 12 22C7.28595 22 4.92893 22 3.46447 20.5355C2 19.0711 2 16.714 2 12Z"/>
                    <g transform="${KNOB[device.state]} scale(0.745)">
                        <path stroke="#BFCDE7" fill="#2d2f34C0" stroke-width="1.5" d="M6 8C6 5.17157 6 3.75736 6.87868 2.87868C7.75736 2 9.17157 2 12 2C14.8284 2 16.2426 2 17.1213 2.87868C18 3.75736 18 5.17157 18 8V16C18 18.8284 18 20.2426 17.1213 21.1213C16.2426 22 14.8284 22 12 22C9.17157 22 7.75736 22 6.87868 21.1213C6 20.2426 6 18.8284 6 16V8Z"/>
                    </g>
                </svg>
            </div>
        </div>
    `;
    
    const networkElement = (nindex,network) => `
        <div class="network-box">
            <div class="network-attributes">
                <div class="network-head">
                    <h3>Network</h3>
                    <div class="netconn-buttons">
                        <div class="${network.options===0?"sel":"nosel"}" onclick="networks[${nindex}].options=networks[${nindex}].options&254;updateNetworkFields(PKT_UPDATE_CONFIG,MSG_NETWORK,${nindex},'options')">
                            <svg width="100%" height="100%" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                                <path stroke="#BFCDE7C0" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M12 15V9M7.41604 11.0005C7.14845 10.388 7 9.71159 7 9.00049C7 7.87466 7.37209 6.83574 8 6M16.584 11.0005C16.8516 10.388 17 9.71159 17 9.00049C17 7.87466 16.6279 6.83574 16 6M18.7083 3C20.1334 4.59227 21 6.69494 21 9C21 9.68739 20.9229 10.3568 20.777 11M5.29168 3C3.86656 4.59227 3 6.69494 3 9C3 9.68739 3.07706 10.3568 3.22302 11M6 18H6.01M9 18H9.01M12 18H12.01M18 15.0001C18.9319 15.0001 19.3978 15 19.7654 15.1522C20.2554 15.3552 20.6448 15.7446 20.8478 16.2346C21 16.6022 21 17.0681 21 18C21 18.9319 21 19.3978 20.8478 19.7654C20.6448 20.2554 20.2554 20.6448 19.7654 20.8478C19.3978 21 18.9319 21 18 21H6C5.06812 21 4.60218 21 4.23463 20.8478C3.74458 20.6448 3.35523 20.2554 3.15224 19.7654C3 19.3978 3 18.9319 3 18C3 17.0681 3 16.6022 3.15224 16.2346C3.35523 15.7446 3.74458 15.3552 4.23463 15.1522C4.59855 15.0015 5.05894 15 5.97256 15C9.98171 15 13.9909 15 18 15.0001Z"/>
                            </svg>
                        </div>
                        <div class="${network.options===1?"sel":"nosel"}" onclick="networks[${nindex}].options=networks[${nindex}].options|1;updateNetworkFields(PKT_UPDATE_CONFIG,MSG_NETWORK,${nindex},'options')">
                            <svg width="100%" height="100%" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                                <path transform="rotate(180) translate(-24 -24)" stroke="#BFCDE7C0" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M9 21H6.2C5.0799 21 4.51984 21 4.09202 20.782C3.71569 20.5903 3.40973 20.2843 3.21799 19.908C3 19.4802 3 18.9201 3 17.8V6.2C3 5.0799 3 4.51984 3.21799 4.09202C3.40973 3.71569 3.71569 3.40973 4.09202 3.21799C4.51984 3 5.0799 3 6.2 3H11.8C12.9201 3 13.4802 3 13.908 3.21799C14.2843 3.40973 14.5903 3.71569 14.782 4.09202C15 4.51984 15 5.0799 15 6.2V10M21 21H21.01M8 6H10M17 21C17 18.7909 18.7909 17 21 17M13 21C13 16.5817 16.5817 13 21 13"/>
                            </svg>
                        </div>
                    </div>
                </div>
                <div>
                    ${editableField(network,MSG_NETWORK_FIELD,"name","Name",nindex)}
                    ${editableField(network,MSG_NETWORK_FIELD,"ssid","SSID",nindex)}
                    ${editableField(network,MSG_NETWORK_FIELD,"password","Password",nindex,true)}
                    ${(network.options & NET_OPT_JOIN) === 0
                        ? editableField(network,MSG_NETWORK_FIELD,"apAddress","IP address",nindex,false,-1,`${NET_AP_ADDRESS_DEFAULT} (default)`)
                        : editableField(network,MSG_NETWORK_FIELD,"staticAddress","Static IP",nindex,false,-1,"DHCP")
                          + (network.staticAddress
                              ? editableField(network,MSG_NETWORK_FIELD,"gateway","Gateway",nindex,false,-1,"from DHCP")
                              : "")}
                    <p class="network-note">Network changes take effect when the device restarts.</p>
                </div>
            </div>
            <div class="device-box">
                ${network.devices.map((device,dindex) => deviceElement(nindex,dindex,device))}
            </div>
        </div>
    `;

    return `${state.map((network,nindex) => networkElement(nindex,network)).join("")}`;
}

const networks = mount(document.getElementById("network"), [
    {
        name:"unknown",
        ssid:"unknown",
        password:"••••••••",
        options:0,
        apAddress:"",           // IPv4 addresses as text; "" = not set
        staticAddress:"",
        gateway:"",
        devices:[
            {
                name:"unknown",
                ip:location.host,
                state:"disconnected",   // "connecting", "connected"
                message:""              // why the last connection failed or ended
            }
        ],
        fields:{options:FIELD_NETWORK_OPTIONS, name:FIELD_NETWORK_NAME, ssid:FIELD_NETWORK_SSID, password:FIELD_NETWORK_PASSWORD,
            apAddress:FIELD_NETWORK_AP_ADDRESS, staticAddress:FIELD_NETWORK_STATIC_ADDRESS, gateway:FIELD_NETWORK_GATEWAY}
    }
], networkingTemplate);

// function updateNetworkOptions(netID) {
//     const data = new Uint8Array([0x0B, 0, 7, 0xB2, 0, (netID >> 8) & 0xFF, netID & 0xFF, 2, 0, networks[netID].options]);
//     socket.send(data);
// }

const ADDRESS_FIELDS = ['apAddress', 'staticAddress', 'gateway'];

// Confirm an edited field: an address must be one the device can use, or empty to
// unset it; a rejected one keeps the field open, outlined in red.
function confirmNetworkField(pt, type, netID, field) {
    const net = networks[netID];
    if (ADDRESS_FIELDS.includes(field)) {
        const text = net._draft.trim();
        const bytes = parseAddress(text);
        const usable = field === 'apAddress' ? apAddressUsable : addressUsable;
        if (text && !(bytes && usable(bytes))) {
            const input = document.getElementById(`input-${netID}-${field}`);
            input.setAttribute('aria-invalid', 'true');
            input.title = field === 'apAddress'
                ? `An address like ${NET_AP_ADDRESS_DEFAULT}; it may not end in 0, 255 or `
                    + `${NET_LEASE_FIRST}–${NET_LEASE_FIRST + NET_LEASE_COUNT - 1}, which its phones and computers get`
                : 'An address like 192.168.1.50, not ending in 0 or 255; empty for none';
            input.focus();
            return;
        }
        net._draft = bytes ? bytes.join('.') : '';
    }
    net._editing = '';
    net[field] = net._draft;
    updateNetworkFields(pt, type, netID, field);
}

function updateNetworkFields(pt,type,netID,field) {
    const encoder = new TextEncoder();

    const value = [];
    if (ADDRESS_FIELDS.includes(field)) {
        value.push(...(parseAddress(networks[netID][field]) ?? [0, 0, 0, 0]));     // 0.0.0.0 unsets it
    } else if (typeof(networks[netID][field]) == "number") {
        value.push(networks[netID][field]);
    } else if (typeof(networks[netID][field]) == "string") {
        value.push(...encoder.encode(networks[netID][field]));
    }

    const pcktlen = 5 + 1 + value.length;

    const data = new Uint8Array([pt, 0, pcktlen, type, 0, (netID >> 8) & 0xFF, netID & 0xFF, value.length + 1, networks[netID].fields[field], ...value]);
    console.log(data); //debug
    socket.send(data); //online

    // Keep the config in step with the device's, as saving the graph sends its
    // networking messages back: an old copy would undo this change.
    if (pt === PKT_UPDATE_CONFIG && gfig) gfig = configSetField(gfig, data.slice(PKT_HEADER_LEN));
}

// A config payload with one field message set, as the device sets it
// (config_set_field in src/config/config_iterator.c): it replaces the message with
// the same type, ID and field, or is added after the element's last message.
function configSetField(payload, msg) {
    const msgs = configMessages(payload);
    const id = msg[2] << 8 | msg[3];
    const same = msgs.findIndex(m => m.type === msg[0] && m.id === id && m.len > 0 && m.pay[0] === msg[5]);
    let at = same;
    if (same < 0) {
        at = msgs.findLastIndex(m => (m.type | 1) === (msg[0] | 1) && m.id === id) + 1;
        if (at === 0) return payload;
    }
    const bytes = msgs.map(m => m.bytes);
    bytes.splice(at, same < 0 ? 0 : 1, msg);
    return Uint8Array.from(bytes.flatMap(b => Array.from(b)));
}

function toggleWebSocket(n,d) {
    const dv = networks[n].devices[d];
    dv.message = "";
    if (dv.state !== "disconnected") {
        // Disconnect, or give up connecting.
        dv.state = "disconnected";
        disconnectWebSocket(dv); //online
        clearInterval(offlineTelemetry); //offline
        clearTimeout(offlineConnect); //offline
    } else {
        dv.state = "connecting";
        connectWebSocket(dv); //online
        //offline-start
        // No controller to talk to: after a moment, as if connecting, feed in the
        // last saved config, or a canned first-boot config, as if the device sent it.
        offlineConnect = setTimeout(() => {
            dv.state = "connected";
            const data = offlineConfig || new Uint8Array([
                PKT_CONFIG, 0, 58,
                0xB0, 0, 0, 0, 9, 0, 189, 252, 234, 193, 78, 30, 121, 77,
                0xB1, 0, 0, 0, 5, 1, 76, 56, 83, 71,
                0xB1, 0, 0, 0, 3, 2, 0, 0,
                0xB2, 0, 0, 0, 2, 0, 1,
                0xB3, 0, 0, 0, 5, 1, 76, 56, 83, 71,
                0xB3, 0, 0, 0, 4, 2, 76, 82, 88,
            ]);
            networks[0].devices[0].did = data.slice(9,17);
            gfig = data.slice(3);
            unmarshalConfigs(gfig);
            // and signals, as telemetry: a chip temperature drifting around 30 °C,
            // on each ADC channel a slow wave, in volts, and on each LM35 a room
            // temperature drifting around 22 °C.
            let tick = 0;
            offlineTelemetry = setInterval(() => {
                const t = performance.now() / 1000;
                for (const e of Object.values(elements)) {
                    if (e instanceof uiCore && tick % 5 === 0) {
                        receiveTelemetry(`sig ${e.id} 0 ${(30 + 4 * Math.sin(t / 20) + Math.random() - 0.5).toFixed(2)}`);
                    } else if (e instanceof uiLM35) {
                        if (tick % 5 === 0) receiveTelemetry(`sig ${e.id} 0 ${(22 + 2 * Math.sin(t / 30 + e.channel)).toFixed(1)}`);
                    } else if (e instanceof uiADC) {
                        const level = 0.5 + 0.4 * Math.sin(t / (4 + 3 * e.channel)) + 0.01 * (Math.random() - 0.5);
                        receiveTelemetry(`sig ${e.id} 0 ${(level * ADC_VREF).toFixed(3)}`);
                    }
                }
                tick++;
            }, 200);
        }, 800);
        //offline-end
    }
}

// Split a config payload into its messages: {type, id, len, pay, bytes}.
// `bytes` is the whole message, header included. A truncated last message is dropped.
function configMessages(binary) {
    const msgs = [];
    let offset = 0;

    while (offset + MSG_HEADER_LEN <= binary.length) {
        const len = binary[offset + 4];
        const end = offset + MSG_HEADER_LEN + len;
        if (end > binary.length) break;

        msgs.push({
            type: binary[offset],
            flags: binary[offset + 1],
            id: binary[offset + 2] << 8 | binary[offset + 3],
            len: len,
            pay: binary.slice(offset + MSG_HEADER_LEN, end),
            bytes: binary.slice(offset, end),
        });
        offset = end;
    }

    return msgs;
}

function unmarshalConfigs(binary) {
    const decoder = new TextDecoder();
    const configs = {};

    for (const elm of configMessages(binary)) {
        if (!configs.hasOwnProperty(elm.type)) {
            configs[elm.type] = [];
        }

        configs[elm.type].push(elm);
    }

    restoreGraph(configMessages(binary));

    for (const device of configs[MSG_DEVICE]) {
        const did = networks[0].devices[0].did;
        let netId = -1;

        if (areByteArraysEqual(device.pay.slice(1), did)) {
            const devId = device.id;

            let iElm = 0;
            while (iElm < configs[MSG_DEVICE_FIELD].length) {
                const devParam = configs[MSG_DEVICE_FIELD][iElm];
                if (devParam.id !== devId) {
                    iElm++;
                    continue;
                }

                const field = devParam.pay[0];
                switch (field) {
                    case FIELD_DEVICE_NAME:
                        networks[0].devices[0].name = decoder.decode(devParam.pay.slice(1));
                        break;
                
                    case FIELD_DEVICE_NETWORK:
                        netId = devParam.pay[1] << 8 || devParam.pay[2];
                        break;
                
                    default:
                        break;
                }

                configs[MSG_DEVICE_FIELD].splice(iElm,1);
            }

            iElm = 0;
            while (iElm < configs[MSG_NETWORK].length) {
                const netParam = configs[MSG_NETWORK][iElm];
                if (netParam.id !== netId) {
                    iElm++;
                    continue;
                }

                const field = netParam.pay[0];
                switch (field) {
                    case FIELD_NETWORK_OPTIONS:
                        networks[0].options = netParam.pay[1];
                        break;
                
                    default:
                        break;
                }

                configs[MSG_NETWORK].splice(iElm,1);
            }

            // Addresses not in the config are not set.
            networks[0].apAddress = networks[0].staticAddress = networks[0].gateway = "";
            const address = (pay) => pay.length === 5 && pay.slice(1).some(b => b) ? Array.from(pay.slice(1)).join('.') : "";

            iElm = 0;
            while (iElm < configs[MSG_NETWORK_FIELD].length) {
                const netParam = configs[MSG_NETWORK_FIELD][iElm];
                if (netParam.id !== netId) {
                    iElm++;
                    continue;
                }

                const field = netParam.pay[0];
                switch (field) {
                    case FIELD_NETWORK_NAME:
                        networks[0].name = decoder.decode(netParam.pay.slice(1));
                        break;
                
                    case FIELD_NETWORK_SSID:
                        networks[0].ssid = decoder.decode(netParam.pay.slice(1));
                        break;

                    case FIELD_NETWORK_AP_ADDRESS:
                        networks[0].apAddress = address(netParam.pay);
                        break;

                    case FIELD_NETWORK_STATIC_ADDRESS:
                        networks[0].staticAddress = address(netParam.pay);
                        break;

                    case FIELD_NETWORK_GATEWAY:
                        networks[0].gateway = address(netParam.pay);
                        break;
                
                    default:
                        break;
                }

                configs[MSG_NETWORK_FIELD].splice(iElm,1);
            }
        }
    }
}

function areByteArraysEqual(arr1, arr2) {
  return arr1.length === arr2.length && arr1.every((value, index) => value === arr2[index]);
}

//online-start
// The connection being made or in use; a socket's events count only while it is
// this one, so a cancelled or replaced connection can't change the switch.
let currentSocket = null;
let connectTimer = null;
const CONNECT_TIMEOUT_MS = 10000;

function connectWebSocket(device) {
    const where = device.ip || "the device";
    const ended = (message) => {
        clearTimeout(connectTimer);
        currentSocket = null;
        device.state = "disconnected";
        device.message = message;
    };

    try {
        socket = new WebSocket("ws://" + device.ip);
    } catch (error) {
        ended(`Can't connect to ${where}: ${error.message}`);
        return;
    }
    const ws = socket;
    currentSocket = ws;
    let opened = false;

    // A host that doesn't answer can take a minute to fail on its own.
    connectTimer = setTimeout(() => {
        if (ws !== currentSocket) return;
        ended(`No answer from ${where} after ${CONNECT_TIMEOUT_MS / 1000} s. `
            + "Is this computer or phone on the device's WiFi network, and is the device on?");
        ws.close();
    }, CONNECT_TIMEOUT_MS);

    socket.onopen = function () {
        if (ws !== currentSocket) return;
        clearTimeout(connectTimer);
        opened = true;
        device.state = "connected";
        console.log("WebSocket connected.");
    };

    socket.binaryType = "arraybuffer";
    socket.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
            const data = new Uint8Array(event.data);
            console.log("Received binary data:", data);

            if (data[0] === PKT_CONFIG) {
                gfig = data.slice(3);
                unmarshalConfigs(gfig);
                // console.log(config);
                // reconstructConfigInterface(config);
                // buildInterface(config);
            } else if (data[0] === PKT_DEVICE_ID) {
                networks[0].devices[0].did = data.slice(3);
            }

        } else {
            receiveTelemetry(event.data);
        }
    };

    // The browser says why a connection ended only in the close code: 1006 for
    // both a host that can't be reached and a connection that dropped.
    socket.onclose = function (event) {
        console.log("WebSocket disconnected.", event.code);
        if (ws !== currentSocket) return;
        ended(opened
            ? `Lost the connection to ${where}. Click the switch to connect again.`
            : `Couldn't connect to ${where}. `
                + "Is this computer or phone on the device's WiFi network, and is the device on?");
    };

    socket.onerror = function (error) {
        console.error("WebSocket error:", error);
    };
}

// Close the connection, or stop making it. Its close event then counts for nothing.
function disconnectWebSocket(device) {
    clearTimeout(connectTimer);
    currentSocket = null;
    if (socket && socket.readyState !== WebSocket.CLOSED) socket.close();
}
//online-end

# NeoGraph

This project provides a **web-based control interface** for WiFi-enabled microcontrollers such as the **Raspberry Pi Pico W 2**.  
It allows you to interact with the controller through a browser — defining control setups, sending and receiving data, and monitoring or automating connected hardware.

---

## 🚀 Features

- **Web-based UI:**  
  Serve a responsive web interface directly from the controller.  
- **Hardware Control:**  
  Send commands to control the controller’s I/O pins and peripherals.  
- **Data Feedback:**  
  Receive live data from the controller and visualize it on the web interface.  
- **Standalone Operation:**  
  Configure the controller to continue running independently after setup.  
- **Built-in WiFi Access Point:**  
  Connect directly to the controller’s WiFi network — no external router required.

---

## 🧰 Build Instructions

Make sure you have the **Raspberry Pi Pico SDK** and toolchain installed.  

### Build the firmware

```bash
make build
```

This compiles the firmware and web interface into a single binary ready to flash.

---

## 💾 Flashing the Controller

1. Put the controller in **file storage (BOOTSEL)** mode by holding the **BOOTSEL** button while plugging it in.  
2. Then flash the firmware:

```bash
make flash
```

3. Once flashing completes, the controller will reboot and start its WiFi access point.  
4. Connect to the WiFi network it creates, then open your browser and go to the displayed IP address (usually `192.168.4.1`).

---

## 🧪 Development and Testing

### Build targets

| Target | What it builds |
|---|---|
| `make uidev` | Browser-only UI in `ui/dist/index.html`, with canned controller data. This is the GitHub Pages demo. |
| `make hwdev` | Firmware with the debug UI (console logging kept). |
| `make build` | Release firmware. |
| `make test` | Builds and runs the C unit tests. |

The UI source is shared; the targets differ only in which marked code they strip from the JS:

| Marker | Wraps | Stripped by |
|---|---|---|
| `//online-start … //online-end`, `//online` | Talking to the controller (WebSocket) | `uidev` |
| `//offline-start … //offline-end`, `//offline` | Stand-ins for the controller (canned data) | `hwdev`, `build` |
| `//debug` | Console logging | `build` |

Markers must end their line: a block marker sits on its own line, and a single-line marker is a trailing comment.

### Test Web Interface Locally

You can test the web interface in your browser without flashing:

```bash
make uidev
```

If you have a hot reload server you can use `entr` to rebuild whenever a file changes and get that reloaded.

```bash
ls ui/content/*.* ui/content/components/* ui/layout.* | entr make uidev
```

---

## 🧩 Typical Workflow

1. **Build and flash** using `make build && make flash`.  
2. **Connect** to the controller’s WiFi network.  
3. **Open** the IP address in your browser.  
4. **Configure** your control setup in the UI.  
5. **Interact** with your hardware — manually or in autonomous mode.

---

## 📄 License

This project is open-source and available under the [MIT License](LICENSE).

## 😀 Credits

Icons used on [svgrepo.com](https://www.svgrepo.com)
- [solar-line-duotone](https://www.svgrepo.com/collection/solar-line-duotone-icons)
- [dazzle-line](https://www.svgrepo.com/collection/dazzle-line-icons)
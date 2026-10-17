// An LM35 temperature sensor (an LM35DZ reads 0 to 100 °C) on one of the device's
// ADC channels, as an ADC input's. A floating source whose value is the
// temperature in °C, LM35_MV_PER_C mV per °C. On the device it drives the
// hardware outputs wired to it (a PWM) from 0 °C (0) to LM35_TEMP_FULL °C (1);
// its readings also reach the browser as telemetry, in °C.
class uiLM35 extends uiADC {
    static badge = "LM35";
    static defaultLabel = "LM35";

    constructor(x = 0, y = 0) {
        super(x, y, "lm35", MSG_HW_LM35);
        this.unit = '°C';
    }

    reading() {
        return Number.isFinite(this.value) ? `${this.value.toFixed(1)} °C` : '–';
    }
}

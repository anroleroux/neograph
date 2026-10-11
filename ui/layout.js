const testing = true; //testing
const debug = false; //testing

const container = document.getElementById('container');
const bcSelect = document.getElementById('bcselect');
function toNetwork() {
    container.className = "network";
    bcSelect.className = "network";
};
function toCommand() {
    container.className = "command";
    bcSelect.className = "command";
}
function toGraph() {
    container.className = "graph";
    bcSelect.className = "graph";
}
function toSettings() {
    container.className = "settings";
    bcSelect.className = "settings";
}

function reactive(obj, onChange) {
    return new Proxy(obj, {
        get(target, prop, receiver) {
            const value = Reflect.get(target, prop, receiver);

            if (ArrayBuffer.isView(value) && !(value instanceof DataView)) {
                return value;
            }
            
            if (typeof value === 'object' && value !== null)
                return reactive(value, onChange);
            return value;
        },
        set(target, prop, value, receiver) {
            const result = Reflect.set(target, prop, value, receiver);
            if (prop !== "_draft") {
                onChange();
            }
            return result;
        },
        deleteProperty(target, prop) {
            const result = Reflect.deleteProperty(target, prop);
            onChange();
            return result;
        }
    });
}

function mount(root, state, template) {
    function bindEditableInputs() {
        document.querySelectorAll("input[id^='input-']").forEach(input => {
            input.oninput = e => {
                const [ , nindex, field ] = input.id.split("-");
                networks[nindex]._draft = e.target.value;
            };
            input.focus();
        });
    }

    function render() {
        root.innerHTML = template(state);
        bindEditableInputs();
    }
    
    const r = reactive(state, render);
    render();
    return r;
}

/* {{network-js}} */

/* {{command-js}} */

/* {{components-js}} */

/* {{graph-js}} */

/* {{settings-js}} */
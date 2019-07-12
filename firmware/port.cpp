#include <makestack/types.h>
#include <makestack/logger.h>
#include <makestack/vm.h>
#include <Arduino.h>

void vm_port_panic(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);
    vprintf(fmt, vargs);
    va_end(vargs);

    vm_port_print("Backtrace:\n");
    vm_print_stacktrace(vm_port_get_current_context()->frames);

    WARN("Restarting in 10 seconds...");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    esp_restart();
}

void vm_port_print(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);
    vlogger(fmt, vargs);
    va_end(vargs);
}

void vm_port_debug(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);
    vlogger(fmt, vargs);
    va_end(vargs);
}

void vm_port_unhandled_error(ErrorInfo &error) {
    vm_print_error(error);

    WARN("Restarting in 10 seconds...");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    esp_restart();
}

static VM *app_vm = nullptr;
static Context *app_ctx = nullptr;
static Value onready_callback = Value::Undefined();

Context *vm_port_get_current_context() {
    return app_ctx;
}

static Value api_onready(Context *ctx, int nargs, Value *args) {
    onready_callback = VM_GET_ARG(0);
    return Value::Undefined();
}

static Value api_print(Context *ctx, int nargs, Value *args) {
    std::string str = VM_GET_STRING_ARG(0);
    vm_port_print("%s\n", str.c_str());
    return Value::Undefined();
}

static Value api_publish(Context *ctx, int nargs, Value *args) {
    std::string name = VM_GET_STRING_ARG(0);
    Value value = VM_GET_ARG(1);

    char type;
    switch (value.type()) {
    case ValueType::Bool:
        type = 'b';
        break;
    case ValueType::Int:
        type = 'i';
        break;
    case ValueType::String:
        type = 's';
    break;
    default:
        type = 'u';
    }

    vm_port_print("@%s %c:%s\n", name.c_str(), type, value.toString().c_str());
    return Value::Undefined();
}

static Value api_delay(Context *ctx, int nargs, Value *args) {
    int ms = VM_GET_INT_ARG(0);
    vTaskDelay(ms / portTICK_PERIOD_MS);
    return Value::Undefined();
}

static Value api_pin_mode(Context *ctx, int nargs, Value *args) {
    int pin = VM_GET_INT_ARG(0);
    std::string mode_name = VM_GET_STRING_ARG(1);

    int mode;
    if (mode_name == "OUTPUT") {
        mode = VM_PORT_GPIO_OUTPUT;
    } else {
        return VM_CREATE_ERROR("Invalid mode");
    }

    VM_DEBUG("pinMode: %d %d", pin, mode);
    pinMode(pin, mode);
    return Value::Undefined();
}

static Value api_digital_write(Context *ctx, int nargs, Value *args) {
    int pin = VM_GET_INT_ARG(0);
    std::string level_name = VM_GET_STRING_ARG(1);
    int level;
    if (level_name == "HIGH") {
        level = HIGH;
    } else if (level_name == "LOW") {
        level = LOW;
    } else {
        return VM_CREATE_ERROR("Invalid level name");
    }

    VM_DEBUG("digitalWrite: %d %d", pin, level);
    digitalWrite(pin, level);
    return Value::Undefined();
}

#ifdef MAKESTACK_APP
extern void app_setup(Context *ctx);
#else
void app_setup(Context *ctx) {
    // app_setup() is automatically generated by the transpiler. This function
    // could be called if you build the firmware directly instead of the
    // makestack command.
    WARN("app is not embedded");
}
#endif

void run_app() {
    app_vm = new VM();
    app_ctx = app_vm->create_context();

    Value device_object = Value::Object();
    device_object.set(Value::String("onReady"), Value::Function(api_onready));
    app_vm->globals.set("device", device_object);
    app_vm->globals.set("print", Value::Function(api_print));
    app_vm->globals.set("publish", Value::Function(api_publish));
    app_vm->globals.set("delay", Value::Function(api_delay));
    app_vm->globals.set("pinMode", Value::Function(api_pin_mode));
    app_vm->globals.set("digitalWrite", Value::Function(api_digital_write));

    INFO("Initializing the app...");
    app_setup(app_ctx);

    INFO("Entering the onready callback...");
    if (onready_callback) {
        app_ctx->call(VM_CURRENT_LOC, onready_callback, 0, nullptr);
    }
}

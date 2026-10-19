#include "afe4950_task.h"

AfeTask :: AfeTask (AfeSensor& afe) : _afe(afe) {
}

void AfeTask :: begin (FrameSink sink, void* ctx, ByteSource source) {
    if (_taskHandle != nullptr) {
        return;
    }

    _sink = sink;
    _ctx = ctx;
    _source = source;
    _parser.reset();

    if (_source == nullptr) {
        _afe.start();
    };

    xTaskCreatePinnedToCore(_taskEntry, "AfeTask", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
}

uint32_t AfeTask :: frames() {
    return _frames;
}


void AfeTask :: _taskEntry (void* self) {
    static_cast<AfeTask*>(self)->_taskLoop();
}

void AfeTask :: _taskLoop(){
    AfeParser::Frame f;
    uint8_t b;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PollPeriodMs));

        while (_source ? _source(b) : _afe.readByte(b)) {
            if (_parser.push(b, f)) {
                _frames++;
                if (_sink) {
                    _sink(f, _ctx);
                }
            }
        }
    }
}
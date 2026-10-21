#include "afe4950_parser.h"

int32_t AfeParser::_toSigned24(uint32_t raw) {
    return (int32_t)(raw << 8) >> 8; 
}

void AfeParser::reset() {
    _n = 0;
    _phase = 0;
    _index = 0;
    _spo2Next = 0;
    _spo2Ready = false;
}

bool AfeParser :: _isValidTag(uint8_t tag) {
    return tag == TagHeader || tag == TagCount || (tag >=  TagSample && tag <= TagDcRed);
}


bool AfeParser :: push (uint8_t byte, Frame& out){
    _word [_n++] = byte;
    if (_n < 4) {
        return false; // no se completo la palabra de 4 bytes
    }
    uint8_t tag = _word[3];
    if (!_isValidTag(tag) ) {
        _word [0] = _word[1];
        _word [1] = _word[2];
        _word [2] = _word[3];
        _n = 3;
        _phase = 0;
        _spo2Next = 0;
        return false; // palabra no valida
    }
    _n=0;


    uint32_t raw = (uint32_t)_word[0] | ((uint32_t)_word[1] << 8) | ((uint32_t)_word[2] << 16);
    if (tag >= TagSpO2State) {
        _pushSpO2(tag, raw);
        return false; // palabra de SpO2
    }

    _spo2Next = 0;


    if (tag != TagSample) {
        return false; //  Las cabezeras no cambian de fase
    }

    int32_t value = _toSigned24(raw);

    if (_phase < 2){
        _ecg[_phase++] = value;
        return false; 
    }


    out.ecg[0] = _ecg[0];
    out.ecg[1] = _ecg[1];
    out.ppg = value;
    out.index = _index++;
    _phase = 0;
    return true; 
}

void AfeParser :: _pushSpO2 ( uint8_t tag, uint32_t raw) {
    if (tag == TagSpO2State) {
        _spo2Work = {};
        _spo2Work.state = raw & 0xFF;
        _spo2Work.secRemaining = (raw >> 8) & 0xFF;
        _spo2Next = TagSpO2State+1; 
        return;
    }

    if (tag != _spo2Next){
        _spo2Next = 0; 
        return ; // palabra fuera de secuencia
    }
    switch (tag){
        case TagAcIR: _spo2Work.acIR = raw; break;
        case TagDcIR: _spo2Work.dcIR = raw; break;
        case TagAcRed: _spo2Work.acRed = raw; break;
        case TagDcRed: _spo2Work.dcRed = raw; break;
    }
    if (tag == TagDcRed){
        _spo2Out = _spo2Work;
        _spo2Ready = true;
        _spo2Next = 0;
    }
    else {
        _spo2Next++;
    }
};

bool AfeParser :: takeSpO2(SpO2Params& out) {
    if (!_spo2Ready) {
        return false;
    }

    out = _spo2Out;
    _spo2Ready = false;
    return true;
}
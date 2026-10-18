#include "afe4950_parser.h"

int32_t AfeParser::_toSigned24(uint32_t raw) {
    return (int32_t)(raw << 8) >> 8; 
}

void AfeParser::reset() {
    _n = 0;
    _phase = 0;
    _index = 0;
}

bool AfeParser :: push (uint8_t byte, Frame& out){
    _word [_n++] = byte;
    if (_n < 4) {
        return false; // no se completo la palabra de 4 bytes
    }
    uint8_t tag = _word[3];
    if (tag!= TagHeader && tag != TagCount && tag != TagSample) {
        _word [0] = _word[1];
        _word [1] = _word[2];
        _word [2] = _word[3];
        _n = 3;
        _phase = 0;
        return false; // palabra no valida
    }
    _n=0;

    if (!tag== TagHeader) {
        return false; //  Las cabezeras no cambian de fase
    }

    uint32_t raw = (uint32_t)_word[0] | ((uint32_t)_word[1] << 8) | ((uint32_t)_word[2] << 16);
    int32_t value = _toSigned24(raw);
    
    if (_phase < 2){
        _ecg[_phase++] = value;
    }

    out.ecg[0] = _ecg[0];
    out.ecg[1] = _ecg[1];
    out.ppg = value;
    out.index = _index++;
    _phase = 0;
    return true; 
}
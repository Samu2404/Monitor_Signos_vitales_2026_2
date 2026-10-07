#pragma once 

#include <Arduino.h>
#include <EasyNextionLibrary.h>

class NextionHMI {

    // Metodos publicos de la clase
public:

    // Ids de las páginas en el editor del Nextion (posición en el panel Page)
    enum Page : uint8_t { PageMain = 0, PageFullWf = 1, PageConfig = 2, PageCal = 3 };


    /**
     * @brief Constructor de la clase NextionHMI
     * @param serial: Objeto de la clase puerto UART usado en la comunicacion
     */
    NextionHMI(HardwareSerial& serial=Serial2);
    
    /** @brief Inicializa la clase NextionHMI
     * @param baudRate: Velocidad de baudios para la comunicación con el Nextion
     * @param rxPin: Pin RX del puerto UART
     * @param txPin: Pin TX del puerto UART
     * @param RefreshRate: Tasa de refresco de la pantalla
     */
    void begin(uint32_t baudRate=9600 ,uint8_t rxPin=16, uint8_t txPin=17, uint32_t RefreshRate=60);
    
    /** @brief Grafica un valor en la pantalla Nextion en el canal y el ID especificados
     * @param id: ID del objeto en la pantalla
     * @param channel: Canal del objeto en la pantalla
     * @param value: Valor a graficar entre 0 y el MapValue de la gráfica
     * @note El valor se mapea a un rango de 0 a 255. Si la gráfica no fue registrada con
     *       configWaveform() se usa DefaultMapValue
     */
    void graphWaveform(uint8_t id, uint8_t channel, uint32_t value); 
    

    /** @brief Escribe el valor de SpO2 en la pantalla
     * @param spo2: Valor de SpO2 a escribir
     */
    void writeSpO2(int spo2);
    

    /** @brief Escribe el valor de BPM en la pantalla
     * @param bpm: Valor de BPM a escribir
     */
    void writeBPM(int bpm); 
    
    
    

    /** @brief Escribe el valor de la frecuencia del pulso en la pantalla
     * @param pulseRate: Valor de la frecuencia del pulso a escribir
     */
    void writePulseRate(int pulseRate); 
    

    /** @brief Escribe el valor de la variabilidad de la frecuencia cardíaca en la pantalla
     * @param hrVariance: Valor de la variabilidad de la frecuencia cardíaca a escribir
     */
    void writeHRvariance(int hrVariance); 


    /** @brief Escribe el valor de la temperatura en la pantalla
     * @param temperature: Valor de la temperatura a escribir
     */
    void writeTemperature(float temperature);  


    /** @brief Escribe el valor de la frecuencia respiratoria en la pantalla
     * @param respirationRate: Valor de la frecuencia respiratoria a escribir
     */
    void writeRespirationRate(int respirationRate); 


    /** @brief Registra una gráfica con su propia tasa de refresco
     * @param id: ID del objeto en la pantalla
     * @param channel: Canal del objeto en la pantalla
     * @param rateHz: Puntos por segundo que se envían a esta gráfica (0 = sin límite)
     * @param MapValue: Valor máximo de entrada de esta gráfica; el rango 0..MapValue se escala a 0..255
     *                  (0 = usar DefaultMapValue). Los valores mayores se recortan a MapValue
     * @return true si la solicitud entro en la cola
     * @note Si la gráfica ya estaba registrada se actualizan su tasa y su MapValue
     */
    bool configWaveform(uint8_t id, uint8_t channel, uint32_t rateHz, uint32_t MapValue = DefaultMapValue);


    /** @brief Actualiza el valor de la gráfica en la pantalla Nextion en el canal y el ID especificados
     * @param id: ID del objeto en la pantalla
     * @param channel: Canal del objeto en la pantalla
     * @param value: Valor a graficar entre 0 y el MapValue de la gráfica
     * @note El valor se mapea a un rango de 0 a 255 según el MapValue de configWaveform()
     * @note Cada gráfica (id, channel) tiene su propio temporizador. Si no fue registrada con
     *       configWaveform() se registra automáticamente con la tasa RefreshRate de begin()
     */
    void updateWaveform(uint8_t id, uint8_t channel, uint32_t value);


    /** @brief Actualiza los valores numéricos en la pantalla, enviando solo los que cambiaron
     * @param spo2: Valor de SpO2 a actualizar
     * @param bpm: Valor de BPM a actualizar
     * @param pulseRate: Valor de la frecuencia del pulso a actualizar
     * @param hrVariance: Valor de la variabilidad de la frecuencia cardíaca a actualizar
     * @param temperature: Valor de la temperatura a actualizar
     * @param respirationRate: Valor de la frecuencia respiratoria a actualizar
     * @note Se ejecuta como máximo cada ValuesPeriodMs
     */
    void updateValues(int spo2, int bpm,int pulseRate, int hrVariance, float temperature, int respirationRate);


    /** @brief Olvida los valores ya enviados y reenvía todos los números a la pantalla
     * @note Se hace solo al cargar cada página; llamarlo a mano solo si la pantalla se reinicia
     */
    void invalidateCache();


    /** @brief Página que está mostrando el Nextion
     * @note Cada página debe enviar en su Preinitialize Event: printh 23 02 50 + prints dp,1
     */
    Page currentPage() const { return (Page)_page; }


    // Metodos privados de la clase
private:
    friend void easyNexReadCustomCommand();   // EasyNex llama a esta función global con los comandos propios

    static constexpr uint8_t AllWaves = 0;    // _fullWfId: no se sabe cuál es visible, se envían todas
    static NextionHMI* _instance;             // Para que easyNexReadCustomCommand() llegue al objeto
    uint8_t _fullWfId = AllWaves;             // Id de la gráfica visible en full_wf

    void _onCustomCommand();                  // Maneja los comandos # <len> <grupo> que no son de EasyNex


    static constexpr uint8_t  MaxWaveforms   = 4;   // Gráficas simultáneas que se pueden registrar
    static constexpr uint32_t ValuesPeriodMs = 250; // Periodo mínimo entre revisiones de updateValues()
    static constexpr uint32_t DefaultMapValue = 4096; // MapValue por defecto (ADC de 12 bits)

    // Identificador de cada valor numérico dentro de la caché
    enum ValueId : uint8_t { ValSpO2, ValBPM, ValPulseRate, ValHRV, ValTemperature, ValRespiration, ValCount };

    // Estado de cada gráfica: su periodo y cuándo se envió su último punto
    struct WaveChannel {
        uint8_t  id;
        uint8_t  channel;
        uint32_t period;    // us entre puntos (0 = sin límite)
        uint32_t lastTime;  // micros() del último punto enviado
        uint32_t mapValue;  // Valor de entrada que equivale al tope de la gráfica (255)
    };

    struct msg {
        enum type {Wave, WaveUpdate, WaveConfig, Num, Invalidate };
        type type;
        uint8_t id;
        uint8_t channel;
        int32_t value;
        uint32_t mapValue;  // Solo lo usa WaveConfig
    };

    struct Vitals {
        int spo2;
        int bpm;
        int pulseRate;
        int hrVariance;
        float temperature;
        int respirationRate;
    };

    static constexpr int32_t Unknown = INT32_MIN;   // Valor aún no enviado / no recibido
    static constexpr int NoPageEvent = -1;          // Marca en EasyNex::currentPageId: no llegó ninguna página
    static const char* const NumObjects[ValCount];  // Objeto del Nextion de cada ValueId

    WaveChannel* _findWaveform(uint8_t id, uint8_t channel);
    void _setNum(ValueId slot, int32_t value);   // Guarda el valor y lo envía si se ve y cambió
    void _syncNum(ValueId slot);                 // Envía el valor guardado si la pantalla no lo tiene
    void _invalidate();                          // Solo desde la tarea del HMI (o el constructor)
    void _listen();                              // Lee los comandos que manda el Nextion
    void _onPageLoaded(uint8_t page);
    bool _showsNumbers() const { return _page == PageMain; }
    bool _showsWave(uint8_t id) const {
        if (_page == PageMain)   return true;
        if (_page == PageFullWf) return _fullWfId == AllWaves || _fullWfId == id;
        return false;
    }
    static void _taskEntry(void* self);
    void _taskLoop();
    void _sendWave (uint8_t id, uint8_t channel, uint32_t value, uint32_t MapValue);
    bool _configWave(uint8_t id, uint8_t channel, uint32_t rateHz, uint32_t MapValue = DefaultMapValue);
    void _updateWave(uint8_t id, uint8_t channel, uint32_t value);
    void _handle(const msg& m);
    void _enqueueNum(ValueId slot, int32_t value);

    void _flushVitals();
    QueueHandle_t _vitalsBox = nullptr;



    EasyNex _myNex;
    HardwareSerial& _serial;
    QueueHandle_t _queue=  nullptr;
    TaskHandle_t _taskHandle = nullptr;


    static constexpr uint8_t QueueSize = 64;
    static constexpr UBaseType_t TaskPriority = 1;
    static constexpr size_t TaskStackSize = 4096;
    static constexpr BaseType_t TaskCore = 1;
    static constexpr uint32_t TaskPollMs=5;

    
    uint32_t _baudRate = 0;
    uint32_t _refreshRate = 60;
    WaveChannel _waves[MaxWaveforms];
    uint8_t _waveCount = 0;
    int32_t _lastValues[ValCount];   // Último valor que tiene la pantalla (Unknown = desconocido)
    int32_t _wanted[ValCount];       // Último valor pedido por las tareas (Unknown = ninguno)
    uint32_t _lastValuesTime = 0;
    volatile uint8_t _page = PageMain;   // Al arrancar se asume main hasta que responda "sendme"
};


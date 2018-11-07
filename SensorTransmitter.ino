#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "SensorData.h"
#include "PMS5003Frame.h"
#include "BME280Proxy.h"
#include "PMS5003Proxy.h"
#include "RFM69TXProxy.h"
#include "FeatherOLEDProxy.h"

using namespace Sensors::BME280;
using namespace Sensors::PMS5003;
using namespace TX;
using namespace Display;

const unsigned long sampleFrequency = 60000; // ms (once per minute)
bool isFirstLoop = true;

BME280Proxy climateProxy(BME280Proxy::F);
PMS5003Proxy particleProxy;
RFM69TXProxy transmissionProxy;
FeatherOLEDProxy displayProxy;

void setup() {
    Serial.begin(115200);

    displayProxy.Initialize();
    climateProxy.Initialize();
    particleProxy.Initialize();
    transmissionProxy.Initialize();

    displayProxy.PrintWaiting();
}

void loop() {
    SensorData data;
    climateProxy.ReadSensor(&data);
    particleProxy.ReadSensor(&data);

    if(isFirstLoop) {
        displayProxy.Clear();
    }

    displayProxy.PrintSensors(data);
    data.PrintDebug();

    Serial.println("");

    TXResult result = transmissionProxy.Transmit(data);
    //displayProxy.PrintRFM69Update(&result);

    Serial.println("");
    isFirstLoop = false;
    delay(sampleFrequency);
}

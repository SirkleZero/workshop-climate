#include <Arduino.h>

#include "workshop-climate-lib.h" // unsure exactly why this has to be here for this to compile. without it, the sub-directory .h files aren't found. Probably has something to do with not finding the library if nothing is loaded from the root of the src folder.
#include "Sensors\SensorManager.h"
#include "Sensors\SensorData.h"
#include "Sensors\PMS5003Frame.h"
#include "Sensors\BME280Proxy.h"
#include "Sensors\PMS5003Proxy.h"
#include "TX\RFM69TXProxy.h"
#include "Display\FeatherOLEDProxy.h"

using namespace Display;
using namespace Sensors;
using namespace TX;

const unsigned long sampleFrequency = 60000; // ms (once per minute)
bool isFirstLoop = true;
SensorData data;

SensorManager manager(AvailableSensors::All);

BME280Proxy climateProxy(TemperatureUnit::F);
PMS5003Proxy particleProxy;
RFM69TXProxy transmissionProxy;
FeatherOLEDProxy displayProxy;

void setup() {
    Serial.begin(115200);

    //while (!Serial); // MAKE SURE TO REMOVE THIS!!!

    displayProxy.Initialize();
    climateProxy.Initialize();
    particleProxy.Initialize();
    transmissionProxy.Initialize();

    displayProxy.PrintWaiting();
}

void loop() {
    climateProxy.ReadSensor(&data);
    particleProxy.ReadSensor(&data);

    if(isFirstLoop) {
        displayProxy.Clear();
    }

    displayProxy.PrintSensors(data);
    
    TXResult result = transmissionProxy.Transmit(data);
    
    isFirstLoop = false;
    delay(sampleFrequency);
}

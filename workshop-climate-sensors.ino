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

static const unsigned long SampleFrequency = 20000; // every 20 seconds
bool isFirstLoop = true;
bool systemRunnable = true;
SensorData data;

SensorManager manager(AvailableSensors::All, TemperatureUnit::F, SampleFrequency);
RFM69TXProxy transmissionProxy;
FeatherOLEDProxy displayProxy;

void setup() {
    Serial.begin(115200);

    //while (!Serial); // MAKE SURE TO REMOVE THIS!!!

	if (displayProxy.Initialize().IsSuccessful)
	{
		displayProxy.PrintWaiting();

		if (manager.Initialize().IsSuccessful)
		{
			systemRunnable = transmissionProxy.Initialize().IsSuccessful;
		}
	}
}

void loop() {
	if (systemRunnable)
	{
		if (manager.ReadSensors(&data))
		{
			if (isFirstLoop)
			{
				displayProxy.Clear();
			}

			displayProxy.PrintSensors(data);

			TXResult result = transmissionProxy.Transmit(data);
			displayProxy.PrintTransmissionInfo(result);

			isFirstLoop = false;
		}
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper *msg = F("One or more components failed to initialize.");
		Serial.println(msg);
		displayProxy.PrintError(msg);
	}
}

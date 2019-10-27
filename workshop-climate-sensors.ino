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

static const unsigned long SampleFrequency = 15000; // every 15 seconds
//static const unsigned long SampleFrequency = 60000; // every 60 seconds
bool isFirstLoop = true;
bool systemRunnable = true;

SensorManager sensorManager(AvailableSensors::BME280, TemperatureUnit::F, SampleFrequency);
RFM69TXProxy transmissionProxy;
FeatherOLEDProxy displayProxy;
SensorData data;

void setup() {
    Serial.begin(115200);

    while (!Serial); // MAKE SURE TO REMOVE THIS!!!

	if (displayProxy.Initialize().IsSuccessful)
	{
		displayProxy.PrintWaiting();

		InitializationResult smr = sensorManager.Initialize();
		if (smr.IsSuccessful)
		{
			systemRunnable = transmissionProxy.Initialize().IsSuccessful;
		}
		else
		{
			displayProxy.PrintError(smr.ErrorMessage);
		}
	}
}

void loop() {
	// if all the checks from the Setup method ran successfully, we're good to run; otherwise, print an error message.
	if (systemRunnable)
	{
		// the manager uses a configurable timer, so call this method as often as possible.
		if (sensorManager.ReadSensors(&data))
		{
			// clear the waiting message from the display
			if (isFirstLoop)
			{
				displayProxy.Clear();
			}

			// print the information from the sensors.
			displayProxy.PrintSensors(data);

			// use the radio and transmit the data. when done, print some information about how the transmission went.
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

#include <Arduino.h>
#include <Adafruit_SleepyDog.h>

/*
unsure exactly why this has to be here for this to compile. without it, the sub-directory .h files
aren't found. Probably has something to do with not finding the library if nothing is loaded from
the root of the src folder.
*/
#include "workshop-climate-lib.h"

#include "Sensors\BME280Data.h"
#include "Display\ControllerDisplay.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\ControllerConfiguration.h"
#include "Sensors\BME280Proxy.h"
#include "TX\RFM69TXProxy.h"

using namespace Configuration;
using namespace Display;
using namespace Sensors;
using namespace TX;

BME280Data data;
ControllerConfiguration config;
bool isFirstLoop = true;
bool systemRunnable = true;

SDCardProxy sdCard;
ControllerDisplay display;
BME280Proxy bme280Proxy;
RFM69TXProxy transmissionProxy;

void setup()
{
	Serial.begin(115200);

	//while (!Serial); // MAKE SURE TO REMOVE THIS!!!

	if (display.Initialize().IsSuccessful)
	{
		display.Clear();
		display.DrawLayout();
		display.PrintSensors(BME280Data::EmptyData());

		// Radio chip select needs to be pulled up per this thread
		// https://forums.adafruit.com/viewtopic.php?f=47&t=120223&start=15
		//pinMode(8, INPUT_PULLUP);
		// OR
		// initialize the radio first.
		// no idea why this causes the sd card to fail initialization, but if we don't
		// do one of these options, we can't read the sd card...... lame.
		InitializationResult tr = transmissionProxy.Initialize();
		InitializationResult sdr = sdCard.Initialize();
		if (sdr.IsSuccessful)
		{
			sdCard.LoadConfiguration(&config);
		}
		InitializationResult bmep = bme280Proxy.Initialize(TemperatureUnit::F, config.PollIntervalMS);

		systemRunnable = bmep.IsSuccessful && tr.IsSuccessful && sdr.IsSuccessful;
		systemRunnable = true;

		// IMPORTANT! Turn on the watch dog timer and enable at the maximum value. For the M0 
		// this is approximately 16 seconds, after which the watch dog will restart the device.
		// This exists purely as a stability mechanism to mitigate device lockups / hangs / etc.
		Watchdog.enable();
		sdCard.LogMessage(F("Watchdog timer enabled during device setup."));
	}
}

void loop()
{
	// if all the checks from the Setup method ran successfully, we're good to run; otherwise, print an error message.
	if (systemRunnable)
	{
		// reset the watchdog with each loop iteration. If the loop hangs, the watchdog will reset the device.
		Watchdog.reset();

		// Sensor proxies use a configurable timer, so call this method as often as possible.
		if (bme280Proxy.ReadSensor(&data))
		{
			Watchdog.reset();

			// clear the waiting message from the display
			if (isFirstLoop)
			{
				//displayProxy.Clear();
			}

			// print the information from the sensors.
			display.PrintSensors(data);
			/*data.PrintDebug();
			bme280Proxy.PrintDebug();*/

			// use the radio and transmit the data. when done, print some information about how the transmission went.
			//TXResult result = transmissionProxy.Transmit(data);
			//displayProxy.PrintTransmissionInfo(result);
			//result.PrintDebug();

			Watchdog.reset();

			isFirstLoop = false;
		}
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper *msg = F("One or more components failed to initialize.");
		Serial.println(msg);
		//displayProxy.PrintError(msg);
	}
}

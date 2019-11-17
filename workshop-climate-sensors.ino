#include <Arduino.h>
#include <Adafruit_SleepyDog.h>

/*
unsure exactly why this has to be here for this to compile. without it, the sub-directory .h files
aren't found. Probably has something to do with not finding the library if nothing is loaded from
the root of the src folder.
*/
#include "workshop-climate-lib.h"

#include "Sensors\BME280Proxy.h"
#include "Sensors\BME280Data.h"
#include "Sensors\BufferedBME280.h"
#include "Display\ControllerDisplay.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\ControllerConfiguration.h"
#include "TX\RFM69TXProxy.h"

// set a boolean value that determines if we want serial debugging to work during the setup phase
bool enableSetupSerialWait = false;

using namespace Configuration;
using namespace Display;
using namespace Sensors;
using namespace TX;

BME280Data data;
BufferedBME280 bufferedData(120);
ControllerConfiguration config;
bool systemRunnable = true;

SDCardProxy sdCard;
ControllerDisplay display;
BME280Proxy bme280Proxy;
RFM69TXProxy transmissionProxy;

void setup()
{
	Serial.begin(115200);

	if (enableSetupSerialWait)
	{
		while (!Serial);
	}

	if (display.Initialize().IsSuccessful)
	{
		display.Clear();

		// NOTE!
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

		// TODO: Maybe instead of loading an empty set of data for display for like, a quarter
		// second, we display a message that we're booting and loading and stuff? Or a loading
		// icon or picture or something?
		display.LoadData(BME280Data::EmptyData());
		display.Display(ScreenRegion::Home);
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

			// print the information from the sensors.
			display.LoadData(data);
			display.Display();

			// use the radio and transmit the data. when done, print some information about how the transmission went.
			TXResult result = transmissionProxy.Transmit(data);
			result.PrintDebug();

			// TESTING array based moving average
			//Serial.print(F("Actual Humidity:	")); Serial.println(data.Humidity);
			
			bufferedData.Add(data); // buffered version of the bme data
			//Serial.print(F("BME280 Buffer:	")); Serial.println(bufferedData.Humidity);

			// display.LoadData(bufferedData);
			// END TESTING

			Watchdog.reset();
		}

		// update the display
		display.Display();
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper *msg = F("One or more components failed to initialize.");
		Serial.println(msg);
		//displayProxy.PrintError(msg);
	}
}

#include <Arduino.h>
#include <Wire.h>
#include <MemoryFree.h>
#include <pgmStrToRAM.h>
#include <Adafruit_SleepyDog.h>
#include <WiFiNINA.h>

/*
 unsure exactly why this has to be here for this to compile. without it, the
 sub-directory .h files aren't found. Probably has something to do with not
 finding the library if nothing is loaded from the root of the src folder.
*/
#include "workshop-climate-lib.h"

#include "Sensors\SensorData.h"
#include "RX\RFM69RXProxy.h"
#include "RX\SensorTransmissionResult.h"
#include "TX\AdafruitIOProxy.h"
#include "TX\IoTUploadResult.h"
#include "Display\RXTFTFeatherwingProxy.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\Secrets.h"
#include "Configuration\ControllerConfiguration.h"

using namespace Configuration;
using namespace Display;
using namespace RX;
using namespace Sensors;
using namespace TX;

// objects that store data
Secrets secrets;
SensorTransmissionResult result;
IoTUploadResult uploadResult;
ControllerConfiguration controllerConfiguration;
bool systemRunnable = true;
InitializationResult internetEnabled;

// objects that handle functionality
SDCardProxy sdCard;
RXTFTFeatherwingProxy display;
RFM69RXProxy radio;
AdafruitIOProxy httpClient;

/*
we need our modules in the following priority order:
1. SD Card - Contains all of our configuration. Can't run without this information.
2. The Display - we can technically function without a display, but we're going to use it 
   to display errors, so uh, it's required.
3. The RFM69 Radio - Send and receives data. Without it, again, no point.
4. The Internet / adafruit - we can function without this, it's the only component not required.
*/
void setup()
{
	Serial.begin(115200);
	//while (!Serial); // MAKE SURE TO REMOVE THIS!!!

	// cascading checks to make sure all our everything thats required is initialized properly.
	if (display.Initialize().IsSuccessful)
	{
		display.Clear();
		display.DrawLayout();

		display.PrintSensors(SensorData::EmptyData());
		display.PrintFreeMemory(freeMemory());

		if (sdCard.Initialize().IsSuccessful)
		{
			Serial.println("sd card initialized");
			sdCard.LoadSecrets(&secrets);
			sdCard.LoadConfiguration(&controllerConfiguration);
			Serial.println("configuration loaded");

			Serial.println("relay initialized");

			InitializationResult radioResult = radio.Initialize();
			if (radioResult.IsSuccessful)
			{
				Serial.println("Radio initialized");

				internetEnabled = httpClient.Initialize(&secrets);
				if (internetEnabled.IsSuccessful)
				{
					Serial.println("loaded internets");
					// establish a connection to the network.
					httpClient.Connect();
				}
				else
				{
					Serial.println("failed to load internets");
				}
			}
			else
			{
				systemRunnable = false;
				display.PrintError(radioResult.ErrorMessage);
				Serial.println(radioResult.ErrorMessage);
				sdCard.LogMessage(radioResult.ErrorMessage);
			}
		}

		// IMPORTANT! Turn on the watch dog timer and enable at the maximum value. For the M0 
		// this is approximately 16 seconds, after which the watch dog will restart the device.
		// This exists purely as a stability mechanism to mitigate device lockups / hangs / etc.
		Watchdog.enable();
		sdCard.LogMessage(F("Watchdog timer enabled during device setup."));
	}
}

void loop()
{
	if (systemRunnable)
	{
		// reset the watchdog with each loop iteration. If the loop hangs, the watchdog will reset the device.
		Watchdog.reset();

		/*
		!!! CRITICAL !!!
		the rxProxy listen function needs to execute as often as possible to not miss any messages or 
		acknowledgements. it would be bad for the loop to have a delay call in it, messages will be lost.
		DO NOT put a delay call in the loop function!
		*/
		result = radio.Listen();
		Watchdog.reset();

		if (result.HasResult)
		{
			display.PrintSensors(result.Data);
			display.PrintFreeMemory(freeMemory());

			Watchdog.reset();

			// if the internet isn't working for some reason, don't bother trying to upload anything.
			if (internetEnabled.IsSuccessful)
			{
				uploadResult = httpClient.Transmit(result.Data);
				// The http transmission is likely the most time consuming thing in this application.
				// Make sure to reset the watchdog after it's completed or the device will reboot!
				Watchdog.reset();

				if (!uploadResult.IsSuccess)
				{
					display.PrintError(uploadResult.ErrorMessage);
					Serial.println(uploadResult.ErrorMessage);
					sdCard.LogMessage(uploadResult.ErrorMessage);
				}
			}

			/*
			calling Reset on the radio is a total hack. It re-initializes the RF69 radio because the
			radio head library doesn't handle shared SPI bus very well (apparently). If we don't
			reinitialize this, the loop will catch only the first transmission, and after that it won't
			catch anything. This "fixes" that issue. Yes, it's dumb and shared SPI sucks, at least
			in this case.
			*/
			InitializationResult resetResult = radio.Reset();
			if (!resetResult.IsSuccessful)
			{
				// something didn't work here, so let's...

				display.PrintError(resetResult.ErrorMessage);
				Serial.println(resetResult.ErrorMessage);
				sdCard.LogMessage(resetResult.ErrorMessage);
			}

			// display free memory after things have run.
			display.PrintFreeMemory(freeMemory());
		}
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper *msg = F("One or more components failed to initialize or run.");
		Serial.println(msg);
		display.PrintError(msg);
	}
}

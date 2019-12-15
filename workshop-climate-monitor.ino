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

#include "RX\RFM69RXProxy.h"
#include "RX\SensorTransmissionResult.h"
#include "TX\AdafruitIOProxy.h"
#include "TX\IoTUploadResult.h"
#include "Display\TFTDisplay.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\Secrets.h"
#include "Sensors\BufferedBME280.h"

// set a boolean value that determines if we want serial debugging to work during the setup phase
bool enableSetupSerialWait = false;

using namespace Configuration;
using namespace Display;
using namespace RX;
using namespace Sensors;
using namespace TX;

// objects that store data
Secrets secrets;
SensorTransmissionResult result;
IoTUploadResult uploadResult;
InitializationResult internetEnabled;
BufferedBME280 sensorBuffer(20); // At 4 readings per minute, this will be a 5 minute buffer.
bool systemRunnable = true;
bool isFirstLoop = true;

// objects that handle functionality
SDCardProxy sdCard;
TFTDisplay display;
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

	if (enableSetupSerialWait)
	{
		while (!Serial);
	}

	// cascading checks to make sure all our everything thats required is initialized properly.
	if (display.Initialize().IsSuccessful)
	{
		display.Clear();

		display.LoadMessage(F("Display initialized..."));
		display.Display(ScreenRegion::StatusMessage);

		if (sdCard.Initialize().IsSuccessful)
		{
			display.LoadMessage(F("SD card initialized..."));
			display.Display(ScreenRegion::StatusMessage);

			sdCard.LoadSecrets(&secrets);
			
			display.LoadMessage(F("Configuration loaded..."));
			display.Display(ScreenRegion::StatusMessage);

			InitializationResult radioResult = radio.Initialize();
			if (radioResult.IsSuccessful)
			{
				display.LoadMessage(F("Radio initialized..."));
				display.Display(ScreenRegion::StatusMessage);

				internetEnabled = httpClient.Initialize(&secrets);
				if (internetEnabled.IsSuccessful)
				{
					display.LoadMessage(F("Networking initialized, connecting..."));
					display.Display(ScreenRegion::StatusMessage);

					// establish a connection to the network.
					if (httpClient.Connect())
					{
						display.LoadMessage(F("Connected to the network!"));
						display.Display(ScreenRegion::StatusMessage);
					}
					else
					{
						display.LoadMessage(F("Failed to connect to the network!"));
						display.Display(ScreenRegion::StatusMessage);
					}
				}
				else
				{
					display.LoadMessage(F("Failed to initialize networking!"));
					display.Display(ScreenRegion::StatusMessage);
				}
			}
			else
			{
				systemRunnable = false;
				display.LoadMessage(F("Failed to initialize the radio!"));
				display.Display(ScreenRegion::StatusMessage);
				sdCard.LogMessage(radioResult.ErrorMessage);
			}
		}
	
		// we're done loading things, display a waiting message
		display.LoadMessage(F("Waiting on sensor transmission..."));
		display.Display(ScreenRegion::StatusMessage);
		
		// IMPORTANT! Turn on the watch dog timer and enable at the maximum value. For the M0 
		// this is approximately 16 seconds, after which the watch dog will restart the device.
		// This exists purely as a stability mechanism to mitigate device lockups / hangs / etc.
		sdCard.LogMessage(F("System booted successfully."));
		Watchdog.enable();
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
			sensorBuffer.Add(result.Data);
			Watchdog.reset();

			// print the information from the sensors.
			display.LoadSensorData(sensorBuffer);
			if (isFirstLoop)
			{
				display.Display(ScreenRegion::Home);
				isFirstLoop = false;
			}
			display.Display();
			Watchdog.reset();

			// if the internet isn't working for some reason, don't bother trying to upload anything.
			if (internetEnabled.IsSuccessful)
			{
				uploadResult = httpClient.Transmit(sensorBuffer);
				// The http transmission is likely the most time consuming thing in this application.
				// Make sure to reset the watchdog after it's completed or the device will reboot!
				Watchdog.reset();

				if (!uploadResult.IsSuccess)
				{
					Serial.println(uploadResult.ErrorMessage);
					sdCard.LogMessage(uploadResult.ErrorMessage);
					Watchdog.reset();
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
				Serial.println(resetResult.ErrorMessage);
				sdCard.LogMessage(resetResult.ErrorMessage);
				Watchdog.reset();
			}
		}

		// update the display
		display.Display();
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper *msg = F("One or more components failed to initialize or run.");
		Serial.println(msg);
	}
}

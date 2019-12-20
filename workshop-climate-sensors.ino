#include <Arduino.h>
#include <Adafruit_SleepyDog.h>

/*
unsure exactly why this has to be here for this to compile. without it, the sub-directory .h files
aren't found. Probably has something to do with not finding the library if nothing is loaded from
the root of the src folder.
*/
#include "workshop-climate-lib.h"

#include "Devices.h"
#include "Sensors\BME280Proxy.h"
#include "Sensors\BME280Data.h"
#include "Sensors\BufferedBME280.h"
#include "Display\TFTDisplay.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\ControllerConfiguration.h"
#include "TX\RFM69TXProxy.h"
#include "Relay\HumidityRelayManager.h"

// set a boolean value that determines if we want serial debugging to work during the setup phase
bool enableSetupSerialWait = false;

using namespace Configuration;
using namespace Display;
using namespace Sensors;
using namespace TX;
using namespace Relay;

BME280Data data;
BufferedBME280 bufferedData(120);
ControllerConfiguration config;
Devices mode = Devices::Controller;
bool systemRunnable = true;
bool isFirstLoop = true;

SDCardProxy sdCard;
TFTDisplay display;
BME280Proxy bme280Proxy;
RFM69TXProxy transmissionProxy;
HumidityRelayManager relayManager;

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
		InitializationResult relayManagerResult = relayManager.Initialize(&config);

		InitializationResult bmep = bme280Proxy.Initialize(TemperatureUnit::F, config.PollIntervalMS);

		systemRunnable = bmep.IsSuccessful && tr.IsSuccessful && sdr.IsSuccessful;
		systemRunnable = true;

		// we're done loading things, display a waiting message
		display.LoadMessage(F("Waiting on sensors..."));
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
	// if all the checks from the Setup method ran successfully, we're good to run; otherwise, print an error message.
	if (systemRunnable)
	{
		// reset the watchdog with each loop iteration. If the loop hangs, the watchdog will reset the device.
		Watchdog.reset();

		switch (mode)
		{
			case Devices::Controller:
				RunAsController();
				break;
			case Devices::ClimateSensor:
				RunAsSensor();
				break;
			default:
				break;
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

void RunAsController()
{
	Watchdog.reset();

	/*
	If we are configured in controller mode:
	1. Read from the local sensors
	2. Transmit local sensor readings
	3. Listen for transmissions from remote climate sensors
	4. Buffer local and remote data in the buffered data structure
	5. Use the relay manager to control the humidifier and dehumidifier based on the
	buffered data
	*/

	// Sensor proxies use a configurable timer, so call this method as often as possible.
	if (bme280Proxy.ReadSensor(&data))
	{
		Watchdog.reset();

		// display information from the sensors.
		DisplayReadings(data);

		// use the relay manager to adjust humidification based on sensor data.
		relayManager.AdjustClimate(data);

		TransmitData(data);

		Watchdog.reset();
	}

	// update the display
	display.Display();

	/*
	Use the emergency shutoff function to shut off the relays if a pre-determined time amount has lapsed.
	All of this logic is within this method, no other calls are necessary. The KeepAlive() method is
	essentially a dead man switch that this method uses to either keep things going, or, if the sensor array
	functionality doesn't transmit anything or we don't receive anything, we shut down power to all our devices.
	This is a safety thing.
	*/
	relayManager.EmergencyShutoff();
}

void RunAsSensor()
{
	Watchdog.reset();

	/*
	If we are configured in Sensor mode:
	1. Read from local sensors
	2. Transmit local sensor readings
	*/

	// Sensor proxies use a configurable timer, so call this method as often as possible.
	if (bme280Proxy.ReadSensor(&data))
	{
		Watchdog.reset();

		// display information from the sensors.
		DisplayReadings(data);

		// use the relay manager to adjust humidification based on sensor data.
		relayManager.AdjustClimate(data);

		TransmitData(data);

		Watchdog.reset();
	}

	// update the display
	display.Display();
}

void DisplayReadings(BME280Data sensorData)
{
	display.LoadSensorData(sensorData);
	if (isFirstLoop)
	{
		display.Display(ScreenRegion::Home);
		isFirstLoop = false;
	}
	display.Display();
	Watchdog.reset();
}

void TransmitData(BME280Data sensorData)
{
	TXResult result = transmissionProxy.Transmit(sensorData);
	result.PrintDebug();
}

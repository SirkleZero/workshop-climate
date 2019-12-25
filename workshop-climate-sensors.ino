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
#include "RFM69\RFM69Proxy.h"
#include "Relay\HumidityRelayManager.h"

// set a boolean value that determines if we want serial debugging to work during the setup phase
bool enableSetupSerialWait = false;

using namespace Configuration;
using namespace Display;
using namespace Sensors;
using namespace RFM69;
using namespace Relay;

// fields to update as things change
BufferedBME280 bufferedData(40);
Devices mode = Devices::HumidificationController;
//Devices mode = Devices::RemoteSensor1;
RFM69Proxy radioProxy(mode, 915.0, 8, 3, 4, 13);

ControllerConfiguration config;
BME280Data data;
SDCardProxy sdCard;
TFTDisplay display;
BME280Proxy bme280Proxy;
HumidityRelayManager relayManager;
bool systemRunnable = true;
bool isFirstLoop = true;

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

		InitializationResult rpr = radioProxy.Initialize();
		InitializationResult sdr = sdCard.Initialize();
		if (sdr.IsSuccessful)
		{
			sdCard.LoadConfiguration(&config);
		}
		InitializationResult relayManagerResult = relayManager.Initialize(&config);

		InitializationResult bmep = bme280Proxy.Initialize(TemperatureUnit::F, config.PollIntervalMS);

		systemRunnable = bmep.IsSuccessful && rpr.IsSuccessful && sdr.IsSuccessful;
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
			case Devices::HumidificationController:
				RunAsController();
				break;
			case Devices::RemoteSensor1:
				RunAsSensor();
				break;
			default:
				RunAsController();
				break;
		}
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper* msg = F("One or more components failed to initialize.");
		Serial.println(msg);
		//displayProxy.PrintError(msg);
	}
}

void RunAsController()
{
	Watchdog.reset();

	/*
	Use the emergency shutoff function to shut off the relays if a pre-determined time amount has lapsed.
	All of this logic is within this method, no other calls are necessary. The KeepAlive() method is
	essentially a dead man switch that this method uses to either keep things going, or, if the sensor array
	functionality doesn't transmit anything or we don't receive anything, we shut down power to all our devices.
	This is a safety thing.
	*/
	relayManager.EmergencyShutoff();

	/*
	If we are configured in controller mode:
	1.	Listen for transmissions from remote climate sensors
	2.	Read from the local sensors
	3.	Transmit local sensor readings
	4.	Buffer local and remote data in the buffered data structure
	5.	Use the relay manager to control the humidifier and dehumidifier based on the
	buffered data
	*/

	bool dataReceived = false;

	// Sensor proxies use a configurable timer, so call this method as often as possible.
	if (bme280Proxy.ReadSensor(&data))
	{
		dataReceived = true;

		// add the sensor data to the buffer
		bufferedData.Add(data);

		Serial.print(F("Read my own data, the value was: "));
		Serial.println(data.Humidity);

		// make sure to transmit the raw, unbuffered data!
		TransmitData(data);
	}
	Watchdog.reset();

	// now, listen to see if we get any data that might be sent by another sensor array, 
	// call this method as often as possible.
	SensorTransmissionResult str = radioProxy.ListenForBME280();
	if (str.HasResult)
	{
		dataReceived = true;

		bufferedData.Add(str.Data);
		Serial.print(F("Got a remote reading, the value was: "));
		Serial.println(str.Data.Humidity);
	}
	Watchdog.reset();

	if (dataReceived)
	{
		Serial.print(F("The rolling humidity average is: "));
		Serial.println(bufferedData.Humidity);
	}

	// display the buffered data
	DisplayReadings(bufferedData);
	Watchdog.reset();

	// use the relay manager to adjust humidification based on buffered sensor data.
	relayManager.AdjustClimate(bufferedData);
	Watchdog.reset();

	// update the display
	display.Display();
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

		Serial.print(F("Read my own data, the value was: "));
		Serial.println(data.Humidity);

		// transmit sensor data to either a controller or a monitor (or both!)
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

	switch (mode)
	{
		case Devices::HumidificationController:
		{
			// the humidification controller only needs to send its sensor data to the system monitor
			TXResult r = radioProxy.TransmitBME280(sensorData, Devices::SystemMonitor);
			break;
		}
		case Devices::RemoteSensor1:
		{
			// remote sensors need to send their data to both the controller and the system monitor
			TXResult r = radioProxy.TransmitBME280(sensorData, Devices::HumidificationController);
			r.PrintDebug();
			r = radioProxy.TransmitBME280(sensorData, Devices::SystemMonitor);
			r.PrintDebug();

			break;
		}
		default:
			return;
	}
}

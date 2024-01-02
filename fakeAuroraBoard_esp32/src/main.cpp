#include <Arduino.h>
#include <BLEDevice.h>
#include <vector>
#include <Adafruit_NeoPixel.h>

#define PIN_NEO_PIXEL 16 // The ESP32 pin GPIO16 connected to NeoPixel
#define NUM_PIXELS 500	 // The number of LEDs (pixels) on NeoPixel LED strip

Adafruit_NeoPixel NeoPixel(NUM_PIXELS, PIN_NEO_PIXEL, NEO_RGB + NEO_KHZ800);

class Hold
{
private:
	uint16_t position;
	int red;
	int green;
	int blue;

public:
	Hold(uint16_t position, int red, int green, int blue)
		: position(position), red(red), green(green), blue(blue) {}

	uint16_t getPosition() const
	{
		return position;
	}

	int getRed() const
	{
		return red;
	}

	int getGreen() const
	{
		return green;
	}

	int getBlue() const
	{
		return blue;
	}

	String toString() const
	{
		// Convert hold information to a string
		return "Position: " + String(position) +
			   ", Red: " + String(red) +
			   ", Green: " + String(green) +
			   ", Blue: " + String(blue);
	}
};

class DataDecoder
{
private:
	int currentPacketLength = -1;
	std::vector<uint8_t> currentPacket;
	std::vector<Hold> holds;
	bool allPacketsReceived = false;

public:
	DataDecoder(){};

	void newByteIn(int dataByte)
	{
		if (allPacketsReceived)
		{
			allPacketsReceived = false;
			holds.clear();
		}

		if (currentPacket.size() == 0 && dataByte != 1)
		{
			return;
		}

		currentPacket.push_back(dataByte);

		if (currentPacket.size() == 2)
		{
			currentPacketLength = dataByte + 5;
		}
		else if (currentPacket.size() == currentPacketLength)
		{
			if (!verifyAndParsePacket())
			{
				holds.clear();
			}
			else
			{
				allPacketsReceived = isThisTheLastPacket();
			}

			currentPacket.clear();
			currentPacketLength = -1;
		}
	}

	bool getAllPacketsReceived()
	{
		return allPacketsReceived;
	}

	std::vector<Hold> getHolds()
	{
		return holds;
	}

private:
	bool verifyAndParsePacket()
	{
		if (checksum(std::vector<uint8_t>(currentPacket.begin() + 4, currentPacket.begin() + currentPacketLength - 1)) !=
			static_cast<int>(currentPacket[2]))
		{
			Serial.println("ERROR: checksum invalid");
			return false;
		}

		if ((holds.size() == 0 && !isThisTheFirstPacket()) ||
			(holds.size() > 0 && isThisTheFirstPacket()))
		{
			Serial.println("ERROR: invalid packet order");
			return false;
		}

		for (size_t i = 5; i < currentPacketLength - 1; i += 3)
		{
			uint16_t position = (currentPacket[i + 1] << 8) + currentPacket[i];
			std::vector<int> clr = scaledColorToFullColorV3(currentPacket[i + 2]);

			// Assume Hold class has a constructor that takes x, y, r, g, b as parameters
			Hold hold = Hold(position, clr[0], clr[1], clr[2]);
			holds.push_back(hold);
		}

		return true;
	}

	bool isThisTheFirstPacket()
	{
		return (currentPacket[4] == 84 || currentPacket[4] == 82);
	}

	bool isThisTheLastPacket()
	{
		return (currentPacket[4] == 84 || currentPacket[4] == 83);
	}

	int checksum(std::vector<uint8_t> list)
	{
		int i = 0;
		for (uint8_t byteValue : list)
		{
			i = (i + byteValue) & 255;
		}
		return (~i) & 255;
	}

	std::vector<int> scaledColorToFullColorV3(int holdData)
	{
		std::vector<int> fullColor(3);
		fullColor[2] = static_cast<int>(((holdData & 0b00000011) >> 0) / 3. * 255.);
		fullColor[1] = static_cast<int>(((holdData & 0b00011100) >> 2) / 7. * 255.);
		fullColor[0] = static_cast<int>(((holdData & 0b11100000) >> 5) / 7. * 255.);
		return fullColor;
	}
};

// The name that will come up in the list in the kilter board app.
// Must be alphanumeric.
#define DISPLAY_NAME "TK Lielahti Kilter"

// Aurora API level. must be nonzero, positive, single-digit integer.
// API level 3+ uses a different protocol than API levels 1 and 2 and below.
#define API_LEVEL 3

// Extracted by decompiling kilter board app, in file BluetoothServiceKt.java
#define ADVERTISING_SERVICE_UUID "4488B571-7806-4DF6-BCFF-A2897E4953FF"
#define DATA_TRANSFER_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define DATA_TRANSFER_CHARACTERISTIC "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class BLECharacteristicCallbacksOverride : public BLECharacteristicCallbacks
{
public:
	DataDecoder dd;

	// Whenever data is sent to the esp32 we just immediately pass it onto the dekstop app to do all of
	void onWrite(BLECharacteristic *pCharacteristic)
	{
		if (pCharacteristic->getUUID().toString() == BLEUUID(DATA_TRANSFER_CHARACTERISTIC).toString())
		{
			// Serial.write(pCharacteristic->getValue().c_str(), pCharacteristic->getValue().length());
			std::string value = pCharacteristic->getValue();

			for (int i = 0; i < value.length(); i++)
			{
				dd.newByteIn(value[i]);
				bool allReceived = dd.getAllPacketsReceived();
				if (allReceived)
				{
					NeoPixel.clear();
					NeoPixel.show();
					for (const auto &hold : dd.getHolds())
					{
						Serial.println(hold.toString());
						NeoPixel.setPixelColor(hold.getPosition(), NeoPixel.Color(hold.getRed(), hold.getGreen(), hold.getBlue())); // it only takes effect if pixels.show() is called
					}
					NeoPixel.show();
					Serial.println();
				}
			}
		}
	}
};

bool restartAdvertising = false;

// When a device connects or disconnects the server will stop advertising, so we need to restart it to be discoverable again.
class BLEServerCallbacksOverride : public BLEServerCallbacks
{
	void onConnect(BLEServer *pServer)
	{
		restartAdvertising = true;
	}
	void onDisconnect(BLEServer *pServer)
	{
		restartAdvertising = true;
	}
};

BLECharacteristicCallbacksOverride characteristicCallbacks;
BLEServerCallbacksOverride serverCallbacks;
BLEServer *bleServer = nullptr;

void setup()
{
	Serial.begin(9600);
	Serial.write(4);
	Serial.write(API_LEVEL);

	NeoPixel.begin(); // initialize NeoPixel strip object (REQUIRED)

	char boardName[2 + sizeof(DISPLAY_NAME)];
	snprintf(boardName, sizeof(boardName), "%s%s%d", DISPLAY_NAME, "@", API_LEVEL);
	BLEDevice::init(boardName);
	bleServer = BLEDevice::createServer();
	bleServer->setCallbacks(&serverCallbacks);

	// This service + characteristic is how the app sends data to the board
	BLEService *service = bleServer->createService(DATA_TRANSFER_SERVICE_UUID);
	BLECharacteristic *characteristic = service->createCharacteristic(DATA_TRANSFER_CHARACTERISTIC, BLECharacteristic::PROPERTY_WRITE);
	characteristic->setCallbacks(&characteristicCallbacks);
	service->start();

	// Advertising service, this is how the app detects an Aurora board
	BLEService *advertisingService = bleServer->createService(ADVERTISING_SERVICE_UUID);
	advertisingService->start();

	BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
	pAdvertising->addServiceUUID(ADVERTISING_SERVICE_UUID);
	pAdvertising->setScanResponse(true);
	pAdvertising->setMinPreferred(0x06); // Functions that help with iPhone connections issue
	pAdvertising->setMinPreferred(0x12);
	BLEDevice::startAdvertising();
}

void loop()
{
	if (restartAdvertising)
	{
		delay(500); // Let the bluetooth hardware sort itself out
		restartAdvertising = false;
		bleServer->startAdvertising();
	}
}
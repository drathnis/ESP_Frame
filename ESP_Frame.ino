#include "NoGit.h"
#include <FastLED.h>
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <WebSocketsServer.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.hpp>
#include <ArduinoJson.h>


typedef enum {
	set_power,
	toggle,
	set_bright,
	set_rgb,
	set_hsv,
	set_strip,
	set_ct,
	set_animation,
	not_recognized
} availableModes;


typedef enum {
	rgb_mode,
	animation_mode,
} colorModes;

typedef enum {
	wheel_animation,
	chase_animation,
	raindbow_animation,
	none,
} animations;

#define SSID		YOUR_SSID
#define PASSKEY		YOUR_PASS



#define BUFFER_LENGTH 256


const int localPort = 55443;
IPAddress multiCastAddress(239, 255, 255, 250);
unsigned int multiCastPort = 1982;
char incomingPacket[BUFFER_LENGTH];


WiFiUDP udp;
WiFiUDP udp_multi;
WiFiServer wifiServer(localPort);



String deviceID = "0x0000";
String deviceName = "ESP_Frame";
String power = "off";
int brightness = 0;
int sat = 0;
int hue = 0;
int colorTemp = 0;
int rgbInt = 0;
int firmwareVersion = 1;
int colorMode = rgb_mode;
int selctedAnimation = none;

String replyMSG;

StaticJsonDocument<1024> doc;





#define LED_PIN_TOP			14
#define LED_PIN_BOTTOM      4
#define NUM_LEDS			27
#define BRIGHTNESS			64
#define LED_TYPE			WS2812B
#define COLOR_ORDER			GRB
#define NUM_STRIPS			2



class LEDstrip {
public:
	byte mode = 0;
	byte last_mode = 0;
	byte option = 0;
	byte option2 = 0;
	CRGB leds[NUM_LEDS];
};



CRGB lastStateTop[NUM_LEDS];
CRGB lastStateBottom[NUM_LEDS];
int chosenStrip = 2;

CRGB colorHolder;

LEDstrip topStrip;
LEDstrip bottomStrip;
LEDstrip strip[] = { topStrip, bottomStrip };

CLEDController* controllers[NUM_STRIPS];

void wifiInit();
String createResponse();
void UDP_multiAnnounce();
void udp_multicastListener();
void udp_reply(IPAddress address, uint16_t port);
void tcp_Control();
void processCommand(const String cmd);
int getModeValue(String cmd);
void led_clear();
void SetColor();
int getModeValue(String cmd);
void setColorHsv();
void setColorRgb(int rgb);
void toggle_strip();
void setBrightness();
void saveState();
void setColorTemp();
CRGB getRGBfromTemperature();


void setup() {
	
	Serial.begin(115200);
	wifiInit();

	deviceID = String(ESP.getChipId());

	replyMSG = createResponse();

	Serial.println(replyMSG);

	UDP_multiAnnounce();


	controllers[0] = &FastLED.addLeds<LED_TYPE, LED_PIN_TOP, COLOR_ORDER>(strip[0].leds, NUM_LEDS).setCorrection(
		TypicalLEDStrip);
	controllers[1] = &FastLED.addLeds<LED_TYPE, LED_PIN_BOTTOM, COLOR_ORDER>(strip[1].leds, NUM_LEDS).setCorrection(
		TypicalLEDStrip);
	


}

void loop() {
  


	udp_multicastListener();

	tcp_Control();
	
	
	//testing

	chaseAnimation();

	if (colorMode == animation_mode){
		runAnimation();
	}


}

void runAnimation() {

	switch (selctedAnimation){
		case wheel_animation:
			wheelAnimation();
			break;
		case chase_animation:
			chaseAnimation();
			break;
		case raindbow_animation:
			raindbowAnimation();
			break;

	default:
		break;
	}


}

void wheelAnimation() {



}


void chaseAnimation() {

	const long interval = 50;

	static unsigned long previousMillis = 0;


	unsigned long currentMillis = millis();

	if (currentMillis - previousMillis >= interval) {

		previousMillis = currentMillis;
		chase_do();




	}
}

void chase_do() {

	static int pos = 0;
	static bool top = true;
	static int currentStrip = 0;


	if (top){

		pos++;

		if(pos == NUM_LEDS) {
			currentStrip = 1;
			top = false;
		}



	}


	if (!top) {

		pos--;

		if (pos == 0) {
			currentStrip = 0;
			top = true;
		}

	}


	for (size_t i = 0; i < NUM_LEDS; i++)
	{
		strip[0].leds[i].r = 255;
		strip[1].leds[1].r = 255;
		strip[0].leds[i].b = 0;
		strip[1].leds[1].b = 0;
		if (i == pos)
		{
			
			strip[currentStrip].leds[i].r = 0;
			strip[currentStrip].leds[i].b = 255;

			if (pos > 0 && top)
			{
				strip[currentStrip].leds[i-1].r = 255;
				strip[currentStrip].leds[i - 1].b = 0;
			}
			else if (pos < NUM_LEDS && !top)
			{
				strip[currentStrip].leds[i + 1].r = 255;
				strip[currentStrip].leds[i + 1].b = 0;
			}
			


		}

	}


	Serial.println(pos);

	controllers[0]->showLeds();
	controllers[1]->showLeds();


}


void raindbowAnimation() {

	const int FRAMES_PER_SECOND = 120;
	static uint8_t gHue = 0;
	//static uint8_t rHue = 255;
	EVERY_N_MILLISECONDS(20) { gHue++; }

	//rHue--;

	fill_rainbow(strip[0].leds, NUM_LEDS, gHue, 7);
	fill_rainbow(strip[1].leds, NUM_LEDS, gHue, 7);

	controllers[0]->showLeds();
	controllers[1]->showLeds();

	delay(1000 / FRAMES_PER_SECOND);


}

void wifiInit() {
	WiFi.mode(WIFI_STA);
	wifi_set_sleep_type(NONE_SLEEP_T);

	WiFi.begin(SSID, PASSKEY);
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print('.');
		delay(500);
	}
	Serial.print("Connected! IP address: ");
	Serial.println(WiFi.localIP());
	Serial.printf("UDP server on port %d\n", localPort);


	//UDP Setup
	udp.begin(localPort);
	udp_multi.beginMulticast(WiFi.localIP(), multiCastAddress, multiCastPort);

	//TCP Setup
	wifiServer.begin();
}

String createResponse() {

	const String ip = WiFi.localIP().toString();
	String response = "Location: esp://" + ip + ":" + localPort;

	response += "\n";
	response += "id: " + deviceID;
	response += "\n";
	response += "model: Strip";
	response += "\n";
	response += "fw_ver: " + String(firmwareVersion);
	response += "\n";
	response += "support: set_power toggle set_bright set_rgb set_strip";
	response += "\n";
	response += "power: " + String(power);
	response += "\n";
	response += "bright: " + String(brightness);
	response += "\n";
	response += "color_mode: " + String(colorMode);
	response += "\n";
	response += "ct: " + String(colorTemp);
	response += "\n";
	response += "rgb: " + String(rgbInt);
	response += "\n";
	response += "hue: " + String(int(hue));
	response += "\n";
	response += "sat: " + String(int(sat));
	response += "\n";
	response += "name: " + deviceName;

	return response;
}

void UDP_multiAnnounce() {

	Serial.println("Sending Announcement");
	udp_multi.write(replyMSG.c_str());
	udp_multi.endPacket();

}

void udp_multicastListener() {

	const int packetLength = udp_multi.parsePacket();
	if (packetLength) {
		int len = udp_multi.read(incomingPacket, BUFFER_LENGTH);
		if (len > 0) {
			incomingPacket[len] = 0;
			const String message = incomingPacket;


			Serial.print("From ");
			IPAddress remote = udp_multi.remoteIP();
			const uint16_t remotePort = udp_multi.remotePort();
			for (int i = 0; i < 4; i++) {
				Serial.print(remote[i], DEC);
				if (i < 3) {
					Serial.print(".");
				}
			}
			Serial.printf(":%d", remotePort);
			Serial.println("");

			if (message.substring(0, 8) == "M-SEARCH") {
				Serial.println("Responding to Discovery");
				udp_reply(remote, remotePort);
			}

		}
	}


}

void udp_reply(IPAddress address, uint16_t port) {
	udp.beginPacket(address, port);
	udp.write(replyMSG.c_str());
	udp.endPacket();
}

void tcp_Control() {
	WiFiClient client = wifiServer.available();
	if (client) {
		while (client.connected()) {

			udp_multicastListener();

			while (client.available() > 0) {
				const String cmd = client.readStringUntil('\n');

				processCommand(cmd);
			}

			delay(5);
		}

		client.stop();
		Serial.println("Client disconnected");
	}
}

void processCommand(const String cmd) {
	deserializeJson(doc, cmd);

	const char* method = doc["method"];

	switch (getModeValue(method)) {
	case set_power:
		colorMode = rgb_mode;
		Serial.println("set_power");
		Serial.println("TODO");
		//TODO
		break;
	case toggle:
		colorMode = rgb_mode;
		Serial.println("toggle");
		toggle_strip();
		break;
	case set_bright:
		Serial.print("set_bright: ");
		brightness = doc["params"][0];
		setBrightness();
		colorMode = rgb_mode;
		break;
	case set_rgb:
		Serial.println("set_rgb");
		setColorRgb(doc["params"][0]);
		SetColor();
		colorMode = rgb_mode;
		break;

	case set_hsv:
		colorMode = rgb_mode;
		Serial.println("set_hsv");
		hue = doc["params"][0];
		sat = doc["params"][1];
		setColorHsv();
		SetColor();
		break;
	case set_strip:
		colorMode = rgb_mode;
		Serial.println("set_strip");
		chosenStrip = doc["params"][0];
		break;
	case set_ct:
		colorMode = rgb_mode;
		Serial.println("set_ct");
		colorTemp = doc["params"][0];
		Serial.println(colorTemp);
		setColorTemp();
		break;
	case set_animation:
		colorMode = animation_mode;
		selctedAnimation = doc["params"][0];

	case not_recognized:
	default:
		Serial.println("not_recognized");
	}
}

int getModeValue(String cmd) {
	if (cmd == "set_power") {
		return set_power;
	}
	if (cmd == "toggle") {
		return toggle;
	}
	if (cmd == "set_bright") {
		return set_bright;
	}
	if (cmd == "set_rgb") {
		return set_rgb;
	}
	if (cmd == "set_hsv") {
		return set_hsv;
	}
	if (cmd == "set_strip") {
		return set_strip;
	}
	if (cmd == "set_ct_abx") {
		return set_ct;
	}


	Serial.println("MODE NOT RECOGNIZED");

	return not_recognized;
}

void setColorTemp() {

	const CRGB colr = getRGBfromTemperature();

	if (chosenStrip >= 2) {

		for (int i = 0; i < NUM_LEDS; i++) {

			strip[0].leds[i] = colr;
			strip[1].leds[i] = colr;

		}
		controllers[0]->showLeds();
		controllers[1]->showLeds();
	} else {
		for (int i = 0; i < NUM_LEDS; i++) {
			strip[chosenStrip].leds[i] = colr;

		}
		controllers[chosenStrip]->showLeds();
	}

}

void setBrightness() {

	const int amount = map(brightness, 1, 100, 1, 255);

	Serial.println(amount);


	if (chosenStrip >= 2) {

		for (int i = 0; i < NUM_LEDS; i++) {

			strip[0].leds[i].maximizeBrightness();
			strip[1].leds[i].maximizeBrightness();

			strip[0].leds[i] %= amount;
			strip[1].leds[i] %= amount;

		}
		controllers[0]->showLeds();
		controllers[1]->showLeds();
	} else {
		for (int i = 0; i < NUM_LEDS; i++) {
			strip[chosenStrip].leds[i].maximizeBrightness();
			strip[chosenStrip].leds[i] %= amount;

		}
		controllers[chosenStrip]->showLeds();
	}

}

void toggle_strip() {

	if (power == "on") {
		power = "off";
		Serial.println("turning off");

		led_clear();
	} else if (power == "off") {
		power = "on";
		Serial.println("turning on");
		for (auto i = 0; i < NUM_LEDS; i++) {


			strip[0].leds[i] = lastStateTop[i];
			strip[1].leds[i] = lastStateBottom[i];



		}

		controllers[0]->showLeds();
		controllers[1]->showLeds();

	}


}

void setColorRgb(int rgb) {

	rgbInt = rgb;

	const byte r = static_cast<byte>(rgb >> 16);
	const byte g = static_cast<byte>(rgb >> 8);
	const byte b = static_cast<byte>(rgb);

	colorHolder = CRGB(r, g, b);
}

void setColorHsv() {
	//this is the algorithm to convert from RGB to HSV

	const double s = sat / 100.0;
	const double v = brightness / 100.0;

	double r = 0;
	double g = 0;
	double b = 0;


	const int i = static_cast<int>(floor(hue / 60.0));
	const double f = hue / 60.0 - i;
	const double pv = v * (1 - s);
	const double qv = v * (1 - s * f);
	const double tv = v * (1 - s * (1 - f));

	switch (i) {
	case 0:
		r = v;
		g = tv;
		b = pv;
		break;
	case 1:
		r = qv;
		g = v;
		b = pv;
		break;
	case 2:
		r = pv;
		g = v;
		b = tv;
		break;
	case 3:
		r = pv;
		g = qv;
		b = v;
		break;
	case 4:
		r = tv;
		g = pv;
		b = v;
		break;
	case 5:
		r = v;
		g = pv;
		b = qv;
		break;
	default:
		break;
	}

	colorHolder = CRGB(constrain((int)255 * r, 0, 255), constrain((int)255 * g, 0, 255),
		constrain((int)255 * b, 0, 255));
}

void led_clear() {

	saveState();

	for (auto i = 0; i < NUM_LEDS; i++) {

		strip[0].leds[i].r = 0;
		strip[0].leds[i].g = 0;
		strip[0].leds[i].b = 0;

		strip[1].leds[i].r = 0;
		strip[1].leds[i].g = 0;
		strip[1].leds[i].b = 0;

	}

	controllers[0]->showLeds();
	controllers[1]->showLeds();

	power = "off";
}

void saveState() {

	for (auto i = 0; i < NUM_LEDS; i++) {

		lastStateTop[i].r = strip[0].leds[i].r;
		lastStateTop[i].g = strip[0].leds[i].g;
		lastStateTop[i].b = strip[0].leds[i].b;

		lastStateBottom[i].r = strip[1].leds[i].r;
		lastStateBottom[i].g = strip[1].leds[i].g;
		lastStateBottom[i].b = strip[1].leds[i].b;


		/*lastStateTop[i] = strip[0].leds[i];
		lastStateBottom[i] = strip[1].leds[i];*/

	}

}

void SetColor() {


	if (chosenStrip >= 2) {

		for (int i = 0; i < NUM_LEDS; i++) {
			strip[0].leds[i].r = colorHolder.r;
			strip[0].leds[i].g = colorHolder.g;
			strip[0].leds[i].b = colorHolder.b;

			strip[1].leds[i].r = colorHolder.r;
			strip[1].leds[i].g = colorHolder.g;
			strip[1].leds[i].b = colorHolder.b;
		}
		controllers[0]->showLeds();
		controllers[1]->showLeds();
	} else {
		for (int i = 0; i < NUM_LEDS; i++) {
			strip[chosenStrip].leds[i].r = colorHolder.r;
			strip[chosenStrip].leds[i].g = colorHolder.g;
			strip[chosenStrip].leds[i].b = colorHolder.b;

		}
		controllers[chosenStrip]->showLeds();
	}

	power = "on";

}

CRGB getRGBfromTemperature() {

	double tmpCalc;
	long r, g, b;
	CRGB color;

	// Temperature must fall between 1000 and 40000 degrees
	if (colorTemp < 1000)
		colorTemp = 1000;
	if (colorTemp > 40000)
		colorTemp = 40000;

	// All calculations require colorTemp \ 100, so only do the conversion once
	colorTemp = colorTemp / 100;

	// Calculate each color in turn

	// First: red
	if (colorTemp <= 66)
		r = 255;
	else
	{
		// Note: the R-squared value for this approximation is .988
		tmpCalc = colorTemp - 60;
		tmpCalc = 329.698727446 * (pow(tmpCalc, -0.1332047592));
		r = tmpCalc;
		if (r < 0)
			r = 0;
		if (r > 255)
			r = 255;
	}

	// Second: green
	if (colorTemp <= 66)
	{
		// Note: the R-squared value for this approximation is .996
		tmpCalc = colorTemp;
		tmpCalc = 99.4708025861 * log(tmpCalc) - 161.1195681661;
		g = tmpCalc;
		if (g < 0)
			g = 0;
		if (g > 255)
			g = 255;
	} else
	{
		// Note: the R-squared value for this approximation is .987
		tmpCalc = colorTemp - 60;
		tmpCalc = 288.1221695283 * (pow(tmpCalc, -0.0755148492));
		g = tmpCalc;
		if (g < 0)
			g = 0;
		if (g > 255)
			g = 255;
	}

	// Third: blue
	if (colorTemp >= 66)
		b = 255;
	else if (colorTemp <= 19)
		b = 0;
	else
	{
		// Note: the R-squared value for this approximation is .998
		tmpCalc = colorTemp - 10;
		tmpCalc = 138.5177312231 * log(tmpCalc) - 305.0447927307;

		b = tmpCalc;
		if (b < 0)
			b = 0;
		if (b > 255)
			b = 255;
	}

	return color.setRGB(r, g, b);
}

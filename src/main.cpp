#include <Arduino.h>

#include <WiFiManager.h>
#include <IRCClient.h>
#include <HTTPClient.h>
#include <pngle.h>
#include <Preferences.h>

Preferences preferences;

#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

const char* root_ca= \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n" \
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n" \
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n" \
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n" \
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n" \
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n" \
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n" \
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n" \
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n" \
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n" \
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n" \
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n" \
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n" \
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n" \
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n" \
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n" \
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n" \
"rqXRfboQnoZsG4q5WTP468SQvvG5\n" \
"-----END CERTIFICATE-----\n";


typedef struct s_message_twitch_data {
	String badge_info;
	String badges;
	String client_nonce;
	String color;
	String display_name;
	String emote_only;
	String emotes;
	String flags;
	String id;
	String mod;
	String room_id;
	String subscriber;
	String tmi_sent_ts;
	String turbo;
	String user_id;
} t_message_twitch_data;


#define IRC_SERVER   "irc.chat.twitch.tv"
#define IRC_PORT     6667

String twitchChannelName;
String twitchBotName;
String twitchToken;
int led = 2;
String ircChannel = "";

WiFiClient wiFiClient;
IRCClient client(IRC_SERVER, IRC_PORT, wiFiClient);
HTTPClient http;
WiFiManager	wifiManager;

WiFiManagerParameter param_channel_name("ChannelName", "Channel Name", "", 50);
WiFiManagerParameter param_bot_name(    "BotName",     "Bot Name",     "", 50);
WiFiManagerParameter param_token(       "Token",       "Token",        "", 50);

bool shouldSaveConfig = false;

void saveConfigCallback() {
	Serial.println("Should save config");
	shouldSaveConfig = true;
}

void parseTwitchData(String data, t_message_twitch_data *ret) {
	ret->badge_info = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->badges = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->client_nonce = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->color = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->display_name = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->emote_only = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->emotes = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->flags = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->id = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->mod = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->room_id = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->subscriber = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->tmi_sent_ts = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->turbo = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);

	ret->user_id = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
	data = data.substring(data.indexOf(";") + 1);
}

void pngle_on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
	rgba[0] = rgba[0] * (rgba[3] / 255.0);
	rgba[1] = rgba[1] * (rgba[3] / 255.0);
	rgba[2] = rgba[2] * (rgba[3] / 255.0);
	uint16_t color = (rgba[0] << 8 & 0xf800) | (rgba[1] << 3 & 0x07e0) | (rgba[2] >> 3 & 0x001f);
	if (rgba[3])
		tft.fillRect(x*2, y*2, w*2, h*2, color);
}

void callback(IRCMessage ircMessage) {
	if (ircMessage.command == "PRIVMSG" && ircMessage.text[0] != '\001') {
		digitalWrite(led, HIGH);
		t_message_twitch_data data;
		parseTwitchData(ircMessage.twitch_data, &data);
		Serial.printf("emotes: %s\n", data.emotes.c_str());

		char buff[200];
		String str = data.emotes.substring(0, data.emotes.indexOf(":")); // get the ID of the first emote
		sprintf(buff, "https://static-cdn.jtvnw.net/emoticons/v2/%s/static/light/3.0", str.c_str());
		Serial.printf("url = '%s'\n", buff);
		
		http.begin(buff, root_ca);
		int httpCode = http.GET();

		int len = http.getSize();

		WiFiClient *stream = http.getStreamPtr();

		pngle_t *pngle = pngle_new();
		// tft.clear();
		tft.fillScreen(TFT_BLACK);
		pngle_set_draw_callback(pngle, pngle_on_draw);

		uint8_t buf[1024];
		int remain = 0;
		
		 while (http.connected() && (len > 0 || len == -1)) {
			size_t size = stream->available();
			if (!size) {
				delay(1);
				continue;
			}

			if (size > sizeof(buf) - remain)
				size = sizeof(buf) - remain;

			int c = stream->readBytes(buf + remain, size);
			if (c > 0) {
				int fed = pngle_feed(pngle, buf, remain + c);
				if (fed < 0) {
					tft.fillScreen(TFT_BLACK);
					tft.printf("ERROR: %s\n", pngle_error(pngle));
					break;
				} else {
					len -= c;
				}
				remain = remain + c - fed;
				if (remain > 0)
					memmove(buf, buf + fed, remain);
			} else
				delay(1);
		}

		pngle_destroy(pngle);
		http.end();

		String message("<" + ircMessage.nick + "> " + ircMessage.text);
		Serial.println(message);
		digitalWrite(led, LOW);
	}
}


void setup() {
	Serial.begin(115200);
	preferences.begin("emotes", false);

	pinMode(led, OUTPUT);

	wifiManager.setDebugOutput(false);
	wifiManager.setTimeout(180);
	wifiManager.setConfigPortalTimeout(180); // try for 3 minute
	wifiManager.setMinimumSignalQuality(15);
	wifiManager.setRemoveDuplicateAPs(true);
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	wifiManager.addParameter(&param_channel_name);
	wifiManager.addParameter(&param_bot_name);
	wifiManager.addParameter(&param_token);
	
	wifiManager.setClass("invert"); // dark theme

	std::vector<const char*> menu = { "param", "wifi", "info", "sep", "restart" };
	wifiManager.setMenu(menu);

	// wifiManager.setParamsPage(true);
	wifiManager.setCountry("US");
	// wifiManager.setHostname(hostname);

	bool rest = wifiManager.autoConnect("ESP32");

	if (rest)
		Serial.println("Wifi connected");
	else
		ESP.restart();

	if (shouldSaveConfig) {
		Serial.println("saving config");
		preferences.putString("ChannelName", param_channel_name.getValue());
		preferences.putString("BotName",     param_bot_name.getValue());
		preferences.putString("Token",       param_token.getValue());
	}

	twitchChannelName = preferences.getString("ChannelName");
	twitchBotName     = preferences.getString("BotName");
	twitchToken       = preferences.getString("Token");
	
	IPAddress ip = WiFi.localIP();
	Serial.println(ip);

	ircChannel = "#" + twitchChannelName;
	client.setCallback(callback);

	tft.begin();
	tft.setRotation(3);
}


void loop() {
	if (!client.connected()) {
		Serial.println("Attempting to connect to " + ircChannel);
		if (client.connect(twitchBotName, "", twitchToken)) {
			client.sendRaw("JOIN " + ircChannel);
			client.sendRaw("CAP REQ :twitch.tv/tags twitch.tv/commands");
			Serial.println("connected and ready to rock");
		} else {
			Serial.println("failed... try again in 5 seconds");
			delay(5000);
		}
		return;
	}
	client.loop();
	wifiManager.process();
}

void sendTwitchMessage(String message) {
	client.sendMessage(ircChannel, message);
}
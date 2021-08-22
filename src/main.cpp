#include <Arduino.h>
#include <WiFiManager.h>
#include <IRCClient.h>
#include <HTTPClient.h>
#include <pngle.h>
#include <Preferences.h>

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)

Preferences preferences;

#ifdef USE_LCD
	#include <SPI.h>
	#include <TFT_eSPI.h>

	TFT_eSPI tft = TFT_eSPI();
	const int led = 2;
	const int backlight = 14;
#endif

#ifdef USE_HUB75
	#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
	MatrixPanel_I2S_DMA *display = nullptr;
#endif

#ifdef USE_GIF
	#include <AnimatedGIF.h>
	AnimatedGIF gif;
#endif
uint8_t gif_playing = 0;

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

typedef struct s_param {
	String key;
	String val;
} t_param;

#define IRC_SERVER "irc.chat.twitch.tv"
#define IRC_PORT   6667

#define CDN_URL_GIF "https://static-cdn.jtvnw.net/emoticons/v2/%s/default/light/%s"
#define CDN_URL_PNG "https://static-cdn.jtvnw.net/emoticons/v2/%s/static/light/%s"

#ifdef  USE_GIF
	#define CDN_URL_DEFAULT CDN_URL_GIF
#else
	#define CDN_URL_DEFAULT CDN_URL_PNG
#endif


String twitchChannelName;
String twitchBotName;
String twitchToken;
String ircChannel = "";

WiFiClient wiFiClient;
IRCClient client(IRC_SERVER, IRC_PORT, wiFiClient);
HTTPClient http;
WiFiManager	wifiManager;

WiFiManagerParameter param_channel_name("ChannelName", "Channel Name", "", 50);
WiFiManagerParameter param_bot_name(    "BotName",     "Bot Name",     "", 50);
WiFiManagerParameter param_token(       "Token",       "Token",        "", 50);

uint32_t off_x;
uint32_t off_y;

#ifdef USE_GIF

uint8_t *gif_ptr;


// Draw a line of image directly on the LCD
void GIFDraw(GIFDRAW *pDraw) {
	uint8_t *s;
	uint16_t *d, *usPalette, usTemp[320];
	int x, y;

	usPalette = pDraw->pPalette;
	y = pDraw->iY + pDraw->y; // current line
	
	s = pDraw->pPixels;
	if (pDraw->ucDisposalMethod == 2) {// restore to background color 
		for (x=0; x<MATRIX_W; x++) {
			if (s[x] == pDraw->ucTransparent)
			s[x] = pDraw->ucBackground;
		}
		pDraw->ucHasTransparency = 0;
	}
	// Apply the new pixels to the main image
	// if (pDraw->ucHasTransparency) { // if transparency used
	// 	uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
	// 	int x, iCount;
	// 	pEnd = s + pDraw->iWidth;
	// 	x = 0;
	// 	iCount = 0; // count non-transparent pixels
	// 	while (x < pDraw->iWidth) {
	// 		c = ucTransparent-1;
	// 		d = usTemp;
	// 		while (c != ucTransparent && s < pEnd) {
	// 			c = *s++;
	// 			if (c == ucTransparent) // done, stop
	// 			{
	// 				s--; // back up to treat it like transparent
	// 			}
	// 			else // opaque
	// 			{
	// 				*d++ = usPalette[c];
	// 				iCount++;
	// 			}
	// 		} // while looking for opaque pixels
	// 		if (iCount) { // any opaque pixels?
	// 			for(int xOffset = 0; xOffset < iCount; xOffset++ ){
	// 				#ifdef USE_LCD
	// 					tft.drawPixel(x + xOffset, y, usTemp[xOffset]);
	// 				#endif
	// 				#ifdef USE_HUB75
	// 					display->drawPixel(off_x+x + xOffset, off_y+y, usTemp[xOffset]);
	// 				#endif
	// 			}
	// 			x += iCount;
	// 			iCount = 0;
	// 		}
	// 		// no, look for a run of transparent pixels
	// 		c = ucTransparent;
	// 		while (c == ucTransparent && s < pEnd) {
	// 			c = *s++;
	// 			if (c == ucTransparent)
	// 				iCount++;
	// 			else
	// 				s--; 
	// 		}
	// 		if (iCount) {
	// 			x += iCount; // skip these
	// 			iCount = 0;
	// 		}
	// 	}
	// } else {
		s = pDraw->pPixels;
		// Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
		for (x=0; x<pDraw->iWidth; x++) {
			#ifdef USE_LCD
				// tft.drawPixel(x, y, usPalette[*s++]);
				tft.fillRect(off_x+(x*SCALE), off_y+(y*SCALE), SCALE, SCALE, usPalette[*s++]);
			#endif
			#ifdef USE_HUB75
				display->drawPixel(off_x+x, off_y+y, usPalette[*s++]);
			#endif
		}
	// }
}

#endif

void parseTwitchData(String data, t_param *ret) {
	uint32_t i=0;
	while (1) {
		ret[i].key = data.substring(0, data.indexOf("="));
		ret[i].val = data.substring(data.indexOf("=") + 1, data.indexOf(";"));
		// Serial.printf("%s:\t%s\n", ret[i].key.c_str(), ret[i].val.c_str());
		i++;

		data = data.substring(data.indexOf(";") + 1);
		if (data == "user-type")
			break;
	}
}

String get_param(String key, t_param *param, size_t size) {
	for (int i=0; i<size; i++) {
		if (param[i].key == key) {
			return param[i].val;
		}
	}
	return String();
}

void pngle_on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
	rgba[0] = rgba[0] * (rgba[3] / 255.0);
	rgba[1] = rgba[1] * (rgba[3] / 255.0);
	rgba[2] = rgba[2] * (rgba[3] / 255.0);

	uint16_t color = (rgba[0] << 8 & 0xf800) | (rgba[1] << 3 & 0x07e0) | (rgba[2] >> 3 & 0x001f);
	if (rgba[3]) {
		#ifdef USE_LCD
			tft.fillRect(off_x+(x*SCALE), off_y+(y*SCALE), w*SCALE, h*SCALE, color);
		#endif
		#ifdef USE_HUB75
			display->fillRect(off_x+x, off_y+y, w, h, color);
		#endif
	} else {
		#ifdef USE_LCD
			tft.fillRect(off_x+(x*SCALE), off_y+(y*SCALE), w*SCALE, h*SCALE, 0x0000);
		#endif
		#ifdef USE_HUB75
			display->fillRect(off_x+x, off_y+y, w, h, 0x0000);
		#endif
	}
}

void pngle_on_init(pngle_t *pngle, uint32_t w, uint32_t h) {
	off_x = (MATRIX_W - w*SCALE) / 2;
	off_y = (MATRIX_H - h*SCALE) / 2;
}

int download_http(const char *url, const char* emote) {
	char buff[200];
	sprintf(buff, url, emote, STRINGIZE_VALUE_OF(EMOTE_SIZE));
	Serial.printf("url = '%s'\n", buff);
	
	http.begin(buff, root_ca);
	return http.GET();
}

void download_png(size_t len, WiFiClient *stream) {
	gif_playing = 0;
	pngle_t *pngle = pngle_new();

	#ifdef USE_LCD
		tft.fillScreen(TFT_BLACK);
	#endif
	#ifdef USE_HUB75
		display->clearScreen();
	#endif

	pngle_set_init_callback(pngle, pngle_on_init);
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
				// tft.fillScreen(TFT_BLACK);
				// tft.printf("ERROR: %s\n", pngle_error(pngle));
				break;
			} else
				len -= c;

			remain = remain + c - fed;
			if (remain > 0)
				memmove(buf, buf + fed, remain);
		} else
			delay(1);
	}

	pngle_destroy(pngle);
	#ifdef USE_HUB75
		display->flipDMABuffer();
	#endif
	#ifdef USE_LCD
		digitalWrite(led, LOW);
	#endif
}

void irc_callback(IRCMessage ircMessage) {
	// Serial.printf("cmd = %s\n", ircMessage.command.c_str());
	if (ircMessage.command == "PRIVMSG" && ircMessage.text[0] != '\001') {
		
		Serial.printf("<%s> %s\n", ircMessage.nick.c_str(), ircMessage.text.c_str());

		#ifdef USE_LCD
			digitalWrite(led, HIGH);
		#endif

		t_param data[20];
		// Serial.printf("data: %s\n", ircMessage.twitch_data.c_str());

		parseTwitchData(ircMessage.twitch_data, data);
		String emotes = get_param("emotes", data, 20);

		if (emotes != "") {
			Serial.printf("emotes: %s\n", emotes.c_str());

			String str = emotes.substring(0, emotes.indexOf(":")); // get the ID of the first emote
			
			int code = download_http(CDN_URL_DEFAULT, str.c_str());

			if (code == 200 || code == 304) {

				int len = http.getSize();
				Serial.printf("len = '%d'\n", len);

				WiFiClient *stream = http.getStreamPtr();

				int c = stream->peek();

				if (c == 137) {  // PNG
					download_png(len, stream);
				}
				#ifdef USE_GIF
				else if (c == 'G') { // GIF 
					Serial.printf("GIF\n");
					gif.close();
					if (gif_ptr)
						free(gif_ptr);
					#ifdef BOARD_HAS_PSRAM
						gif_ptr = (uint8_t*)ps_malloc(len);
					#else
						gif_ptr = (uint8_t*)malloc(len);
					#endif
					if (gif_ptr) {
						stream->readBytes(gif_ptr, len);
						if (gif.open(gif_ptr, len, GIFDraw)) {
							Serial.printf("Gif open\n");
							gif_playing = 1;
							off_x = (MATRIX_W - gif.getCanvasWidth() * SCALE) / 2;
							off_y = (MATRIX_H - gif.getCanvasHeight() * SCALE) / 2;
							#ifdef USE_LCD
								tft.fillScreen(TFT_BLACK);
							#endif
						}
					} else {
						Serial.printf("Can't allocate space for GIF\n");
						http.end();
						int code = download_http(CDN_URL_PNG, str.c_str());

						if (code == 200 || code == 304) {
							int len = http.getSize();
							Serial.printf("len = '%d'\n", len);

							WiFiClient *stream = http.getStreamPtr();
							download_png(len, stream);
						}
					}
				}
				#endif
				http.end();
				#ifdef USE_LCD
					digitalWrite(led, LOW);
				#endif
			}
		}
	}
}

void start_irc() {
	twitchChannelName = preferences.getString("ChannelName");
	twitchBotName     = preferences.getString("BotName");
	twitchToken       = preferences.getString("Token");

	ircChannel = "#" + twitchChannelName;
	wiFiClient.flush();
	wiFiClient.stop();
	client.setCallback(irc_callback);
}

void setSaveParamsCallback() {
	Serial.println("saving config");
	preferences.putString("ChannelName", param_channel_name.getValue());
	preferences.putString("BotName",     param_bot_name.getValue());
	preferences.putString("Token",       param_token.getValue());
	start_irc();
}

void setup() {
	Serial.begin(115200);
	preferences.begin("emotes", false);

	#ifdef USE_LCD
		pinMode(led, OUTPUT);
		pinMode(backlight, OUTPUT);
		digitalWrite(backlight, 1);
	#endif

	Serial.printf("Start Wifi manager\n");
	wifiManager.setDebugOutput(false);
	wifiManager.setTimeout(180);
	wifiManager.setConfigPortalTimeout(180); // try for 3 minute
	wifiManager.setMinimumSignalQuality(15);
	wifiManager.setRemoveDuplicateAPs(true);
	wifiManager.setSaveParamsCallback(setSaveParamsCallback);

	wifiManager.addParameter(&param_channel_name);
	wifiManager.addParameter(&param_bot_name);
	wifiManager.addParameter(&param_token);
	
	wifiManager.setClass("invert"); // dark theme

	std::vector<const char*> menu = { "param", "wifi", "info", "sep", "restart" };
	wifiManager.setMenu(menu);

	// wifiManager.setParamsPage(true);
	wifiManager.setCountry("US");
	// wifiManager.setHostname(hostname);

	bool rest = wifiManager.autoConnect("Twitch_Emote");

	if (rest) {
		Serial.println("Wifi connected");
		wifiManager.startWebPortal();
	}
	else
		ESP.restart();
	Serial.print("IP: \n");
	Serial.println(WiFi.localIP());

	start_irc();

	#ifdef USE_LCD
		tft.begin();
		tft.setRotation(3);
		// tft.setSwapBytes(false);
		tft.initDMA();
	#endif
	#ifdef USE_HUB75
		HUB75_I2S_CFG::i2s_pins _pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
		
		HUB75_I2S_CFG mxconfig(
			MATRIX_W,     // Module width
			MATRIX_H,     // Module height
			MATRIX_CHAIN, // chain length
			_pins         // pin mapping
		);

		mxconfig.double_buff    = false;                    // use DMA double buffer (twice as much RAM required)
		mxconfig.driver         = HUB75_I2S_CFG::SHIFTREG; // Matrix driver chip type - default is a plain shift register
		mxconfig.i2sspeed       = HUB75_I2S_CFG::HZ_10M;   // I2S clock speed
		mxconfig.clkphase       = true;                    // I2S clock phase
		mxconfig.latch_blanking = MATRIX_LATCH_BLANK;      // How many clock cycles to blank OE before/after LAT signal change, default is 1 clock

		display = new MatrixPanel_I2S_DMA(mxconfig);

		display->begin();  // setup display with pins as pre-defined in the library
		display->setBrightness8(MATRIX_BRIGHNESS); //0-255
	#endif

	Serial.println("...Starting Display");

	#ifdef USE_GIF
		gif.begin(LITTLE_ENDIAN_PIXELS);
	#endif
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
	#ifdef USE_GIF
		if (gif_playing)
			gif.playFrame(true, NULL);
	#endif
	client.loop();
	wifiManager.process();
}

void sendTwitchMessage(String message) {
	client.sendMessage(ircChannel, message);
}
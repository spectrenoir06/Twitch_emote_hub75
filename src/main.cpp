#include <Arduino.h>
#include <WiFiManager.h>
#include <IRCClient.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <AnimatedGIF.h>
#include <PNGdec.h>

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)

#ifndef OFF_X
	#define OFF_X 0
#endif

#ifndef OFF_Y
	#define OFF_Y 0
#endif

#define IRC_SERVER "irc.chat.twitch.tv"
#define IRC_PORT   6667

#define CDN_URL_GIF "https://static-cdn.jtvnw.net/emoticons/v2/%s/default/dark/%s"
#define CDN_URL_PNG "https://static-cdn.jtvnw.net/emoticons/v2/%s/static/dark/%s"

#define CDN_URL_DEFAULT CDN_URL_GIF


Preferences preferences;

#ifdef USE_M5
	#include <M5Stack.h>
	#define tft M5.Lcd
	// #define drawPixel(a, b, c) tft.setAddrWindow(a, b, a, b); tft.pushColor(c)
	#define USE_LCD
#endif

#ifdef USE_LCD
	#ifndef USE_M5
		#include <SPI.h>
		#include <TFT_eSPI.h>

		TFT_eSPI tft = TFT_eSPI();
	#endif
	#ifdef USE_LED
		const int led = 2;
	#endif
#endif

#ifdef USE_HUB75
	#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
	MatrixPanel_I2S_DMA *display = nullptr;
#endif

typedef struct s_param {
	String key;
	String val;
} t_param;


typedef struct {
	uint8_t type;
	uint8_t mode;
	uint8_t* data;
	size_t len;
	int off_x;
	int off_y;
	uint32_t end_time;
	union {
		AnimatedGIF* gif;
		PNG* png;
	};
} t_img;


typedef struct {
	PNG png;
	AnimatedGIF gif;
	// std::queue<t_img> queue;
	t_img queue[EMOTE_BUFFER_SIZE];
	uint cursor = 0;
	size_t size = 0;
} t_img_data;


enum img_mode { DOWNLOAD = 1, PLAY, WAIT, DELETE};

WiFiManagerParameter param_channel_name("ChannelName", "Channel Name",     "?", 50);
WiFiManagerParameter param_bot_name(    "BotName",     "Bot Name",         "?", 50);
WiFiManagerParameter param_token(       "Token",       "Token: (oauth:...)", "?", 50);

String twitchChannelName;
String twitchBotName;
String twitchToken;
String ircChannel = "";

WiFiClient wiFiClient;
IRCClient client(IRC_SERVER, IRC_PORT, wiFiClient);
HTTPClient http;
WiFiManager	wifiManager;

uint8_t *gif_ptr;
uint8_t use_irc = 1;

t_img_data *imgs_data;
t_img http_img;

extern const char root_ca_start[] asm("_binary_src_aws_root_ca_pem_start");
extern const char root_ca_end[]   asm("_binary_src_aws_root_ca_pem_end");

void PNGDraw(PNGDRAW* pDraw) {
	uint16_t usPixels[320];
	uint8_t ucMask[40];
	int y = pDraw->y;
	t_img* img = (t_img*)pDraw->pUser;

	imgs_data->png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0x000000);
	for (int x = 0; x < pDraw->iWidth; x++) {
		#if SCALE == 1
			#ifdef USE_HUB75
				display->drawPixel(img->off_x + x, img->off_y + y, usPixels[x]);
			#endif
			#ifdef USE_LCD
				tft.drawPixel(img->off_x + x, img->off_y + y, usPixels[x]);
				// drawPixel(img->off_x + x, img->off_y + y, usPixels[x]);
			#endif
		#else
			#ifdef USE_HUB75
				display->fillRect((img->off_x + (x * SCALE), (img->off_y + (y * SCALE), w * SCALE, h * SCALE, usPixels[x]);
			#endif
			#ifdef USE_LCD
				tft.fillRect(img->off_x+(x*SCALE), img->off_y+(y*SCALE), SCALE, SCALE, usPixels[x]);
			#endif
		#endif
	}
}

// Draw a line of image directly on the LCD
void GIFDraw(GIFDRAW *pDraw) {
	uint8_t *s;
	uint16_t *d, *usPalette, usTemp[320];
	int x, y;
	t_img *img = (t_img*)pDraw->pUser;

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
	int ofx = pDraw->iX;

	for (x=ofx; x<(pDraw->iWidth+ofx); x++) {
		#ifdef USE_LCD
			tft.fillRect(img->off_x+(x*SCALE), img->off_y+(y*SCALE), SCALE, SCALE, usPalette[*s++]);
		#endif
		#ifdef USE_HUB75
			display->drawPixel(img->off_x+x, img->off_y+y, usPalette[*s++]);
		#endif
	}
}

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

int download_http(const char *url, const char* emote) {
	char buff[200];
	sprintf(buff, url, emote, STRINGIZE_VALUE_OF(EMOTE_SIZE));
	Serial.printf("url = '%s'\n", buff);
	
	http.begin(buff, root_ca_start);
	return http.GET();
}

void download_http_data(uint8_t c, size_t len, WiFiClient *stream, String str) {
	if (c == 'G' || c == 137) { // GIF || PNG
		if (imgs_data->size < EMOTE_BUFFER_SIZE) {
			Serial.printf("insert: cursor = %d, size = %d, pos =%d\n", imgs_data->cursor, imgs_data->size, (imgs_data->cursor + imgs_data->size) % EMOTE_BUFFER_SIZE);
			t_img* img = &imgs_data->queue[(imgs_data->cursor + imgs_data->size) % EMOTE_BUFFER_SIZE];
			#ifdef BOARD_HAS_PSRAM
				img->data = (uint8_t*)ps_malloc(len);
			#else
				img->data = (uint8_t*)malloc(len);
			#endif
			if (img->data) {
				stream->readBytes(img->data, len);
				img->type = c;
				img->len = len;
				img->mode = WAIT;
				imgs_data->size++;
			}
			else {
				Serial.printf("Can't allocate space for GIF\n");
				http.end();
				int code = download_http(CDN_URL_PNG, str.c_str());

				if (code == 200 || code == 304) {
					int len = http.getSize();
					Serial.printf("len = '%d'\n", len);

					WiFiClient *stream = http.getStreamPtr();
					download_http_data(stream->peek(), len, stream, str);
				}
			}
		}
	}
}

void irc_callback(IRCMessage ircMessage) {
	// Serial.printf("cmd = %s\n", ircMessage.command.c_str());
	if (ircMessage.command == "PRIVMSG" && ircMessage.text[0] != '\001') {
		
		Serial.printf("<%s> %s\n", ircMessage.nick.c_str(), ircMessage.text.c_str());

		#ifdef USE_LED
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
				download_http_data(stream->peek(), len, stream, str);
				http.end();
				#ifdef USE_LED
					digitalWrite(led, LOW);
				#endif
			}
		}
	}
	else if (ircMessage.command == "NOTICE") {
		Serial.println(ircMessage.text);
		//Improperly formatted auth
		//Login authentication failed
		use_irc = 0;
		Serial.println("Stop IRC client");
	}
	else if (ircMessage.command == "JOIN") {
		Serial.println("Twitch auth OK");
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
	Serial.println("Saving config");
	preferences.putString("ChannelName", param_channel_name.getValue());
	preferences.putString("BotName",     param_bot_name.getValue());
	preferences.putString("Token",       param_token.getValue());
	start_irc();
}

void File_Upload() {
	String webpage = "";
	webpage += FPSTR(HTTP_STYLE);
	webpage += F("<body class='invert'><div class='wrap'>");
	webpage += F("<h3>Select File to upload an image to display</h3>"); 
	webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data'>");
	webpage += F("<input class='buttons' type='file' name='fupload' id = 'fupload' value=''><br>");
	webpage += F("<br><button class='buttons' type='submit'>Upload File</button><br>");
	webpage += F("</div'></body'>");
	wifiManager.server->send(200, "text/html", webpage);
}

void handleFileUpload(){ // upload a new file to the Filing system
	String webpage = "";
	HTTPUpload& uploadfile = wifiManager.server->upload();
	if(uploadfile.status == UPLOAD_FILE_START) {
		Serial.print("Upload File Name: "); Serial.println(uploadfile.filename);
		Serial.print("type: "); Serial.println(uploadfile.type);

		#ifdef USE_LED
			digitalWrite(led, HIGH);
		#endif

		if (uploadfile.type == "image/gif" || uploadfile.type == "image/png") {
			if (imgs_data->size < EMOTE_BUFFER_SIZE) {
				#ifdef BOARD_HAS_PSRAM
					http_img.data = (uint8_t*)ps_malloc(1);
				#else
					http_img.data = (uint8_t*)malloc(1);
				#endif
				http_img.len = 0;
				http_img.type = (uploadfile.type == "image/gif") ? 'G' : 127;
				http_img.mode = DOWNLOAD;
			}
		}
		else {
			webpage = "";
			webpage += FPSTR(HTTP_STYLE);
			webpage += F("<body class='invert'><div class='wrap'>");
			webpage += F("<h3>File uploaded Error</h3>");
			webpage += F("<a href='/upload'>[Back]</a><br><br>");
			webpage += F("</div'></body'>");
			wifiManager.server->send(400,"text/html", webpage);
		}
	} else if (uploadfile.status == UPLOAD_FILE_WRITE) {
		// Serial.print("Write: "); Serial.println(uploadfile.currentSize);
		if (uploadfile.type == "image/gif" || uploadfile.type == "image/png") {
			#ifdef BOARD_HAS_PSRAM
				uint8_t* ptr_tmp = (uint8_t*)ps_realloc(http_img.data, http_img.len + uploadfile.currentSize);
			#else
				uint8_t* ptr_tmp = (uint8_t*)realloc(http_img.data, http_img.len + uploadfile.currentSize);
			#endif

			if (!ptr_tmp) {
				free(gif_ptr);
				gif_ptr = 0;
				Serial.printf("Can't reallocate space for GIF\n");
				webpage = "";
				webpage += FPSTR(HTTP_STYLE);
				webpage += F("<body class='invert'><div class='wrap'>");
				webpage += F("<h3>File uploaded Error not enought RAM</h3>");
				webpage += F("<a href='/upload'>[Back]</a><br><br>");
				webpage += F("</div'></body'>");
				wifiManager.server->send(400,"text/html", webpage);
				return;
			} else {
				http_img.data = ptr_tmp;
				memcpy(http_img.data + http_img.len, uploadfile.buf, uploadfile.currentSize);
				http_img.len += uploadfile.currentSize;
			}
		}
	}
	else if (uploadfile.status == UPLOAD_FILE_END) {
		// Serial.print("END: "); Serial.println(http_img.len);
		if (uploadfile.type == "image/gif" || uploadfile.type == "image/png") {
			if (imgs_data->size < EMOTE_BUFFER_SIZE) {
				Serial.printf("insert: cursor = %d, size = %d, pos =%d\n", imgs_data->cursor, imgs_data->size, (imgs_data->cursor + imgs_data->size) % EMOTE_BUFFER_SIZE);
				t_img* img = &imgs_data->queue[(imgs_data->cursor + imgs_data->size) % EMOTE_BUFFER_SIZE];
				http_img.mode = WAIT;
				memcpy(img, &http_img, sizeof(t_img));
				imgs_data->size++;
			}
			webpage = "";
			webpage += FPSTR(HTTP_STYLE);
			webpage += F("<body class='invert'><div class='wrap'>");
			webpage += F("<h3>File was successfully uploaded</h3>"); 
			webpage += F("<h2>Uploaded File Name: "); webpage += uploadfile.filename+"</h2>";
			webpage += F("<a href='/upload'>[Back]</a><br><br>");
			webpage += F("</div'></body'>");
			wifiManager.server->send(200,"text/html", webpage);
		}
	}
}

#ifdef USE_HUB75
	void flip_matrix() {
		display->flipDMABuffer();
	}
#endif


void setup() {
	#ifdef USE_M5
		M5.begin();
		M5.Power.begin();
	#else
		Serial.begin(115200);
	#endif
	#ifdef BOARD_HAS_PSRAM
		Serial.printf("esp32 use external RAM\n");
	#else
		Serial.printf("esp32 use internal RAM\n");
	#endif
	preferences.begin("emotes", false);

	#ifdef USE_LCD
		#ifdef USE_LED
			pinMode(led, OUTPUT);
		#endif
		pinMode(TFT_BL, OUTPUT);
		digitalWrite(TFT_BL, 1);
	#endif

	Serial.printf("Start Wifi manager\n");
	wifiManager.setDebugOutput(false);
	wifiManager.setTimeout(180);
	wifiManager.setConfigPortalTimeout(180); // try for 3 minute
	wifiManager.setMinimumSignalQuality(15);
	wifiManager.setRemoveDuplicateAPs(true);
	wifiManager.setSaveParamsCallback(setSaveParamsCallback);

	param_channel_name.setValue(preferences.getString("ChannelName", "?").c_str(), 50);
	param_bot_name.setValue(    preferences.getString("BotName", "?").c_str(),     50);
	param_token.setValue(       preferences.getString("Token", "?").c_str(),       50);

	wifiManager.addParameter(&param_channel_name);
	wifiManager.addParameter(&param_bot_name);
	wifiManager.addParameter(&param_token);

	wifiManager.setClass("invert"); // dark theme

	std::vector<const char*> menu = {"wifi", "info", "sep", "update", "restart" };
	wifiManager.setMenu(menu);

	// wifiManager.setParamsPage(true);
	wifiManager.setCountry("JP");
	wifiManager.setHostname("matrix-emote");

	bool rest = wifiManager.autoConnect("Twitch_Emote");

	if (rest) {
		Serial.println("Wifi connected");
		wifiManager.startWebPortal();

		wifiManager.server->on("/upload",   File_Upload);
		wifiManager.server->on("/fupload",  HTTP_POST,[](){ wifiManager.server->send(200);}, handleFileUpload);
	}
	else
		ESP.restart();
	Serial.print("IP: "); Serial.println(WiFi.localIP());

	start_irc();

	#ifdef USE_LCD
		#ifndef USE_M5
			tft.begin();
			tft.setRotation(LCD_ROTATION);
			// tft.setSwapBytes(false);
			tft.initDMA();
		#endif
		tft.fillScreen(TFT_BLACK);
	#endif
	#ifdef USE_HUB75
		HUB75_I2S_CFG::i2s_pins _pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
		
		HUB75_I2S_CFG mxconfig(
			MATRIX_W,     // Module width
			MATRIX_H,     // Module height
			MATRIX_CHAIN, // chain length
			_pins         // pin mapping
		);

		mxconfig.double_buff = true;                   // use DMA double buffer (twice as much RAM required)
		// #ifndef DMA_DOUBLE_BUFF
		// #endif
		mxconfig.driver          = HUB75_I2S_CFG::SHIFTREG; // Matrix driver chip type - default is a plain shift register
		mxconfig.i2sspeed        = HUB75_I2S_CFG::HZ_10M;   // I2S clock speed
		mxconfig.clkphase        = true;                    // I2S clock phase
		mxconfig.latch_blanking  = MATRIX_LATCH_BLANK;      // How many clock cycles to blank OE before/after LAT signal change, default is 1 clock

		display = new MatrixPanel_I2S_DMA(mxconfig);

		display->begin();  // setup display with pins as pre-defined in the library
		display->setBrightness8(MATRIX_BRIGHNESS); //0-255
		// display->flipDMABuffer();
	#endif

	#ifdef BOARD_HAS_PSRAM
		imgs_data = (t_img_data*)ps_malloc(sizeof(t_img_data));
	#else
		imgs_data = (t_img_data*)malloc(sizeof(t_img_data));
	#endif
	imgs_data->size = 0;
	imgs_data->cursor = 0;
}

unsigned long next_frame = 0;

void loop() {
	if (!client.connected() && use_irc) {
		Serial.println("Attempting to connect to " + ircChannel);
		if (client.connect(twitchBotName, "", twitchToken)) {
			client.sendRaw("JOIN " + ircChannel);
			client.sendRaw("CAP REQ :twitch.tv/tags twitch.tv/commands");
			Serial.println("Connected to twitch IRC");
			Serial.println("Wait for auth...");
		} else {
			Serial.println("Failed... try again in 5 seconds");
			delay(5000);
		}
		return;
	}

	if (imgs_data->size) {
		// Serial.println(imgs_data->size);
		t_img* img = &imgs_data->queue[imgs_data->cursor];

		Serial.println("\n\nbuffer:");
		for (int i = 0; i < imgs_data->size; i++) {
			t_img* img_ptr = &imgs_data->queue[(imgs_data->cursor + i) % EMOTE_BUFFER_SIZE];
			Serial.printf("    [%d]\tmode:%d,\ttype:%d,\tlen:%d,\tend_time:%d\n", i, img_ptr->mode, img_ptr->type, img_ptr->len, img_ptr->end_time);
		}

		if (img->mode == WAIT) {
			Serial.printf("Load new image\n");
			if (img->type == 'G') {
				img->gif = new AnimatedGIF();
				img->gif->begin(LITTLE_ENDIAN_PIXELS);
				img->gif->open(img->data, img->len, GIFDraw);
				img->off_x = ((int)MATRIX_W - img->gif->getCanvasWidth()  * SCALE) / 2 + OFF_X;
				img->off_y = ((int)MATRIX_H - img->gif->getCanvasHeight() * SCALE) / 2 + OFF_Y;
				#ifdef USE_LCD
					tft.fillScreen(TFT_BLACK);
				#endif
			} else {
				img->png = new PNG();
				img->png->openRAM(img->data, img->len, PNGDraw);
				img->off_x = ((int)MATRIX_W - img->png->getWidth()  * SCALE) / 2 + OFF_X;
				img->off_y = ((int)MATRIX_H - img->png->getHeight() * SCALE) / 2 + OFF_Y;
				#ifdef USE_HUB75
					display->clearScreen();
				#endif
				#ifdef USE_LCD
					tft.fillScreen(TFT_BLACK);
				#endif
				img->png->decode(img, 0);
				#ifdef USE_HUB75
					flip_matrix();
				#endif
			}
			img->mode = PLAY;
			img->end_time = millis() + MIN_TIME;
		} else {
			if (img->type == 'G') {
				if (millis() > next_frame) {
					#ifdef USE_HUB75
						display->clearScreen();
					#endif
					int t = millis();
					int i;
					img->gif->playFrame(false, &i, img);
					next_frame = t + i;
					#ifdef USE_HUB75
						flip_matrix();
					#endif
				}
			}
			if (imgs_data->size > 1 && img->end_time < millis()) {
				// img->mode = DELETE;
				Serial.printf("NEXT\n");
				free(img->data);
				if (img->type == 'G')
					delete img->gif;
				else
					delete img->png;

				imgs_data->cursor++;
				imgs_data->cursor %= EMOTE_BUFFER_SIZE;
				imgs_data->size--;
			}
		}
	}

	client.loop();
	wifiManager.process();
	#if USE_M5
		M5.update();
	#endif
}

void sendTwitchMessage(String message) {
	client.sendMessage(ircChannel, message);
}
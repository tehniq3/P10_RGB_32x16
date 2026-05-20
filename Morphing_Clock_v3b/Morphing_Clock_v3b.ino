/*
 Thanks to:
- Dominic Buchstaller for PxMatrix
- Hari Wiguna aka HariFun for Morphing Digits
- Brian Lough aka WitnessMeNow for tutorials on the matrix
- SKElectronics for base sketch 
 v.0 - Nicu FLORICA (niq_ro) prepared the sketch for is test
 v.0a - brightness control
 v.0b - delete tens of hours when is 0 after 1
 v.1 - added AM/PM information, date and name of the day
 v.1a - date in format DD/MM/YEAR not DD/MM/YY and also show name of the day in both languages, fixed seconds points (no flash)
 v.1b - extract time after boot (in setup) for no show the artephacts
 v.1c - added DST switch (A0 to GND -> DST = 0, A0 to 3.3V -> DST = 1) as at https://nicuflorica.blogspot.com/2023/10/ceas-ntp-cu-tranzitii-animate-si-date.html
 v.1c1 - fix the unclear numbers at transition between DST (summer/winter time)
 v.1c2 - restart after changed status of DST switch
 v.1c3 - reconnect to wifi if is lost
 v.1c4 - put 1 for tens not morphing moving
 v.1c5 - replaced 0 AM with 12 AM
 v.2 - changed the digit lenght from 3 to 2 digits in order to show 24-hour format and migrated to 24-hour format
 v.2a - corrected 5 and 0 transation + clean upper display and remove artephacts instead AM/PM in upper side,
 v.2a1- corrected transition from 23 to 00
 v.2b - used AI for made upper text to be as scroll text not static (name od day and date, one in rmanian, onces in english, with changed of colours)
 v.2b1 - updated small letters m (used m as 1/2m and ~ changed as 1/2m) and b
 v.2b2 - updated text of the date
 v.3 - added info from open-meteo site
 v.3a - added Exchange rate: EURO to RON (romanian lei)
 v.3b - decreased black display time
*/

#include <PxMatrix.h>           // https://github.com/2dom/PxMatrix
#include <TimeLib.h>            // include Arduino time library -

#define double_buffer
// ESP8266 WiFi main library
#include <ESP8266WiFi.h>
#include <NTPClient.h>          // include NTPClient library
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>
// Libraries for internet time


#ifdef ESP32

#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 2
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#endif

#ifdef ESP8266

#include <Ticker.h>
Ticker display_ticker;
#define P_LAT 16
#define P_A 5
#define P_B 4
#define P_C 15
#define P_D 12
#define P_E 0
#define P_OE 2

#endif

// Pins for LED MATRIX
//PxMATRIX display(32, 16, P_LAT, P_OE, P_A, P_B, P_C, P_D);
PxMATRIX display(32, 16, P_LAT, P_OE, P_A, P_B, P_C);
//PxMATRIX display(32, 16, P_LAT, P_OE, P_A, P_B);

// set Wi-Fi SSID and password
const char *ssid     = "bbk2";
const char *password = "internet2";

WiFiUDP ntpUDP;
// 'time.nist.gov' is used (default server) with +1 hour offset (3600 seconds) 60 seconds (60000 milliseconds) update interval
//NTPClient timeClient(ntpUDP, "time.nist.gov", 19800, 60000); //GMT+5:30 : 5*3600+30*60=19800
NTPClient timeClient(ntpUDP, "time.nist.gov", 7200, 60000); //GMT+2:00 : 2*3600+0*60=7200

byte _Second, _Minute;
byte _Hour;
unsigned long _Epoch;

byte nivel = 25;  // level of brightnesss (min) 32..255 (max)
byte am = 0;
byte y = 2;
byte lang = 0; // 0 - lb. romana, 1 - english

#ifdef ESP8266
// ISR for display refresh
void display_updater()
{
  display.display(70);
}
#endif

#ifdef ESP32
void IRAM_ATTR display_updater() {
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  //isplay.display(70);
  display.displayTestPattern(70);
  portEXIT_CRITICAL_ISR(&timerMux);
}
#endif

//=== SEGMENTS ===
#include "Digit.h"
Digit digit0(&display, 2, 28, y, display.color565(0, 0, nivel));  // units of seconds
Digit digit1(&display, 2, 23, y, display.color565(0, 0, nivel));  // tens of seconds
Digit digit2(&display, 2, 17, y, display.color565(nivel, nivel, 0));  // units of minutes
Digit digit3(&display, 2, 12, y, display.color565(nivel, nivel, 0));  // tens of minutes
Digit digit4(&display, 2, 6, y, display.color565(nivel, 0, 0));  // units of hours
Digit digit5(&display, 2, 1, y, display.color565(nivel, 0, 0));  // tens of hours 
Digit digit6(&display, 2, 1, y, display.color565(0, 0, 0));  // tens of hours invisible
//int changeSpeed = 500;

#include "TinyFont.h"
unsigned long tpceas;

int Seconda;
int Minut;    // get minutes (0 - 59)
int Ora;      // get hours   (0 - 23)
int Zi;
int Luna;
int An = -15;
int zi;

const long utcOffsetInSeconds = 7200;  // +2
byte DST = 0;
byte DST0 = 7;
#define DSTpin A0
byte h24 = 1; // 1 = 24-hour format, 0 = 12-hour format

// --- Variabile pentru Text Deplasabil ---
int scroll_X = 32;            
unsigned long prevScrollTime = 0; 
int scrollDelay = 150;         
String textSus = "";           // O lăsăm gol aici, îl vom compune dinamic
int textWidth = 0;             // <-- AICI se va memora lungimea în pixeli
// ---------------------------------------

String NumeZi[7] = {
"Dum~inica",  // m = m and ~
"Luni",
"Marti",
"Miercuri",
"Joi",
"Vineri",
"Sam~bata"
};

String NameDay[7] = {
"Sunday",
"Monday",
"Tuesday",
"Wednesday",
"Thursday",
"Friday",
"Saturday"
};
byte cetext  = 0;
uint16_t textColor = display.color565(0, 0, 20);
byte er, ge, be; 

// Craiova 
float latitude = 44.3167; 
float longitude = 23.8;
float altitudine = 100.0; 

String meteo1, meteo2, meteo3, meteo4, meteo5, meteo6, meteo7, meteo8, meteo9, meteo10;
byte ziuv = 0;
byte gata = 1;
unsigned long tpactualizare;
unsigned long tpactualizare0 = 1200000;


// --- FUNCȚIE PENTRU TRADUCEREA CODURILOR WMO ---
// Open-Meteo folosește coduri numerice pentru starea vremii
String traducereVreme(int cod) {
  switch(cod) {
    case 0: return "Cer senin";
    case 1: return "Predom~inant senin";
    case 2: return "Partial noros";
    case 3: return "Innorat";
    case 45: case 48: return "Ceata";
    case 51: case 53: case 55: return "Burnita";
    case 56: case 57: return "Burnita înghetata";
    case 61: case 63: case 65: return "Ploaie";
    case 66: case 67: return "Ploaie inghetata";
    case 71: case 73: case 75: return "Ninsoare";
    case 77: return "Granule de gheata";
    case 80: case 81: case 82: return "Averse de ploaie";
    case 85: case 86: return "Averse de ninsoare";
    case 95: return "Furtuna";
    case 96: case 99: return "Furtuna cu grindina";
    default: return "Necunoscut";
  }
}

String directieVant(int grade) {
 // const char* directii[] = {"Nord", "Nord-Est", "Est", "Sud-Est", "Sud", "Sud-Vest", "Vest", "Nord-Vest"};
  const char* directii[] = {"N", "NE", "E", "SE", "S", "SV", "V", "NV"};
  int index = int((float)(grade / 45.)) % 8;
  return directii[index];
}

String descriereUV(float uv) {
  if (uv <= 2) return  "Scazut";
  if (uv <= 5) return  "Moderat";
  if (uv <= 7) return  "Ridicat";
  if (uv <= 10) return "F.ridicat";
  return "Extreme";
}

String descriereAQI(float aqi) {
  if (aqi <= 20) return  "Bun";
  if (aqi <= 40) return  "Acceptabil";
  if (aqi <= 60) return  "Moderat";
  if (aqi <= 80) return  "Slab";
  if (aqi <= 100) return "F.slab";
  return "Extrem de slab";
}

// --- Adresa URL a fisierului XML de la BNR ---
const char* bnr_xml_url = "https://www.bnr.ro/nbrfxrates.xml";


void setup() {
  // put your setup code here, to run once:
  if (analogRead(DSTpin) < 500)  // check DST switch state
   DST = 0;  // winter
  else
   DST = 1;  // summer
   DST0 = DST; 
// Initialize a NTPClient to get time
  timeClient.begin();
  timeClient.setTimeOffset(utcOffsetInSeconds + 3600*DST);
  
  Serial.begin(115200);
  Serial.println(" ");
  Serial.println("Morphing clock on P10 display");  
  
  display.begin(8);
  display.flushDisplay();
  display.setTextWrap(false);
  // Define your scan pattern here {LINE, ZIGZAG, ZAGGIZ} (default is LINE)
  // display.setScanPattern(ZAGZIG);
  // display.setScanPattern(ZAGGIZ);
  // display.setScanPattern(ZIGZAG);

#ifdef ESP8266
  display_ticker.attach(0.002, display_updater);
#endif

#ifdef ESP32
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &display_updater, true);
  timerAlarmWrite(timer, 2000, true);
  timerAlarmEnable(timer);
#endif

  WiFi.begin(ssid, password);
  Serial.print("Connecting.");
  while ( WiFi.status() != WL_CONNECTED )
  {
    cetext++;
    delay(500);
    Serial.print(".");
    er = (cetext%8)/4;
    ge = ((cetext%8)%4)/2;
    be = ((cetext%8)%4)%2; 
    if (er + ge + be == 0)
    {
    er = 1;
    ge = 1;
    be = 1;
   }
    display.drawPixel(31, 15, display.color565 (nivel*er, nivel*ge, nivel*be));
  }
  cetext = 0;
  Serial.println("connected");
//  timeClient.begin();
  delay(10);
  display.drawPixel(31, 15, display.color565 (0, 0, 0));

  display.fillScreen(display.color565(0, 0, 0));
  TFDrawText (&display, "NTPclock", 0, 0, display.color565(nivel, nivel, nivel));  
  if (DST == 0)
   TFDrawText (&display, "GMT+2", 12, 10, display.color565 (nivel, nivel, nivel));
   else
   TFDrawText (&display, "GMT+3", 12, 10, display.color565 (nivel, nivel, nivel));

while (An < 0)
    {    
     timeClient.update();
     zi = timeClient.getDay();
     Ora = timeClient.getHours();
     Minut = timeClient.getMinutes();
     Seconda = timeClient.getSeconds(); 
    // Serial.println(zi);
    unsigned long unix_epoch = timeClient.getEpochTime();   // get UNIX Epoch time 
       Luna = month(unix_epoch);
       Zi = day(unix_epoch);    
       An = year(unix_epoch) - 2000;
     delay (1000);
    }
    if (h24 == 0)
    {
      if (Ora > 12) 
      {
        Ora = Ora - 12;
        am = 0;
      }
      else
      {
        Ora = Ora;
        am = 1;
      }
      if (Ora == 0) Ora = 12;
     } 
  delay(2000);
  display.drawPixel(31, 15, display.color565 (0, 0, 0));
  cursValutar();
  OpenMeteo(); 
  actualizeazaTextSus(); // go to create the text 
  display.fillScreen(display.color565(0, 0, 0));
        digit1.DrawColon(display.color565(nivel, nivel, nivel));
        digit3.DrawColon(display.color565(nivel, nivel, nivel));
        digit0.Draw(Seconda % 10);
        digit1.Draw(Seconda / 10);
        digit2.Draw(Minut % 10);
        digit3.Draw(Minut / 10);
        digit4.Draw(Ora % 10);
      //  digit5.Draw(Ora / 10);
       if (h24 == 0)
         {
          if (Ora >= 10) digit5.Draw(Ora / 10);
        else
        digit6.Draw(Ora / 10);
         }
        else
         digit5.Draw(Ora / 10); 
 }

void loop() {
  if (analogRead(DSTpin) < 500)  // check DST switch state
   DST = 0;
  else
   DST = 1;

  if (DST0 != DST)
    {
    display.fillScreen(display.color565(0, 0, 0));
    TFDrawText (&display, "Restart ", 0, 10, display.color565(nivel, 0, 0));  
    delay(1000);
    ESP.restart();
    }
  
  if (WiFi.status() == WL_CONNECTED)  // check WiFi connection status
  {
     // === LOGICA SCROLL TEXT SUS ===
      unsigned long currentMillis = millis(); 
      if (currentMillis - prevScrollTime >= scrollDelay) 
      {
        prevScrollTime = currentMillis;
        display.fillRect(0, 0, 31, 5, 0);  // clear the window
    TFDrawText (&display, textSus, scroll_X, 0, display.color565 (nivel*er, nivel*ge, nivel*be));  // show data
    scroll_X--;
   // Serial.println(scroll_X);
    if (textWidth == 0) textWidth = 32;     
    if (scroll_X < -textWidth) {  
      scroll_X = 32; // Reapare din dreapta
    //  cetext++;
      cetext = cetext + 1; 
      if ((cetext == 6) and (ziuv == 0))
          cetext = 7;      
      if (cetext > 10)
      {
        cetext = 0;
        if (millis()- tpactualizare > tpactualizare0)
        {
        display.fillScreen(display.color565(0, 0, 0)); 
        cursValutar();   
        OpenMeteo();
        digit1.DrawColon(display.color565(nivel, nivel, nivel));
        digit3.DrawColon(display.color565(nivel, nivel, nivel));
        digit0.Draw(Seconda % 10);
        digit1.Draw(Seconda / 10);
        digit2.Draw(Minut % 10);
        digit3.Draw(Minut / 10);
        digit4.Draw(Ora % 10);
        digit5.Draw(Ora / 10);
        }
      }
    Serial.print("cetext = ");
    Serial.println(cetext);
      actualizeazaTextSus(); // go to update the text for scrolling
    Serial.println(textSus);
    }   
  }
  // ==============================
   
    if ((millis() - tpceas > 1000) or (An < 0))
    {    
     timeClient.update();
     An = year(timeClient.getEpochTime()) - 2000;
     zi = timeClient.getDay();
     Ora = timeClient.getHours();
     Minut = timeClient.getMinutes();
     Seconda = timeClient.getSeconds(); 
    // Serial.println(zi);
     tpceas = millis();
      }
    unsigned long unix_epoch = timeClient.getEpochTime();   // get UNIX Epoch time 
    if (unix_epoch != _Epoch) {
    //  Seconda = second(unix_epoch);      // get seconds from the UNIX Epoch time
      // Serial.println(Seconda);
    //  Minut = minute(unix_epoch);    // get minutes (0 - 59)
    //  Ora   = hour(unix_epoch);        // get hours   (0 - 23)
       Luna = month(unix_epoch);
       Zi = day(unix_epoch);
    //  Serial.println(zi);
   // An = year(unix_epoch) - 2000;      zi = timeClient.getDay();

//TFDrawText (&display, lstr, xo, yo, cc_ylw);    
    if (h24 == 0)
    {
      if (Ora > 12) 
      {
        Ora = Ora - 12;
        am = 0;
      }
      else
      {
        Ora = Ora;
        am = 1;
      }
       if (Ora == 0) Ora = 12;
    }     
      if (_Epoch == 0)
      { // If we didn't have a previous time. Just draw it without morphing.
        digit0.Draw(Seconda % 10);
        digit1.Draw(Seconda / 10);
        digit2.Draw(Minut % 10);
        digit3.Draw(Minut / 10);
        digit4.Draw(Ora % 10);
        if (h24 == 0) 
        if (Ora >= 10) digit5.Draw(Ora / 10);
        else
        digit5.Draw(Ora / 10);
      }
      else
      
   //   if (_Epoch != 0)
      { 

        // epoch changes every miliseconds, we only want to draw when digits actually change.
        if (Seconda != _Second) {
          
         // digit1.DrawColon(display.color565(0, 0, 0));
         // digit3.DrawColon(display.color565(0, 0, 0));
          int s0 = Seconda % 10;
          int s1 = Seconda / 10;
          if (s0 != digit0.Value()) digit0.Morph(s0);
          if (s1 != digit1.Value()) digit1.Morph(s1);
          digit1.DrawColon(display.color565(nivel, nivel, nivel));
          digit3.DrawColon(display.color565(nivel, nivel, nivel));
          _Second = Seconda;
        }

        if (Minut != _Minute) {
          int m0 = Minut % 10;
          int m1 = Minut / 10;
          if (m0 != digit2.Value()) digit2.Morph(m0);
          if (m1 != digit3.Value()) digit3.Morph(m1);
          _Minute = Minut;
        }
        
  if (Ora != _Hour) {   
          int h0 = Ora % 10;
          int h1 = Ora / 10;
           if (h0 != digit4.Value()) digit4.Morph(h0);
          if (h24 == 0)
          {
          if (h1 > 0) 
          {
            digit5.Draw(1);
          }
          else 
           digit6.Draw(1);   // put tens of hours invisible
          }
          else
          digit5.Morph(h1);
          _Hour = Ora;
        }
      }
            _Epoch = unix_epoch;
      }
    display.drawPixel(31, 15, display.color565 (0, 0, 0));
    }
  if ( WiFi.status() != WL_CONNECTED )  // if wifi was lost
  {
    delay(500);
    Serial.print(".");
    display.drawPixel(31, 15, display.color565 (nivel, 0, 0));
  } 

}  // end main loop

// Wheater info
void OpenMeteo()
{ 
  gata = 0;
  Serial.println("Meteo data is searching !");   
//display.fillScreen(display.color565(0, 0, 0)); // clear the display
////TFDrawText (&display, "Caut....", 0, 0, display.color565(nivel, nivel, nivel));  
//delay(100);
//display.fillRect(22, 6, 31, 15, 0);  // clear the window
//display.fillScreen(display.color565(0, 0, 0)); // clear the display

    // ==========================================
    // PARTEA 1: DATE METEO (Vreme, Presiune, UV, Zi/Noapte)
    // ========================================== 
    {
    WiFiClientSecure client;
    HTTPClient http;
    
    // Pentru Open-Meteo este obligatoriu să setăm clientul ca "insecure" 
    // (ignoră verificarea certificatului SSL). Pe ESP8266 este necesar pentru a nu da eroare.
    client.setInsecure();

    // Construim URL-ul cererii. 
    // Parametrul "current=" cere exact datele care ne interesează în acest moment.
/*
   String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) + 
                 "&longitude=" + String(longitude, 4) + 
                 "&current=temperature_2m,relative_humidity_2m,surface_pressure,pressure_msl,weather_code,wind_speed_10m,wind_direction_10m&timezone=auto";
 */
      // Am adaugat "is_day" la finalul listei
      String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) + 
                   "&longitude=" + String(longitude, 4) + 
                   "&current=temperature_2m,relative_humidity_2m,surface_pressure,pressure_msl,weather_code,wind_speed_10m,wind_direction_10m,uv_index,is_day&timezone=auto";
  
    http.begin(client, url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
        Serial.print("Eroare la parsarea JSON: ");
        Serial.println(error.c_str());
      } else 
      {
        float temp = doc["current"]["temperature_2m"].as<float>();
        float umiditate = doc["current"]["relative_humidity_2m"].as<float>();
        int codVreme = doc["current"]["weather_code"].as<int>();
        String timp = doc["current"]["time"].as<String>();

        // Traducem codul numeric în text românesc
        String descriere = traducereVreme(codVreme);  
         
        // Extragere Zi/Noapte (1 = Zi, 0 = Noapte)
        int isDay = doc["current"]["is_day"].as<int>();
        String stareZiNoapte = isDay ? "Zi" : "Noapte";
      
        // Citim ambele tipuri de presiune (in hPa)
        float presiuneSol_hPa = doc["current"]["surface_pressure"].as<float>();
        float presiuneMSL_hPa = doc["current"]["pressure_msl"].as<float>();
        
        // Convertim in mmHg (1 hPa = 0.75006 mmHg)
        float presiuneSol_mmHg = presiuneSol_hPa * 0.75006;
        float presiuneMSL_mmHg = presiuneMSL_hPa * 0.75006;

        float vitezaVant = doc["current"]["wind_speed_10m"].as<float>();
        int directieGrade = doc["current"]["wind_direction_10m"].as<int>();
        float uvIndex = doc["current"]["uv_index"].as<float>();

        // Afișare
        int temp0 = temp*10.;
        int temp1 = temp0/10; 
        int temp2 = temp0%10;
        int directieGrade1 = directieGrade;
        int vitezaVant0 = vitezaVant*10.;
        int vitezaVant1 = vitezaVant0/10;
        int vitezaVant2 = vitezaVant0%10;
        int uv0 = uvIndex*10.;
        int uv1 = uv0/10; 
        int uv2 = uv0%10;
        ziuv = isDay;
        
        meteo1 = descriere;
        meteo2 = "Tem~peratura ";
        if (temp > 0)
         meteo2 = meteo2 + "+";
        meteo2 = meteo2 + temp1 + "." + temp2 + "#C";  // # is degree on the display
        meteo3 = "Um~iditate: ";
        meteo3 = meteo3 + int(umiditate) + "%";  
        meteo4 = "Presiune: ";
        meteo4 = meteo4 + int(presiuneMSL_mmHg+0.5) + " m~m~Hg";
        //meteo5 = "Vant din ";
        meteo5 = "Vant ";
        //meteo5 = meteo5 + vitezaVant1 + "." + vitezaVant2  + " km/h din " + directieVant(directieGrade1) + " !";
        //meteo5 = meteo5 + directieVant(directieGrade1) + " cu " + vitezaVant1 + "." + vitezaVant2  + " km/h";
        meteo5 = meteo5 + directieVant(directieGrade1) + " " + vitezaVant1 + "." + vitezaVant2  + "km~/h";
        //meteo6 = "indice UV: ";
        meteo6 = "UV: ";
        meteo6 = meteo6 + uv1 + "." + uv2 + " (" + descriereUV(uvIndex) + ")";
         
        Serial.println("\n=========================================");
        Serial.printf("Locatie: %.4f, %.4f\n", latitude, longitude);
        Serial.printf("Locatie: Craiova (Alt: %.0f m)\n", altitudine);
        // Serial.printf("Ora masuratoarii: %s\n", timp.c_str());
        // Afisam Zi/Noapte langa ora
          Serial.printf("Ora: %s (%s)\n", timp.c_str(), stareZiNoapte.c_str());
        Serial.println("-----------------------------------------");
        Serial.printf("Stare: %s (Cod %d)\n", descriere.c_str(), codVreme);
        Serial.printf("Temperatura: %.1f °C\n", temp);
        Serial.printf("Umiditate: %.0f %%\n", umiditate);     
        Serial.printf("Presiune_nivelul marii: %.0f mmHg\n", presiuneMSL_mmHg);
         // Afișăm presiunea în mmHg cu o zecimală (%.1f)
        Serial.printf("Presiune sol: %.1f mmHg\n", presiuneSol_mmHg);
        Serial.printf("Vant: %.1f km/h din %s (%d grade)\n", vitezaVant, directieVant(directieGrade).c_str(), directieGrade);
        Serial.printf("Indice UV: %.1f (%s)\n", uvIndex, descriereUV(uvIndex).c_str());
        Serial.println("=========================================\n");
      }
    } else {
      Serial.printf("Eroare la cererea HTTPS: %s\n", http.errorToString(httpCode).c_str());
    }   
    http.end();
    }
     
    // ==========================================
    // PARTEA 2: CALITATEA AERULUI (AQI, PM2.5, PM10)
    // ==========================================
    {
      WiFiClientSecure client2;
      HTTPClient http2;
      client2.setInsecure();

      String aqiUrl = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(latitude, 4) + 
                      "&longitude=" + String(longitude, 4) + 
                      "&current=european_aqi,pm2_5,pm10&timezone=auto";
      
      http2.begin(client2, aqiUrl);
      int httpCode2 = http2.GET();
      
      if (httpCode2 > 0) {
        String payload2 = http2.getString();
        DynamicJsonDocument doc2(512);
        DeserializationError error2 = deserializeJson(doc2, payload2);
        
        if (!error2) {
          float euAqi = doc2["current"]["european_aqi"].as<float>();
          float pm25 = doc2["current"]["pm2_5"].as<float>();
          float pm10 = doc2["current"]["pm10"].as<float>();

          int calitateaer = euAqi;
          int pm250 = pm25*10.;
          int pm251 = pm250/10; 
          int pm252 = pm250%10;
          int pm100 = pm10*10.;
          int pm101 = pm100/10; 
          int pm102 = pm100%10;
          
          //meteo7 = "indice poluare: ";
          meteo7 = "poluare:";
          //meteo7 = meteo7 + calitateaer + "  (" + descriereAQI(calitateaer) + ")";
          meteo7 = meteo7 + calitateaer + " " + descriereAQI(calitateaer);
          meteo8 = "PM2.5: ";
         // meteo8 = meteo8 + pm251 + "." + pm252 + " &m/g3 PM10 " + pm101 + "." + pm102 + " &m/g3" ;
          meteo8 = meteo8 + pm251 + "." + pm252 + " &g/m~3" ;
          meteo9 = "PM10: ";
          meteo9 = meteo9 + pm101 + "." + pm102 + " &g/m~3" ;
                    
          Serial.println("-----------------------------------------");
          Serial.printf("Indice Aer (European): %.0f (%s)\n", euAqi, descriereAQI(euAqi).c_str());
          Serial.printf("PM 2.5: %.1f µg/m³\n", pm25);
          Serial.printf("PM 10:  %.1f µg/m³\n", pm10);
          Serial.println("=========================================\n");
        
        } else {
          Serial.println("Eroare la parsarea JSON pentru Aer");
        }
      } else {
        Serial.printf("Eroare HTTPS Aer: %s\n", http2.errorToString(httpCode2).c_str());
      }
      http2.end();
      }
   
    Serial.println("Data was found ?!"); 
    gata = 1;
    tpactualizare = millis();
}

void cursValutar()
{
// ==========================================
    // PARTEA 3: CURS VALUTAR BNR (EUR / RON)
    // ==========================================
    {
    WiFiClientSecure client3;
    HTTPClient http3;

    // Pe ESP8266, pentru a nu avea probleme cu certificatele SSL, setam clientul ca "insecure"
    // (Acest lucru este ok pentru date publice necritice precum cursul valutar)
    client3.setInsecure();

    // Initializam conexiunea HTTPS
    http3.begin(client3, bnr_xml_url);
    
    // Trimitem cererea GET
    int httpCode3 = http3.GET();

    // Verificam daca am primit un raspuns valid (cod 200 inseamna OK)
    if (httpCode3 == HTTP_CODE_OK) {
      String payload3 = http3.getString(); // Salvam tot continutul XML intr-un String
      
      // Cautam cursul pentru EUR in XML
      // In fisierul BNR, cursul apare sub forma: <Rate currency="EUR">4.9500</Rate>
      
      int startIndex = payload3.indexOf("EUR"); 
      
      if (startIndex != -1) {
        // Gasim primul ">" dupa "EUR" pentru a ajunge la valoare
        int valStart = payload3.indexOf(">", startIndex) + 1;
        // Gasim "<" care inchide valoarea
        int valEnd = payload3.indexOf("<", valStart);
        
        // Extragem substring-ul cu valoarea
        String cursEuro = payload3.substring(valStart, valEnd);
        
        Serial.println("----------------------------------");
        Serial.print("Cursul EUR actualizat: ");
        Serial.println(cursEuro);
        Serial.println("----------------------------------");
        
        // Daca vrei sa il transformi in numar pentru calcule:
         float cursFloat = cursEuro.toFloat();
         meteo10 = "1 EURO: ";
         meteo10 = meteo10 + cursEuro + " RON        " ;
         Serial.print("-> ");
        Serial.println(cursFloat);
        Serial.println("----------------------------------");
     
      } else {
        Serial.println("Eroare: Nu s-a gasit eticheta EUR in XML.");
      }
    } else {
      Serial.printf("Eroare la cererea HTTP: %s\n", http3.errorToString(httpCode3).c_str());
    }
    
    // Eliberam resursele
    http3.end();
      Serial.println("=================//========================\n");
    }
}


void actualizeazaTextSus() {  // create the text for scrolling
    textSus = "";
     if (cetext == 0)
      {
        textSus = textSus + NumeZi[zi];
        textSus = textSus + " " + Zi/10 + Zi%10 + "." + Luna/10 + Luna%10 + ".20" + An;     
    /*
        cursValutar();   
        OpenMeteo();     
        Seconda = timeClient.getSeconds();
        digit1.DrawColon(display.color565(nivel, nivel, nivel));
        digit3.DrawColon(display.color565(nivel, nivel, nivel));
        digit0.Draw(Seconda % 10);
        digit1.Draw(Seconda / 10);
        digit2.Draw(Minut % 10);
        digit3.Draw(Minut / 10);
        digit4.Draw(Ora % 10);
        digit5.Draw(Ora / 10);
     */
      }
    else
    if (cetext == 1)
      textSus = meteo1; 
    else    
    if (cetext == 2)
      textSus = meteo2; 
    else  
    if (cetext == 3)
      textSus = meteo3; 
    else  
    if (cetext == 4)
      textSus = meteo4; 
    else
    if (cetext == 5)
      textSus = meteo5; 
    else
    if (cetext == 6)
      textSus = meteo6; 
    else
    if (cetext == 7)
      textSus = meteo7; 
    else
    if (cetext == 8)
      textSus = meteo8; 
    else
    if (cetext == 9)
      textSus = meteo9; 
    else
    if (cetext == 10)
      textSus = meteo10;  
         
      Serial.println(textSus);
  int16_t x1, y1;
  uint16_t w, h;
 //  display.getTextBounds(textSus.c_str(), 0, 0, &x1, &y1, &w, &h);
   display.getTextBounds(textSus.c_str(), 0, 0, &x1, &y1, &w, &h);
  // Salvează lățimea în variabila noastră
  textWidth = w * 4 / 6; // used character had lengh = 4 not 6 as defauld fonts
  Serial.print("Lenght of new text = ");
  Serial.println(textWidth);
  er = (cetext%8)/4;
  ge = ((cetext%8)%4)/2;
  be = ((cetext%8)%4)%2; 
  if (er + ge + be == 0)
  {
    Serial.println("black to white !");
    er = 1;
    ge = 1;
    be = 1;
  }
  Serial.print("R = ");
  Serial.print(er);
  Serial.print(", G = ");
  Serial.print(ge);  
  Serial.print(", B = ");
  Serial.println(be);  
  // ----------------------------------------
}

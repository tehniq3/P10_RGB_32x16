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
 v.3b1 - real update of EURO exchange rate after decreased size of memory by using client3.setBufferSizes(512, 512);
 v.3c - domino style for Morphing (unite of seconds -> tens of seconds -> units of minutes -> tens of minutes ->  
 v.3d - INLOCUIT BNR XML cu Frankfurter API (JSON mic) pentru a evita erorile de memorie/SSL la extragerea cursului Euro
      - FIX eroare -4: marit buffer SSL de la 512 la 2048, adaugat retry, heap check, yield
      - REVENIT la BNR XML ca sursa primara (curs oficial Romanesc) cu parsare simpla (fara lib XML),
        Frankfurter API ramane fallback (curs ECB poate diferi de BNR), validare 3.0-7.0 RON
 v.3d1- curs BNR: https://curs.bnr.ro/nbrfxrates.xml
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
    unsigned long unix_epoch = timeClient.getEpochTime();   
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
  yield();
  delay(500);
  OpenMeteo(); 
  actualizeazaTextSus(); 
  display.fillScreen(display.color565(0, 0, 0));
        digit1.DrawColon(display.color565(nivel, nivel, nivel));
        digit3.DrawColon(display.color565(nivel, nivel, nivel));
        digit0.Draw(Seconda % 10);
        digit1.Draw(Seconda / 10);
        digit2.Draw(Minut % 10);
        digit3.Draw(Minut / 10);
        digit4.Draw(Ora % 10);
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
  
  if (WiFi.status() == WL_CONNECTED) 
  {
     // === LOGICA SCROLL TEXT SUS ===
      unsigned long currentMillis = millis(); 
      if (currentMillis - prevScrollTime >= scrollDelay) 
      {
        prevScrollTime = currentMillis;
        display.fillRect(0, 0, 31, 5, 0);  
    TFDrawText (&display, textSus, scroll_X, 0, display.color565 (nivel*er, nivel*ge, nivel*be));  
    scroll_X--;
    if (textWidth == 0) textWidth = 32;     
    if (scroll_X < -textWidth) {  
      scroll_X = 32; 
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
        yield();
        delay(500);
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
      actualizeazaTextSus(); 
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
     tpceas = millis();
      }
    unsigned long unix_epoch = timeClient.getEpochTime();   
    if (unix_epoch != _Epoch) {
       Luna = month(unix_epoch);
       Zi = day(unix_epoch);

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
      { 
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
      { 
        if (Seconda != _Second) {
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
           digit6.Draw(1);   
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
  if ( WiFi.status() != WL_CONNECTED )  
  {
    delay(500);
    Serial.print(".");
    display.drawPixel(31, 15, display.color565 (nivel, 0, 0));
  } 

}  // end main loop

void OpenMeteo()
{ 
  gata = 0;
  Serial.println("Meteo data is searching !");   

    {
    WiFiClientSecure client;
    HTTPClient http;
    
    client.setInsecure();

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

        String descriere = traducereVreme(codVreme);  
         
        int isDay = doc["current"]["is_day"].as<int>();
        String stareZiNoapte = isDay ? "Zi" : "Noapte";
      
        float presiuneSol_hPa = doc["current"]["surface_pressure"].as<float>();
        float presiuneMSL_hPa = doc["current"]["pressure_msl"].as<float>();
        
        float presiuneSol_mmHg = presiuneSol_hPa * 0.75006;
        float presiuneMSL_mmHg = presiuneMSL_hPa * 0.75006;

        float vitezaVant = doc["current"]["wind_speed_10m"].as<float>();
        int directieGrade = doc["current"]["wind_direction_10m"].as<int>();
        float uvIndex = doc["current"]["uv_index"].as<float>();

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
        meteo2 = meteo2 + temp1 + "." + temp2 + "#C"; 
        meteo3 = "Um~iditate: ";
        meteo3 = meteo3 + int(umiditate) + "%";  
        meteo4 = "Presiune: ";
        meteo4 = meteo4 + int(presiuneMSL_mmHg+0.5) + " m~m~Hg";
        meteo5 = "Vant ";
        meteo5 = meteo5 + directieVant(directieGrade1) + " " + vitezaVant1 + "." + vitezaVant2  + "km~/h";
        meteo6 = "UV: ";
        meteo6 = meteo6 + uv1 + "." + uv2 + " (" + descriereUV(uvIndex) + ")";
         
        Serial.println("\n=========================================");
        Serial.printf("Locatie: %.4f, %.4f\n", latitude, longitude);
        Serial.printf("Locatie: Craiova (Alt: %.0f m)\n", altitudine);
        Serial.printf("Ora: %s (%s)\n", timp.c_str(), stareZiNoapte.c_str());
        Serial.println("-----------------------------------------");
        Serial.printf("Stare: %s (Cod %d)\n", descriere.c_str(), codVreme);
        Serial.printf("Temperatura: %.1f °C\n", temp);
        Serial.printf("Umiditate: %.0f %%\n", umiditate);     
        Serial.printf("Presiune_nivelul marii: %.0f mmHg\n", presiuneMSL_mmHg);
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
          
          meteo7 = "poluare:";
          meteo7 = meteo7 + calitateaer + " " + descriereAQI(calitateaer);
          meteo8 = "PM2.5: ";
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

// ==========================================
// ==========================================
// CURS VALUTAR - BNR direct prin curs.bnr.ro
// ==========================================
void cursValutar()
{
  Serial.println("=== Curs EUR/RON ===");
  Serial.printf("Heap liber inainte: %d bytes\n", ESP.getFreeHeap());

  bool succes = false;

  // =============================================
  // METODA 1: BNR XML (serverul dedicat curs.bnr.ro)
  // =============================================
  if (!succes)
  {
    Serial.println(">> Conectare la curs.bnr.ro:443...");
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setBufferSizes(1024, 512);
    client.setTimeout(6000);

    const char* host = "curs.bnr.ro";
    
    if (client.connect(host, 443))
    {
      Serial.println("Conectat la curs.bnr.ro! Trimit cerere...");
      
      client.print(String("GET /nbrfxrates.xml HTTP/1.1\r\n") +
                   "Host: " + host + "\r\n" +
                   "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n" +
                   "Accept: text/xml,application/xml,*/*\r\n" +
                   "Connection: close\r\n\r\n");

      unsigned long tStart = millis();
      while (!client.available() && client.connected() && (millis() - tStart < 4000)) {
        delay(10);
        yield();
      }

      char target[] = "currency=\"EUR\"";
      if (client.find(target))
      {
        // Sarim peste caracterul '>' care inchide tag-ul <Rate currency="EUR">
        client.readStringUntil('>');
        // Citim valoarea numerica pana la deschiderea tag-ului de inchidere '</Rate>'
        String rateStr = client.readStringUntil('<');
        rateStr.trim();
        float euroToRon = rateStr.toFloat();

        Serial.printf("BNR curs extras: '%s' -> %.4f\n", rateStr.c_str(), euroToRon);

        if (euroToRon > 3.0 && euroToRon < 7.0)
        {
          char buf[10];
          dtostrf(euroToRon, 4, 4, buf);
          meteo10 = "1 EURO: " + String(buf) + " RON";
          succes = true;
        }
      }
      else
      {
        Serial.println("BNR: Nu s-a gasit EUR! Raspunsul primit a fost:");
        while (client.available()) {
          char c = client.read();
          Serial.write(c);
        }
        Serial.println();
      }
      client.stop();
    }
    else
    {
      Serial.println("Eroare la conectare TCP/SSL pe curs.bnr.ro:443");
    }
    yield();
  }

  // =============================================
  // METODA 2: Fallback Frankfurter API (curs ECB)
  // =============================================
  if (!succes)
  {
    Serial.println(">> Fallback direct pe api.frankfurter.app...");

    WiFiClientSecure client;
    client.setInsecure();
    client.setBufferSizes(1024, 512);
    client.setTimeout(6000);

    const char* host = "api.frankfurter.app";

    if (client.connect(host, 443))
    {
      client.print(String("GET /latest?from=EUR&to=RON HTTP/1.1\r\n") +
                   "Host: " + host + "\r\n" +
                   "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n" +
                   "Accept: application/json\r\n" +
                   "Connection: close\r\n\r\n");

      unsigned long tStart = millis();
      while (!client.available() && client.connected() && (millis() - tStart < 4000)) {
        delay(10);
        yield();
      }

      char headerEnd[] = "\r\n\r\n";
      if (client.find(headerEnd))
      {
        String payload = client.readString();
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
          float euroToRon = doc["rates"]["RON"].as<float>();
          if (euroToRon > 3.0 && euroToRon < 7.0)
          {
            char buf[10];
            dtostrf(euroToRon, 4, 4, buf);
            meteo10 = "1 EURO: " + String(buf) + " RON";
            succes = true;
            Serial.printf("Frankfurter curs extras: %.4f\n", euroToRon);
          }
        }
      }
      client.stop();
    }
    else
    {
      Serial.println("Eroare conexiune Frankfurter");
    }
    yield();
  }

  if (!succes && meteo10.length() == 0) {
    meteo10 = "Err Curs";
  }

  Serial.printf("Heap liber dupa: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Sfarsit Curs ===\n");
}



void actualizeazaTextSus() {  
    textSus = "";
     if (cetext == 0)
      {
        textSus = textSus + NumeZi[zi];
        textSus = textSus + " " + Zi/10 + Zi%10 + "." + Luna/10 + Luna%10 + ".20" + An;     
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
   display.getTextBounds(textSus.c_str(), 0, 0, &x1, &y1, &w, &h);
  textWidth = w * 4 / 6; 
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
}

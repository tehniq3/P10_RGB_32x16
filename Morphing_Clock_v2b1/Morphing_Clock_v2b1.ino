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
*/

#define double_buffer
// ESP8266 WiFi main library
#include <ESP8266WiFi.h>

// Libraries for internet time
#include <WiFiUdp.h>
#include <NTPClient.h>          // include NTPClient library
#include <TimeLib.h>            // include Arduino time library -

#include <PxMatrix.h>           // https://github.com/2dom/PxMatrix
//segHeight = 2;

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
byte limba  = 0;
uint16_t textColor = display.color565(0, 0, 20);
byte er, ge, be; 

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
    delay(500);
    Serial.print(".");
    display.drawPixel(31, 15, display.color565 (nivel, 0, 0));
  }
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
 
  actualizeazaTextSus(); // go to create the text 
 }

void actualizeazaTextSus() {  // create the text for scrolling
 // limba++;
    //   textSus = "Wednesday, 15.04.2026";
 /*
 // textSus = "";
 // textSus = textSus + NumeZi[zi] + ", " + Zi + "." + Luna + ".20" + An;
       // Vineri, 17.4.2026 = 17 x 4 = 68 +2 = 70
       // Wednesday, 15.04.2026 = 21 x 4 = 84 + 2 = 86 worst case 
 */
    textSus = "";
    if (limba%2 == 0)  // lb. romana
      textSus = textSus + NumeZi[zi];
    else               // english
      textSus = textSus + NameDay[zi];
    textSus = textSus + ", " + Zi + "." + Luna + ".20" + An;     
      Serial.println(textSus);
  int16_t x1, y1;
  uint16_t w, h;
   display.getTextBounds(textSus.c_str(), 0, 0, &x1, &y1, &w, &h);
  // Salvează lățimea în variabila noastră
  textWidth = w * 4 / 6; // used character had lengh = 4 not 6 as defauld fonts
  Serial.print("Lenght of new text = ");
  Serial.println(textWidth);
  er = (limba%8)/4;
  ge = ((limba%8)%4)/2;
  be = ((limba%8)%4)%2; 
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
      limba++;
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
/*
      String lstr1 = "";
      lstr1 = lstr1 + Zi/10;
      lstr1 = lstr1 + Zi%10;
      lstr1 = lstr1 + "/";
      lstr1 = lstr1 + Luna/10;
      lstr1 = lstr1 + Luna%10;
      lstr1 = lstr1 + "/";
      lstr1 = lstr1 + An/10;
      lstr1 = lstr1 + An%10; 
*/
/*
      String lstr1 = "";
      lstr1 = lstr1 + Zi/10;
      lstr1 = lstr1 + Zi%10;
      lstr1 = lstr1 + Luna/10;
      lstr1 = lstr1 + Luna%10;
      lstr1 = lstr1 + "20"+ An/10;
      lstr1 = lstr1 + An%10; 

      String lstr = "";
  if (lang == 1)
  {
  if (zi == 0)
     lstr = lstr + " Sunday ";
  if (zi == 1)
     lstr = lstr + " Monday ";
  if (zi == 2)
     lstr = lstr + "Tuesday ";
  if (zi == 3)
     lstr = lstr + "  Wed.  ";
  if (zi == 4)
     lstr = lstr + "Thursday";
  if (zi == 5)
     lstr = lstr + " Friday ";
  if (zi == 6)
     lstr = lstr + "Saturday";
  }
  else
  {
  if (zi == 0)
     lstr = lstr + "Duminica";
  if (zi == 1)
     lstr = lstr + "  Luni  ";
  if (zi == 2)
     lstr = lstr + " Marti  ";
  if (zi == 3)
     lstr = lstr + "Miercuri";
  if (zi == 4)
     lstr = lstr + "  Joi   ";
  if (zi == 5)
     lstr = lstr + " Vineri ";
  if (zi == 6)
     lstr = lstr + "Sambata ";
  }
     lstr.toUpperCase ();
*/

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
          /*
          if (Seconda < 5)        //show AM / PM 
          {
          if (DST == 0)
            TFDrawText (&display, "GMT+2", 12, 0, display.color565 (nivel, nivel, nivel));
            else
            TFDrawText (&display, "GMT+3", 12, 0, display.color565 (nivel, nivel, nivel));
          if (h24 == 0) 
          {
          if (am == 1) // just if is 12-hour format
            TFDrawText (&display, "AM ", 0, 0, display.color565 (nivel, 0, nivel));
           else
            TFDrawText (&display, "PM ", 0, 0, display.color565 (nivel, 0, nivel));
          }
          else 
          TFDrawText (&display, "   ", 0, 0, display.color565 (nivel, 0, nivel));
          }
          else
          if (Seconda < 10)
            {
            TFDrawText (&display, lstr1, 0, 0, display.color565 (0, nivel, nivel));  // show data
            display.drawPixel( 8, 4, display.color565 (nivel, nivel, nivel));
            display.drawPixel(16, 4, display.color565 (nivel, nivel, nivel));
            }
          else
          {
            TFDrawText (&display, lstr, 0, 0, display.color565 (0, nivel, 0));  // show name of the day
            lang = (Seconda/5)%2;
          } 
          */
          
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

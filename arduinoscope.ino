/* 
###########################################################
  Title:                    Arduinoscope
  Created by:               Daniel Turbasa
  Note:                     Based on code from Brian O'Dell from the Fileark Arduino Oscilloscope.
  Requirement libraries:    Adafruit GFX library - Adafruit lirary for graphic displays
                            Adafruit PCD8544 - Adafruit library for Nokia LCD screen with PCD8544 controller
                            Keypad by Mark Stanley, Alexander Brevig - library for matrix style switches and keypads
                            FreqCount by Paul Stoffregen - library for frequency measurements
###########################################################
 */

/* Bibiloteki */
#include "Adafruit_GFX.h"
#include "Adafruit_PCD8544.h"
#include <SPI.h>
#include <Keypad.h>
#include <FreqCount.h>

/* Piny wejściowe/wyjściowe */
#define S_1 7     //sterowanie multiplekserem 1
#define S_2 6     //sterowanie multiplekserem 2
#define S_3 4     //sterowanie multiplekserem 3
#define ACDC 8    //przełącznik AC/DC
#define TRIGGER 5 //wejście triggera
#define PWM 3     //opcjonalne wyjście PWM

int channelAI = A0; //wejście analogowe

/* Konfiguracja wyświetlacza LCD */
// pin 9 - Serial clock out (SCLK)
// pin 10 - Serial data out (DIN)
// pin 11 - Data/Command select (D/C)
// pin 12 - LCD chip select (CS)
// pin 13 - LCD reset (RST)
Adafruit_PCD8544 display = Adafruit_PCD8544(9, 10, 11, 12, 13);

/* Konfiguracja przycisków */
/* 6 przycisków można skonfigurować jako matrycę wykorzystującą 5 pinów arduino */
const byte ROWS = 3; //wiersze
const byte COLS = 2; //kolumny
char keys[ROWS][COLS] = {
{'a','b'},
{'c','d'},
{'e','f'},
};

//Przypisanie przycisków do portów analogowych A1-A5
byte rowPins[ROWS] = {15, 16, 17};
byte colPins[COLS] = {18, 19};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);


/* Zmienne */
uint16_t delayVariable = 500; //okres próbkowana [ms]
float scale = 0;              //skalowanie wyników z przetwornika ADC
int xCounter = 0;             //pozycja na osi X
int yPosition = 0;            //pozycja na osi Y
int readings[85];             //bufor danych - szerokość wyświetlacza w pikselach

//stan początkowy multipleksera
bool S_1_State = LOW;          
bool S_2_State = LOW;
bool S_3_State = LOW;

//zmienne pomocnicze
int a = 0;                    //wybór wzmocnienia
int s = 0;                    //stan multipleksera
int previous_s = s;           //poprzednia wartość okresu próbkowania
int menus = 0;                //wubór menu
float show;                   //wyświetlanie wyniku pomiaru

//flagi odpowiadające za wyzwalanie
bool run = true;
bool sample = false;

//parametry badanego przebiegu
float minimum, maximum, aver, aver_temp, pk, g;
uint16_t frequency = 0;


void setup() {
  Serial.begin(9600);
  FreqCount.begin(1000);          //uruchomienie pomiarów częstotliwości; pomiar dla 1000 milisekund
  analogReference(EXTERNAL);      //zewnętrzne napięcie referencyjne dołączone do napięcia zasilania 3,3V
  pinMode(S_1, OUTPUT);
  pinMode(S_2, OUTPUT);
  pinMode(S_3, OUTPUT);
  pinMode(ACDC, INPUT);
  pinMode(TRIGGER, INPUT_PULLUP);
  digitalWrite(TRIGGER,HIGH);
  //pinMode(PWM, OUTPUT);

  //ustawienia wyświetlacza
  display.begin();
  display.setContrast(50);
  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);;
  
  //wyświetlanie ekranu powitalnego
  display.setCursor(0,0);
  display.println("");
  display.println("");
  display.println(" Arduinoscope");
  display.display();
  delay(1000);
  display.clearDisplay();  
}


void loop()
{  
  scale = 32.0/1023.0;    //mapowanie wartości z przetwornika ADC na piksele wyświetlacza
  
  menu();
  gain();

  /*******************************************************************************************************************************************************/
  /*Ze względu na zastosowany wzmacniacz odwracający w torze wejściowym oscyloskopu, do poprawnego odtworzenia badanego przebiegu, zebrane próbki
   należy odwrócić przemnażając je przez -1. Dodatkowo, ze względu na zastosowanie układu sztucznej masy, do wyniku należy dodać jej wartość.
   W efekcie wzór na wartość próbki wyrażonej w woltach można zaprezentować jako:

   napięcie = -1 * napięcie referencyjne / dziesiętna reprezentacja bitowej wartości przetwornika * wzmocnienie * wartość próbki + potencjał sztucznej masy

   U = -1 * 3.3 / 1024 * g * analogRead(channelAI) + 1.65
   */
  if(run == true)                                                 //uruchomienie pomiarów w zależności od ustawionej flagi
  {
    minimum = maximum = (-analogRead(channelAI)*3.3/1024 + 1.65)/g;   //ustalenie wartości początkowej dla pomiarów wartości minimalnej i maksymalnej
    aver_temp = 0;
    for(xCounter = 0; xCounter < 85; xCounter += 1)
    {
      yPosition = analogRead(channelAI);
      float temp = (-yPosition*3.3/1024 + 1.65)/g;
      if(temp > maximum)                            //pomiar wartości maksymalnej
        maximum = temp;
      if(temp < minimum)                            //pomiar wartości minimalnej
        minimum = temp;
      pk = abs(maximum - minimum);                                //pomiar wartości międzyszczytowej
      aver_temp += temp;
      readings[xCounter] = (-(yPosition*scale)+32);               //wpisanie wyników do bufora
      delayMicroseconds (delayVariable);                          //okres pomiędzy pobraniem kolejnych próbek
    }
    aver = aver_temp/86;
  }
  /*******************************************************************************************************************************************************/
  
  display.clearDisplay();
  readKeypad();

  menu();
  gain();

  //wyświetlanie informacji o trybie pracy AC/DC
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(35,0);
  
  if(digitalRead(ACDC) == HIGH)
  {
    display.print("AC");
  }
  else{
    display.print("DC");
  }

  //wyświetlanie skali
  float nominal_scale = 1.65;
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0,0);
  if ((int)(nominal_scale/g*100) %10 != 0){
    display.print(nominal_scale/g);
    display.setCursor(0,40);
    display.print(-nominal_scale/g);
  }
  else{
    display.print(nominal_scale/g,1);
    display.setCursor(0,40);
    display.print(-nominal_scale/g,1);
  }

  display.drawLine( 0, 39, 0, 7, BLACK);
  display.drawLine( 0, 39, 5, 39, BLACK);
  display.drawLine( 0, 31, 2, 31, BLACK);
  display.drawLine( 0, 23, 5, 23, BLACK);
  display.drawLine( 0, 15, 2, 15, BLACK);
  display.drawLine( 0, 7, 5, 7, BLACK);

  //prezentacja przebiegów na wyświetlaczu LCD
  for(xCounter = 0; xCounter < 85; xCounter += 1)
    {
       display.drawPixel(xCounter, 39-readings[xCounter], BLACK);
       if(xCounter>1){
         display.drawLine(xCounter-1, 39-readings[xCounter-1], xCounter, 39-readings[xCounter], BLACK);
       }
    }
  
  display.display();
} 

/* Obsługa menu programu */
void menu()
{
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(30,40);
  
switch(menus)
{
  case 0:
    sample = false;
    show = fminimum();
    display.print("min");
    break;
    
  case 1:
    sample = false;
    show = fmaximum();
    display.print("max");
    break;
  
  case 2:
    sample = false;
    show = fpk();
    display.print("pk-pk");
    break;
    
  case 3:
    sample = false;
    show = faver();
    display.print("aver");
    break;
  
  case 4:
    sample = false;
    show = freq();
    display.print("freq");
    break;

  case 5:
    sample = true;
    show = sampling();
    display.print("samp");
    break;
    
  default:
    menus = 0;
}
  //wyświetlanie wybranych informacji
  display.setTextSize(1);
  display.setTextColor(BLACK);
  if (sample != true)
  {
    display.setCursor(60,40);
    display.print(show);
  }
  else
  {
    display.setCursor(55,40);
    display.print(show,0);
  }
}

/* Funkcje pomocnicze typu get */
float fminimum()
{
  return(minimum);
}

float fmaximum()
{
  return(maximum);
}

float fpk()
{
  return(pk);
}

float faver()
{
  return(aver);
}

float getGain()
{
  return(g);
}

/* Funkcja obsługująca menu próbkowania */
uint16_t sampling()
{
  Serial.print(s);
  if(s > previous_s)
  {
    delayVariable += 50;
    if(delayVariable >= 10000)
      delayVariable = 10000;
  }
  else if (s < previous_s)
  {
    delayVariable -= 50;
    if(delayVariable <= 50)
      delayVariable = 50;
  }
  previous_s = s;
  Serial.println(previous_s);
  
//  switch(s)
//  {
//    case 0:
//      delayVariable = 100;
//      break;
//  
//    case 1:
//      delayVariable = 200;
//      break;
//    
//    case 2:
//      delayVariable = 500;
//      break;
//  
//    case 3:
//      delayVariable = 1000;
//      break;
//  
//    case 4:
//      delayVariable = 2000;
//      break;
//    
//    case 5:
//      delayVariable = 5000;
//      break;
//    
//    case 6:
//      delayVariable = 10000;
//      break;
//    
//    default:
//      delayVariable = 100;
//      s = 0;
//  }
  return (delayVariable);
}

/* Funkcja pobierająca wyniki pomiarów częstotliwości */
float freq()
{  
  if (FreqCount.available() and run == true) {
    frequency = FreqCount.read();
  }
  return(frequency);
}

/* Funkcja obsługująca wzmocnienie obwodów wejściowych oscyloskopu */
void gain()
{
  switch(a)
  {
    case 0:
      g = 0.1;
      S_1_State = LOW;
      S_2_State = LOW;
      S_3_State = LOW;
      break;
  
    case 1:
      g = 0.2;
      S_1_State = HIGH;
      S_2_State = LOW;
      S_3_State = LOW;
      break;
    
    case 2:
      g = 0.33;
      S_1_State = LOW;
      S_2_State = HIGH;
      S_3_State = LOW;
      break;
  
    case 3:
      g = 1;
      S_1_State = HIGH;
      S_2_State = HIGH;
      S_3_State = LOW;
      break;
  
    case 4:
      g = 2;
      S_1_State = LOW;
      S_2_State = LOW;
      S_3_State = HIGH;
      break;
    
    case 5:
      g = 3.3;
      S_1_State = HIGH;
      S_2_State = LOW;
      S_3_State = HIGH;
      break;
    
    case 6:
      g = 5;
      S_1_State = LOW;
      S_2_State = HIGH;
      S_3_State = HIGH;
      break;
    
    case 7:
      g = 10;
      S_1_State = HIGH;
      S_2_State = HIGH;
      S_3_State = HIGH;
      break;
    
    default:
      S_1_State = LOW;
      S_2_State = LOW;
      S_3_State = LOW;
      a = 0;
      g = 0.1;
  }

  //ustawienie wybranego wzmocnienia poprzez wybór odpowiedniego wejścia multipleksera
  digitalWrite(S_1, S_1_State);
  digitalWrite(S_2, S_2_State);
  digitalWrite(S_3, S_3_State);
  
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(60,0);
  display.print(g);
  
}

/* Funkcja obsługująca przyciski */
void readKeypad()
{
  char key = keypad.getKey();
  if (key != NO_KEY)
  {
    switch(key)
    {
      case 'a':
        menus--;
        break;
      case 'b':
        run = true;
        break;
      case 'c':
        menus++;
        break;
      case 'd':
        if(sample==true){
          s--;
        }
        else{
          a--;
        }
        break;
      case 'e':
        if (sample==true){
          s++;
        }
        else{
          a++;
        }
        break;
      case 'f':
        run = false;
        break;
      default:
      a = 0;
      menus = 0;
    }
  }
}

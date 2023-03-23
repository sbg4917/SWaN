/*
#########################
#      RESOURCES        #
#########################
 Written by Kevin M. Smith in 2013.
 Modified by David Lutz and Meredith Gurnee in 2017 and 2018
 Modified by Emma Hazard in 2019
 Modified by David Lutz, Emma Hazard in 2020
 Modified by Genevieve Goebel in 2021
 Contact: Emma.H.Hazard.22@dartmouth.edu or David.A.Lutz@dartmouth.edu
*/
/*
_________________________________________________________________________________________________________________________________________________________________________________
CODE LAYOUT:
_________________________________________________________________________________________________________________________________________________________________________________
1. LIBRARIES
2. CONSTANTS
3. SETUP
4. LOOP 
5. SLEEP FUNCTIONS -- sleepXbee(), wakeXbee(), wakeISR(), setupSleep(), systemSleep(), sensorsSleep(), sensorsWake()
6. SDI-12 FUNCTIONS -- addressRegister(), charToDec(), isTaken(), setTaken(), printInfo(), takeMeasurements(), checkActive()
7. RTC FUNCTIONS -- showTime(), setupTimer(), getDateTime(), getNow()
8. SD CARD FUNCTIONS -- setupLogFile(), LogData(), printBufferToScreen(), printParToScreen(), getBattery(), printBatteryToScreen()
_________________________________________________________________________________________________________________________________________________________________________________
_________________________________________________________________________________________________________________________________________________________________________________
*/
/*
#########################
# 1.   LIBRARIES        #
#########################
*/

#include <SDI12_PCINT3.h>       //include MayFly SDI12 library adjusted to fix conflict with PC_INT regarding RTC interrupt. Only controls PcInt 3.
#include <avr/sleep.h>       //include arduino library for sleep modes and interrupts
#include <avr/wdt.h>      //include arduino library to reset arduino. Helps save battery during sleep mode. 
#include <SPI.h>      //include library allows you to connect to serial interface
#include <SD.h>        //include library to write to SD card
#include <RTCTimer.h>       //include library for RTC timer
#include <Sodaq_DS3231.h>       //include library for the RTC
#include <Sodaq_PcInt_PCINT0.h>      //include library to allow RTC interrupts 
#include <Adafruit_ADS1X15.h>      // include library driver for 12-bit differential ADC (analogue to digital conversion) for analogue sensors

/*
#######################
# 2.   CONSTANTS      #
#######################
*/

/////////// PROJECT-DEPENDENT CONSTANTS /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOGGERNAME "SWaN1"         //name logger for data purposes
#define DATA_HEADER "Date/Time,SensorID,VWC,Temp,BEC" //adjust as needed for particular sensors

#define MAYFLY_ADDRESS "GQ12TB"        // name of specific Mayfly/node collecting data
#define MEASUREMENT_INTERVAL 30   // time interval (in minutes) between measurements 
#define FILE_NAME "GQ12TB.txt"       //name the data log file on the SD card
#define MINIMUM_VOLTAGE_MAYFLY 0       // The lowest voltage that the battery can reach before the Mayfly  will be shut off
#define BAUD_RATE 9600        // sets the baud rates for xbee and port serial; originally 57600 in STM code
#define STATION_NUMBER  0         // this staggers the real-time clock of the datalogger (in minutes)––this is useful when sending data from multiple stations to one receiver
#define DATAPIN 7         // change to the proper pin (probably either digital pin 6 or 7 on the Mayfly)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//defining necessary pins
#define RTC_PIN A7        // analogue pin 7 is the RTC pin on the Mayfly
#define RTC_INT_PERIOD EveryMinute      // dictate what time scale on which the RTC interrupt occurs 
#define SD_SS_PIN 12      // digital pin 12 is the MicroSD slave select pin on the Mayfly
#define POWERPIN 22       // change to the proper pin (this is 22 on the Mayfly)
#define BATTERY_PIN A6    // analogue input pin 6 controls the potentiometer (measures the battery)

//setting up RTC
RTCTimer timer;       //define the RTC as a timer
int currentminute;      //define the minute as an integer
long currentepochtime = 0;      //define epoch time as a long variable
float ONBOARD_BATTERY = 0;   // variable to store the value of the battery coming from the sensor as a floating-point number

// Aditional definitions
SDI12 mySDI12(DATAPIN);     //define the Datapin as an SDI12 input
Adafruit_ADS1015 ads;    // define ads as analogue to digital driver––use this to convert from Mayfly onboard 16-bit ADS1115

/*
#####################
# 3.   SETUP        #
#####################
*/

void setup(){
  Serial.begin(BAUD_RATE);      //initiate serial connection
  ads.begin();    // begin anonloge to digital conversion driver
  rtc.begin();      //start the RTC
  
  delay(100);     //delay 0.1 seconds
  setupTimer();        // set up timer event
  setupSleep();        // set up sleep mode
 
  showTime(getNow());    // get current time
  
  while(!Serial);   // allows serial compatibility with Leonardo/Arduino boards

  // Power the Mayfly and turns on red LED
  #if POWERPIN > 0
    pinMode(POWERPIN, OUTPUT);     // set the power pin D22 to output
    digitalWrite(POWERPIN, HIGH);    // turn on power
    delay(200);     // allow .2s for transmission time
  #endif

  mySDI12.begin();    //initialize data collection from SDI12 sensor
  delay(500);    // give SDI12 .5s to begin

/* 
  Keep track of active addresses each bit represents an address: 1 is active (taken), 0 is inactive (available) 
  setTaken('A') will set the proper bit for sensor 'A'
  Quickly Scan the Address Space
*/
  for(byte i = '0'; i <= '9'; i++) if(checkActive(i)) setTaken(i);   // scan address space 0-9, if address has active sensor, set address as "taken"

  for(byte i = 'a'; i <= 'z'; i++) if(checkActive(i)) setTaken(i);   // scan address space a-z, if address has active sensor, set address as "taken"

  for(byte i = 'A'; i <= 'Z'; i++) if(checkActive(i)) setTaken(i);   // scan address space A-Z, if address has active sensor, set address as "taken"

  // checks if sensors are active
  boolean found = false;
  for(byte i = 0; i < 62; i++){     // checks for all 62 (0-9, a-z, A-Z) addresses
    if(isTaken(i)){        // if the address is taken, boolean variable 'found' is set to true
      found = true;
      Serial.print("Found address:  ");
      Serial.println(i);
      break;
    }
  }

  // if no sensor address is found, print error message
  if(!found) {
    Serial.println("No sensors found, please check connections and restart Mayfly.");
    while(true);
  } 
  
  setupLogFile();     //initialize file for SD card
  
  //setup serial for data viewing, print header on serial port
  //Serial.println(LOGGERNAME);
  Serial.println();
  Serial.println(DATA_HEADER);
  Serial.println("--------------------------------------------------------------------------------------------------------------------------------------------------");
}

/*
####################
# 4.   LOOP        #
####################
*/

void loop()
{

  timer.update();     // restarts RTC timer at 0 seconds 
  
  if ((currentminute + STATION_NUMBER) % MEASUREMENT_INTERVAL == 0) {  // wakes up logger every 'x' minutes (staggered in minutes according to the station number)
     float volts = getBattery();     // finds voltage of battery connected to Mayfly

     // while voltage is below 'MINIMUM_VOLTAGE_MAYFLY' constant, the Mayfly is put to sleep
     while (volts < MINIMUM_VOLTAGE_MAYFLY) {
        systemSleep();    // Mayfly sleep
     }
   
   // scan addresses 0-9
   for(char i = '0'; i <= '9'; i++) if(isTaken(i)){   // if address is taken, the following funcitons will be called

    printInfo(i);           // this is EXTREMELY IMPORTANT. TDR sensors will not run if this function is not called--nothing is actually printed
    Serial.print("\t,");
    takeMeasurement(i);       // calls takeMeasurement function to record/write measurements from sensor to serial and SD card
    Serial.println();  
    delay(750);      // allow .75s for measurement to be sent––this is the minimum time necessary for TDR sensors
  }

  // scan addresses a-z
  for(char i = 'a'; i <= 'z'; i++) if(isTaken(i)){
    
    printInfo(i);   
    Serial.print("\t,");
    takeMeasurement(i); 
    Serial.println();
    delay(750);
  }

   // scan addresses A-Z
   for(char i = 'A'; i <= 'Z'; i++) if(isTaken(i)){
    
    printInfo(i);
    Serial.print("\t,");
    takeMeasurement(i);
    Serial.println();
    delay(750);
  }

//Consider using for other analog measurements (redox)
  //printParToScreen();      // prints data from PAR sensor
  //Serial.println();
  //delay(1000);          // delays .75s to give PAR time to finish taking measurements and print data

  printBatteryToScreen();    // print voltage of battery
  Serial.println();
  }

  delay(3000);       // delays three seconds to give time to finish tasks before sleeping
  systemSleep();      // Mayfly sleep
}

/*
#############################
# 5.   SLEEP FUNCTIONS      #
#############################
*/

// IMPORTANT: Make sure sleep mode (SM) in XCTU is set to "ASYNC. PIN SLEEP [1]"
// ONLY for sending dataloggers--leave recievers in "Normal [0]" mode 
// IMPORTANT: xBee sleep pin should be set to "Normal [0]" if using digimesh to hop nodes

// Interperrative Service Routine (ISR)--takes no parameters and returns nothing--necessary for interrupts
void wakeISR()
{
  //Leave this blank
}

void setupSleep()
{
  // enable RTC to interrupt sleeping xbee
  // real time clock remains on while Mayfly sleeps in order for interrupts to occur
  pinMode(RTC_PIN, INPUT_PULLUP);
  PcInt::attachInterrupt(RTC_PIN, wakeISR);
 
  // setup the RTC in interrupt mode
  rtc.enableInterrupts(RTC_INT_PERIOD);
  
  // set the sleep mode
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}

// function puts Mayfly to sleep
void systemSleep()
{
  // this method handles any sensor specific sleep setup
  sensorsSleep();

  // delays further actions until the serial ports have finished transmitting
  Serial.flush();
  
  // the next timed interrupt will not be sent until this is cleared
  rtc.clearINTStatus();
    
  // disable ADC
  ADCSRA &= ~_BV(ADEN);
  
  // puts mayfly to sleep 
  noInterrupts();
  sleep_enable();
  interrupts();
  sleep_cpu();
  sleep_disable();
 
  // enable ADC
  ADCSRA |= _BV(ADEN);
  
  // this method handles any sensor specific wake setup
  sensorsWake();
}
 
void sensorsSleep()
{
  //Add any code which your sensors require before sleep
}
 
void sensorsWake()
{
//  //Add any code which your sensors require after waking
}

/*
################################
# 6.   SDI 12 FUNCTIONS        #
################################
*/

//sets up address register for SDI 12
byte addressRegister[8] = {
  0B00000000,
  0B00000000,
  0B00000000,
  0B00000000,
  0B00000000,
  0B00000000,
  0B00000000,
  0B00000000
};

// converts allowable address characters '0'-'9', 'a'-'z', 'A'-'Z' to a decimal number between 0 and 61 (inclusive) to cover the 62 possible addresses
byte charToDec(char i){
  if((i >= '0') && (i <= '9')) return i - '0';
  if((i >= 'a') && (i <= 'z')) return i - 'a' + 10;
  if((i >= 'A') && (i <= 'Z')) return i - 'A' + 37;
  else return i;
}

// this quickly checks if the address has already been taken by an active sensor
boolean isTaken(byte i){
  i = charToDec(i);       // e.g. convert '0' to 0, 'a' to 10, 'Z' to 61.
  byte j = i / 8;       // byte #
  byte k = i % 8;       // bit #
  return addressRegister[j] & (1<<k); // return bit status
}

// this sets the bit in the proper location within the addressRegister to record that the sensor is active and the address is taken.
boolean setTaken(byte i){
  boolean initStatus = isTaken(i);
  i = charToDec(i);     // e.g. convert '0' to 0, 'a' to 10, 'Z' to 61.
  byte j = i / 8;       // byte #
  byte k = i % 8;       // bit #
  addressRegister[j] |= (1 << k);
  return !initStatus;      // return false if already taken
}

// this function is EXTREMELY IMPORTANT for certain kinds of sensors (I.E. the TDR sensors)
// this function prints ONLY to serial––does NOT print this information to SD card
// gets identification information (what type of sensor is being used) from a sensor, and prints it to the serial port
// expects a character between '0'-'9', 'a'-'z', or 'A'-'Z'.
void printInfo(char i){
  int j;
  String command = "";      
  command += (char) i;
  command += "I!";       // SDI-12 command to get sensor identification (sensor type) format [address]I!
  for(j = 0; j < 1; j++){
    mySDI12.sendCommand(command);      // sends [address]I! command to SDI12
    delay(30);
    if(mySDI12.available()>1) break;     // waits until SDI12 has processed the command
    if(mySDI12.available()) mySDI12.read();     // once command is processed, read information from SDI12
  } 
  while(mySDI12.available()){
    char c = mySDI12.read();     // copies sensor-specific information
    if((c!='\n') && (c!='\r')) 
    delay(5);
  }
}

// function takes measurements from sensors and builds a string 'buffer' containing the data
void takeMeasurement(char i){
  String command = "";
  command += i;
  command += "M!";
  mySDI12.sendCommand(command);       // [address][M][!] command begins taking sensor measurement, but does not return the measurement
  String sdiResponse = "";
  delay(30);
  while (mySDI12.available())    // build response string
  {
    char c = mySDI12.read();     // read time & number of measurements taken from sensor into string sdiResponse
    if ((c != '\n') && (c != '\r'))
    {
      sdiResponse += c;
      delay(5);
    }
  }
  mySDI12.clearBuffer();    // clear SDI12 buffer

  // find out how long we have to wait (in milliseconds) for measurements to be ready
  unsigned int wait = 0;
  wait = sdiResponse.substring(1,4).toInt();     // converts response from M! command into usable time

  unsigned long timerStart = millis();
  while((millis() - timerStart) < (1000 * wait)){      // waits given amount of time for measurement to be taken
    if(mySDI12.available())     // sensor can interrupt us to let us know it is done early
    {
      mySDI12.clearBuffer();       // clear SDI12 buffer
      break;
    }
  }
  // Wait for anything else and clear it out
  delay(30);
  mySDI12.clearBuffer();        // clear SDI12 buffer

  // D0! command sends data from measurements taken during M! command
  command = "";
  command += i;
  command += "D0!";     // SDI-12 command to get data [address][D][0][!]
  mySDI12.sendCommand(command);
  while(!mySDI12.available()>1);     // wait for acknowledgement that command has been processed
  delay(2000);     // give the data 2s to transfer
  printBufferToScreen(i);     // calls function to read data and print to serial and SD card
  delay(300);        // give time to transfer data
  mySDI12.clearBuffer();      // clear SDI12 buffer
}

// this checks for activity at a particular address expects a char, '0'-'9', 'a'-'z', or 'A'-'Z'
boolean checkActive(char i){

  String myCommand = "";          // create empty string for myCommand
  myCommand += (char) i;                 // sends basic 'acknowledge' command [address][!]
  myCommand += "!";

  for(int j = 0; j < 3; j++){            // goes through three rapid contact attempts
    mySDI12.sendCommand(myCommand);
    if(mySDI12.available()>1) break;       // waits until SDI12 sends command
    delay(30);
  }
  if(mySDI12.available()>2){       // if it hears anything it assumes the address is occupied
    mySDI12.clearBuffer();         // clear buffer
    return true;               // ends for loop and returns boolean of true 
  }
  else {                    // otherwise, address it is vacant.
    mySDI12.clearBuffer();         // clear buffer
  }
  return false;         //ends for loop and returns boolean of false
}

/*
#########################
# 7.   RTC FUNCTIONS    #
#########################
*/

void showTime(uint32_t ts)
{
  //Retrieve and display the current date/time
  String dateTime = getDateTime();
}

void setupTimer()
{  
  //Schedule the wakeup for every 'x' minutes (set as MEASUREMENT_INTERVAL in project dependent constants)
  timer.every(MEASUREMENT_INTERVAL, showTime);
  
  //Instruct the RTCTimer to get the current time
  timer.setNowCallback(getNow);
}

String getDateTime()
{
  String dateTimeStr;
  
  //Create a DateTime object from the current time
  DateTime dt(rtc.makeDateTime(rtc.now().getEpoch()));
 
  currentepochtime = (dt.get());     //Unix time in seconds 
 
  currentminute = (dt.minute());
  //Convert it to a String
  dt.addToString(dateTimeStr); 
  return dateTimeStr.substring(5, 16);     // prints out a substing of dateTime with characters 5-16 (i.e. does not inlude year or seconds to save string space)
}
 
uint32_t getNow()
{
  // get epoch time from real time clock and return
  currentepochtime = rtc.now().getEpoch();
  return currentepochtime;
}
 
/*
##############################
# 8.   SD CARD FUNCTIONS     #
##############################
*/

void setupLogFile()        //setting up the file on the SD card to record measurements
{
  //Initialize the SD card
  if (!SD.begin(SD_SS_PIN))
  {
    Serial.println("Error: SD card failed to initialize or is missing.");
  } 
  
  //Open the file in write mode
  File logFile = SD.open(FILE_NAME, FILE_WRITE);

  // format file with header
   logFile.println(LOGGERNAME);
   logFile.println("------------------------------------------------------------------------------------------------------------------------------");
  
  //Close the file to save it
  logFile.close();  
}

// function writes buffer with date, time, sensor address, voltage, and sensor measurements to SD card
void logData(String buffer)     
{
  //Re-open the file
  File logFile = SD.open(FILE_NAME, FILE_WRITE);
  
  //Write the txt data
  logFile.println(buffer);
  
  //Close the file to save it
  logFile.close();  
}

// constructs buffer to be printed to serial and SD card with time, date, sensor address, voltage, and sensor measurements 
void printBufferToScreen(char i)     
{
  String buffer = "";        //define buffer
  //buffer += ('$');        // begins buffer with '$'--receiver code uses this to determine the beginning of received strings
  String dateTime = getDateTime();
  buffer += (dateTime);     // add date and time to buffer
  buffer += "\t";
  buffer += MAYFLY_ADDRESS;      // add address given to mayfly as constant variable to buffer
  buffer += "\t";
  buffer += (char) i;      //puts address of sensor on buffer
  buffer += "\t";
  mySDI12.read();       // consume address 
  mySDI12.read();       // consume address

  while(mySDI12.available()){
    char c = mySDI12.read();        // read data from D0! command 
    if(c == '+' || c == '-'){       // format of SDI12 sensors puts '+' or '-' sign for measurements
      buffer += "\t";         // formats buffer with tab instead of '+' between measurements
      if(c == '-') buffer += '-';         // keeps negatives in front of measurements
    }
    else if ((c != '\n') && (c != '\r')) {       // adds all characters except for new lines into buffer
      buffer += c;  
    }
    
    delay(50);
  }

 logData(buffer);        // calls function to write buffer to SD card
 Serial.print(buffer);       // prints buffer to serial
 
 buffer += "?";      // ends buffer with '?'--receiver code uses this to determine the end of received strings 
}


// gets the voltage of Mayfly battery and returns as float
void printBatteryToScreen(){

  String buffer = "";        //define buffer
  //buffer += ('$');        // begins buffer with '$'--receiver code uses this to determine the beginning of received strings 
  String dateTime = getDateTime();
  buffer += (dateTime);     // add date and time to buffer
  buffer += "\t";
  buffer += MAYFLY_ADDRESS;      // add address given to mayfly as constant variable to buffer      
  buffer += "\t";
  buffer += "Battery:";
  buffer += "\t";
  float battery = getBattery();
  buffer += (String) (battery);      // adds voltage of Mayfly battery
  buffer += " volts";        // units

  delay(50);

  logData(buffer);        // calls function to write buffer to SD card
  Serial.print(buffer);       // prints buffer to serial
  
  buffer += "?";      // ends buffer with '?'--receiver code uses this to determine the end of received strings 
}


float getBattery() {
  
  // Get the battery voltage
  float rawBattery = analogRead(BATTERY_PIN);
  
  ONBOARD_BATTERY = (3.3/1023.) * 4.7 * rawBattery; 

  return ONBOARD_BATTERY;
}

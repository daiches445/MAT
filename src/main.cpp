
#include <ArduinoBLE.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <max6675.h>

#define INIT_CODE "12345678"
#define AUTH_FILE_NAME "auth.txt"
#define USER_FILE_NAME "user.txt"

/////Services/////

BLEService ignitonService("19B10000-E8F2-537E-4F6C-D104768A1214"); // create service
BLEService AuthService("A0B10000-E8F2-537E-4F6C-D104768A1214");
BLEService ResponseFromCentralService("911A0000-E8F2-537E-4F6C-D104768A1214");

/////Characteristics/////

//ignition on/off
BLEByteCharacteristic relayCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

//Login
BLEStringCharacteristic AuthUserDataCharacteristic("A0B10004-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);
//Register
BLEStringCharacteristic AuthRegisterCharacteristic("A0B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 300);
//Initilaize code (part of registration procedure)
BLEStringCharacteristic AuthInitCharacteristic("A0B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 8);

//BLEStringCharacteristic AuthValidateCharacteristic("A0B10005-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);

//UNUSED == FOR UID Authentication
BLEStringCharacteristic AuthCodeCharacteristic("A0B10001-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 50);

BLEByteCharacteristic responseFromCentralCharacteristic("911A0001-E8F2-537E-4F6C-D104768A1214", BLENotify);

struct AuthData
{
  bool registerd;
  bool authenticated;
  char init_code[8];
};

struct UserData
{
  const char *username;
  const char *password;
  const char *uuid;
};

const int RelayPin = 2; // pin to use for the Relay
const int thermo_SCK_pin = 13;//MAX6675 serial clock
const int thermo_CS_pin = 6;//MAX6675 chip select
const int thermo_SO_pin = 12;//MAX6675 serial out

MAX6675 thermocouple(thermo_SCK_pin,thermo_CS_pin,thermo_SO_pin);


String auth_code;
UserData user_data;
AuthData auth_data;

bool Register(BLEDevice central)
{

  bool init_code_ok = false; //flag for initial code
  String val = "";           //to obtain data from app
  File user_file;            //file to save on SDcard with new user data
  byte code_attempts = 0;    // add some delay on multiple failed code attempts.

  if (central) //if user is connected
  {
    Serial.println("REGISTER === Central recognized");

    while (central.connected())
    {
      if (code_attempts >= 5)
      {
        code_attempts = 0;
        delay(1000 * 60);
      }

      val = "";          //value reset
      if (!init_code_ok) //if init code hasnt sent
      {
        if (AuthInitCharacteristic.written()) //check if data was sent
        {
          Serial.println("REGISTER === Init code written");
          val = AuthInitCharacteristic.value(); //data from app

          Serial.println("value === " + val);
          Serial.println("INIT CODE  === " INIT_CODE);

          if (val != INIT_CODE) //if code is wrong
          {
            Serial.println("WRONG INIT CODE === register failed.");
            code_attempts += 1;
            AuthInitCharacteristic.writeValue("false"); //sent false as a response to app
          }
          else
          {
            Serial.println("CORRECT INIT CODE.");
            AuthInitCharacteristic.writeValue("true"); //send OK to app
            init_code_ok = true;                       //set flag to true and procced to user data gathering
          }
        }
      }
      else //the next step after receiving correct initial code.
      {
        if (AuthRegisterCharacteristic.written()) //check if user data received
        {

          val = AuthRegisterCharacteristic.value();        //data from app
          user_file = SD.open(USER_FILE_NAME, FILE_WRITE); //open SDcard file in write mode - new file will be created if exist

          Serial.println("USER DATE WRITTEN");
          Serial.print("USER DATE === :");
          Serial.println(val);

          if (!user_file) //error on file open/creation
          {
            Serial.println("ERROR OCCURED +++ USER FILE OPENING");
            SD.remove(USER_FILE_NAME);
            AuthRegisterCharacteristic.writeValue("ERROR OCCURED +++ USER FILE OPENING");
            return false;
          }

          user_file.write(val.c_str()); //write to SD card the new user details
          user_file.close();            //close file after writing

          //for testing
          user_file = SD.open(USER_FILE_NAME);

          if (user_file)
          {
            Serial.print("WRITTEN DATA TO FILE ===== ");
            while (user_file.available())
            {
              Serial.write(user_file.read());
            }
            user_file.close();
          }
          else
          {
            Serial.println("ERROR OPEN user_file");
          }

          AuthRegisterCharacteristic.writeValue("true");

          return true;
        }
      }
    }
  }

  return false;
}

bool Authenticate(BLEDevice central)
{
  unsigned long start_time = millis();
  unsigned long five_minutes = 300000;
  String val = "";
  StaticJsonDocument<100> APP_doc;
  DeserializationError err;

  if (central)
  {
    while (central.connected())
    {
      if (start_time + five_minutes < millis())
      {
        central.disconnect();
        return false;
      }

      //+++++++++++++++++++++++++++++++++
      // USERNAME PASSWORD AUTHENTICATION
      //+++++++++++++++++++++++++++++++++
      if (AuthUserDataCharacteristic.written())
      {

        Serial.println("USERDATA Char written");

        val = AuthUserDataCharacteristic.value();
        err = deserializeJson(APP_doc, val.c_str());

        if (err)
        {
          Serial.print("DeserializationError ====");
          Serial.println(err.c_str());
          AuthUserDataCharacteristic.writeValue("DeserializationError");
          return false;
        }

        const char *username_from_app = APP_doc["username"];
        const char *password_from_app = APP_doc["password"];

        Serial.println("VALUE FROM APP ====");
        Serial.println(username_from_app);
        Serial.println(password_from_app);

        int res = strcmp(user_data.username, username_from_app);
        Serial.print("strcmp res ===== ");
        Serial.println(res);

        if (strcmp(user_data.username, username_from_app) == 0)
        {
          if (strcmp(user_data.password, password_from_app) == 0)
          {
            Serial.print("LOGIN CORRECT");
            AuthUserDataCharacteristic.writeValue("true");
            return true;
          }
          else
          {
            AuthUserDataCharacteristic.writeValue("password");
          }
        }
        Serial.println("LOGIN FALSE");
        AuthUserDataCharacteristic.writeValue("username");

      }
    }
  }

  return false;
}

void onConnectedHandler(BLEDevice central)
{

  Serial.println("AUTH_HANDLER ===");
  Serial.print("Connected event, central: ");
  Serial.println(central.address());

  if (!SD.exists(USER_FILE_NAME)) //if user file not exist in SD,the device hasnt yet been activated and need to be registerd.
  {
    auth_data.registerd = Register(central);
    if (!auth_data.registerd) //if registration procces has failed disconnect current user/central device.
    {
      central.disconnect();
      return;
    }
  }

  File user_file = SD.open(USER_FILE_NAME);                      //open user data file from SD
  StaticJsonDocument<200> SD_doc;                                //define json document for data convertion.
  DeserializationError err = deserializeJson(SD_doc, user_file); //convert the SD JSON data to Strings

  if (err) //check for deserialize/convert error
  {
    Serial.print("DeserializationError ====");
    Serial.println(err.c_str());
    central.disconnect();
    return;
  }

  user_file.close();

  Serial.println("USER DETAILS FROM SD");

  user_data.username = SD_doc["username"]; //coping the user data for details comperison later
  user_data.password = SD_doc["password"];
  // user_data.uuid = SD_doc["uuid"]; currently unused.

  Serial.println(user_data.username);
  Serial.println(user_data.password);

  if(!auth_data.authenticated)
  {
    auth_data.authenticated = Authenticate(central);
  }

  if (auth_data.authenticated)
  {
    Serial.println("Login succesfull.");
    BLE.setAdvertisedService(ignitonService);
    BLE.setAdvertisedService(ResponseFromCentralService);
  }
}

void onDisconnectHandler(BLEDevice central)
{
  // central disconnected event handler
  Serial.print("Disconnected event, central: ");
  Serial.println(central.address());
  digitalWrite(RelayPin, LOW);
  auth_data.authenticated = false;
}

void switchCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic)
{

  if (!auth_data.authenticated)
  {
    central.disconnect();
    return;
  }

  // central wrote new value to characteristic, update LED
  Serial.print("Characteristic event, written: ");
  byte val = relayCharacteristic.value();
  Serial.println(val);
  if (val)
  {
    Serial.println("LED on");
    digitalWrite(RelayPin, HIGH);
  }
  else
  {
    Serial.println("LED off");
    digitalWrite(RelayPin, LOW);
  }
}

void setup()
{
  auth_data.authenticated = false;

  Serial.begin(9600);
  while (!Serial)
    ;

  pinMode(RelayPin, OUTPUT); // use the LED pin as an output

  //digitalWrite(thermo_CS_pin,HIGH);

  Serial.print("Initializing SD card...");

  if (!SD.begin(4))
  {
    Serial.println("initialization failed!");
    while (1)
      ;
  }
  Serial.println("initialization done.");

  SD.end();
  
  float temp = thermocouple.readCelsius();
  float high_temp = 100;
  while(temp < high_temp)
  {
    Serial.print("temp C -");
    Serial.println(temp);
    temp = thermocouple.readCelsius();
    delay(500);
  }
  

  // begin BLE initialization
  if (!BLE.begin())
  {
    Serial.println("starting BLE failed!");
    while (1)
      ;
  }

  //SD.remove(USER_FILE_NAME);

  // set the local name peripheral advertises
  BLE.setLocalName("MAT");

  // set the UUID for the service this peripheral advertises
  BLE.setAdvertisedService(AuthService);

  // add the characteristic to the service

  ignitonService.addCharacteristic(relayCharacteristic);

  AuthService.addCharacteristic(AuthRegisterCharacteristic);
  AuthService.addCharacteristic(AuthInitCharacteristic);
  AuthService.addCharacteristic(AuthUserDataCharacteristic);
  // AuthService.addCharacteristic(AuthValidateCharacteristic);

  ///WIP check if user is 'awake'
  uint8_t const desc1_data[] = {0x01, 0x02};
  BLEDescriptor isCentralConnectedDescriptor("2902", desc1_data, sizeof(desc1_data));

  ResponseFromCentralService.addCharacteristic(responseFromCentralCharacteristic);
  responseFromCentralCharacteristic.addDescriptor(isCentralConnectedDescriptor);

  //add services
  BLE.addService(AuthService);
  BLE.addService(ignitonService);
  BLE.addService(ResponseFromCentralService);

  // assign event handlers for connected, disconnected to peripheral
  BLE.setEventHandler(BLEConnected, onConnectedHandler);
  BLE.setEventHandler(BLEDisconnected, onDisconnectHandler);

  // assign event handlers for characteristic
  relayCharacteristic.setEventHandler(BLEWritten, switchCharacteristicWritten);
  //responseFromCentralCharacteristic.setEventHandler(BLEUpdated, isCentralConnected);
  // AuthValidateCharacteristic.setEventHandler(BLEWritten, onUserDataWritten);

  // set an initial value for the characteristic
  relayCharacteristic.setValue(0);
  AuthInitCharacteristic.setValue("null");
  AuthRegisterCharacteristic.setValue("null");
  responseFromCentralCharacteristic.setValue(0);

  // start advertising
  BLE.advertise();

  Serial.println(("Bluetooth device active, waiting for connections..."));
}

byte resFromCentral = 1;
unsigned long time_stamp = millis();

void loop()
{
  // poll for BLE events
  BLE.poll();

}

//////(WIP) check if central connected ////
// void isCentralConnected(BLEDevice central, BLECharacteristic characteristic)
// {
//   if (!auth_data.authenticated)
//   {
//     central.disconnect();
//     return;
//   }

//   // central signal for connection

//   Serial.print("Characteristic response from cental event, written: ");

//   byte val = 0;
//   characteristic.readValue(val);

//   Serial.print("Updated val = ");
//   Serial.println(val);
//   val = 1;
//   characteristic.writeValue(val,true);
// }

//////  - event based Authentiacte function,currently unused. ////////
// void onUserDataWritten(BLEDevice central, BLECharacteristic characteristic)
// {

//   if(!auth_data.registerd)
//     return;

//   Serial.println("DataValidate Char Written =====");
//   String val = "";
//   File uf = SD.open(USER_FILE_NAME);
//   StaticJsonDocument<200> Local_doc;
//   StaticJsonDocument<200> APP_doc;

//   DeserializationError err = deserializeJson(Local_doc, uf);
//   uf.close();
//   if (err)
//   {
//     Serial.print("DeserializationError local doc ====");
//     Serial.println(err.c_str());
//     return;
//   }

//   const char *username = Local_doc["username"];
//   const char *password = Local_doc["password"];

//   Serial.println("USER DETAILS FROM SD");
//   Serial.println(username);
//   Serial.println(password);

//   val = AuthValidateCharacteristic.value();
//   err = deserializeJson(APP_doc, val.c_str());

//   if (err)
//   {
//     Serial.print("DeserializationError data from app ====");
//     Serial.println(err.c_str());
//   }

//   const char *username_from_app = APP_doc["username"];
//   const char *password_from_app = APP_doc["password"];

//   Serial.println("VALUE FROM APP ====");
//   Serial.println(username_from_app);
//   Serial.println(password_from_app);

//   if (strcmp(user_data.username, username_from_app) == 0)
//   {
//     if (strcmp(user_data.password, password_from_app) == 0)
//     {
//       Serial.print("LOGIN CORRECT");
//       AuthValidateCharacteristic.writeValue("true");
//       auth_data.authenticated = true;
//       return;
//     }
//     else
//     {
//       AuthValidateCharacteristic.writeValue("password");
//     }
//   }

//   Serial.println("LOGIN FALSE");
//   AuthValidateCharacteristic.writeValue("username");
//   auth_data.authenticated = false;
// }

// //+++++++++++++++++++++++++++++++++
// // UUID AUTHENTICATION
// //+++++++++++++++++++++++++++++++++

// if (AuthCodeCharacteristic.written())
// {
//   Serial.println("Auth Char written");

//   val = AuthCodeCharacteristic.value();

//   Serial.print("VALUE FROM APP ====");
//   Serial.println(val.c_str());

//   if (strcmp(user_data.uuid, val.c_str()) == 0)
//   {
//     AuthCodeCharacteristic.writeValue("true");
//   }
//   Serial.println("LOGIN FALSE");
//   AuthCodeCharacteristic.writeValue("false");

//   return false;
// }
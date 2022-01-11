
#include <ArduinoBLE.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Arduino_LSM9DS1.h>

#define INIT_CODE "12345678"
#define AUTH_FILE_NAME "auth.txt"
#define USER_FILE_NAME "user.txt"

/////Services/////

BLEService ignitonService("19B10000-E8F2-537E-4F6C-D104768A1214"); // create service
BLEService AuthService("A0B10000-E8F2-537E-4F6C-D104768A1214");
BLEService ResponseFromCentralService("911A0000-E8F2-537E-4F6C-D104768A1214");
BLEService tempService("5A1A0000-E8F2-537E-4F6C-D104768A1214");


/////Characteristics/////

//ignition on/off
BLEByteCharacteristic relayCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

//Login
BLEStringCharacteristic AuthLoginCharacteristic("A0B10004-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);
//Register
BLEStringCharacteristic AuthRegisterCharacteristic("A0B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 300);
//Initilaize code (part of registration procedure)
BLEStringCharacteristic AuthInitCharacteristic("A0B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 8);
//Reset the device
BLEStringCharacteristic ResetCharacteristic("A0B10010-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);
//BLEStringCharacteristic AuthValidateCharacteristic("A0B10005-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);

//UNUSED == FOR UID Authentication
BLEStringCharacteristic AuthCodeCharacteristic("A0B10001-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 50);

BLEByteCharacteristic responseFromCentralCharacteristic("911A0001-E8F2-537E-4F6C-D104768A1214", BLENotify);

BLEFloatCharacteristic tempCharacteristic("5A1A0001-E8F2-537E-4F6C-D104768A1214",  BLENotify );

struct UserData
{
  String username;
  String password;
  String uuid;
};

const int RelayPin = 5;  //pin to use for the Relay
const int sd_CS_pin = 4; //SD chip select
bool authenticated = false;

UserData user_data;

bool Register(BLEDevice central)
{

  bool init_code_ok = false; //flag for initial code
  String val = "";           //to obtain data from app
  File user_file;            //file to save on SDcard with new user data
  byte code_attempts = 0;    // add some delay on multiple failed code attempts.
  long code_sent;

  if (central) //if user is connected
  {

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
          code_sent = millis();
          val = AuthInitCharacteristic.value(); //data from app


          if (val != INIT_CODE) //if code is wrong
          {
            code_attempts += 1;
            AuthInitCharacteristic.writeValue("false"); //sent false as a response to app
          }
          else
          {
            AuthInitCharacteristic.writeValue("true"); //send OK to app
            init_code_ok = true;                       //set flag to true and procced to user data gathering
          }
        }
      }
      else //the next step after receiving correct initial code.
      {
        long wait4data = millis();

        if (AuthRegisterCharacteristic.written()) //check if user data received
        {

          val = AuthRegisterCharacteristic.value();        //data from app
          user_file = SD.open(USER_FILE_NAME, FILE_WRITE); //open SDcard file in write mode - new file will be created if exist


          if (!user_file) //error on file open/creation
          {
            SD.remove(USER_FILE_NAME);
            AuthRegisterCharacteristic.writeValue("ERROR OCCURED +++ USER FILE OPENING");
            return false;
          }

          user_file.write(val.c_str()); //write to SD card the new user details
          user_file.close();            //close file after writing


          AuthRegisterCharacteristic.writeValue("true");
          File user_file = SD.open(USER_FILE_NAME);                      //open user data file from SD
          StaticJsonDocument<200> SD_doc;                                //define json document for data convertion.
          DeserializationError err = deserializeJson(SD_doc, user_file); //convert the SD JSON data to Strings
          JsonObject obj = SD_doc.as<JsonObject>();

          if (err) //check for deserialize/convert error
          {
            return false;
          }
          user_file.close();

          String uname = obj["username"]; //coping the user data for later comparison
          String pswd = obj["password"];
          user_data.username = uname;
          user_data.password = pswd;
          return true;
        }
        else
        {
          if (wait4data - code_sent > 10 * 1000)
          {
            init_code_ok = false;
            AuthInitCharacteristic.writeValue("false");
          }
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
      if (AuthLoginCharacteristic.written())
      {


        val = AuthLoginCharacteristic.value();       //get data from app
        err = deserializeJson(APP_doc, val.c_str()); //convert JSON data from app to Struct - UserData

        if (err)
        {

          AuthLoginCharacteristic.writeValue("DeserializationError");
          return false;
        }

        const char *username_from_app = APP_doc["username"];
        const char *password_from_app = APP_doc["password"];


        // String local_username_string = user_data.username;
        // String local_password_string = user_data.password;



        if (user_data.username == username_from_app)
        {
          if (user_data.password == password_from_app)
          {
            AuthLoginCharacteristic.writeValue("true");
            return true;
          }
          else
          {
            AuthLoginCharacteristic.writeValue("password");
          }
        }
        AuthLoginCharacteristic.writeValue("username");
      }
    }
  }

  return false;
}

void onConnectedHandler(BLEDevice central)
{
  AuthLoginCharacteristic.writeValue("R");

  if (!SD.exists(USER_FILE_NAME)) //if user file not exist in SD,the device hasnt yet been activated and need to be registerd.
  {
    bool registerd = false;
    AuthLoginCharacteristic.setValue("unregisterd");
    while (!registerd) //if registration procces has failed disconnect current user/central device.
    {
      registerd = Register(central);
    }
  }

  authenticated = Authenticate(central);

  if (authenticated)
  {
    BLE.setAdvertisedService(ignitonService);
    BLE.setAdvertisedService(ResponseFromCentralService);
  }
  else
  {
    central.disconnect();
  }
}

void onDisconnectHandler(BLEDevice central)
{
  // central disconnected event handler
  digitalWrite(RelayPin, LOW);
  authenticated = false;
}

void switchCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic)
{

  if (!authenticated)
  {
    central.disconnect();
    return;
  }

  // central wrote new value to characteristic, update LED
  byte val = relayCharacteristic.value();
  if (val)
  {
    digitalWrite(RelayPin, HIGH);
  }
  else
  {
    digitalWrite(RelayPin, LOW);
  }
}

void ResetDevice(BLEDevice central, BLECharacteristic characteristic)
{
  if (!SD.exists(USER_FILE_NAME))
    return;
  if (!authenticated)
    return;

  String val;
  StaticJsonDocument<100> APP_doc;

  while (central.connected())
  {
    if (ResetCharacteristic.written())
    {

      val = ResetCharacteristic.value();
      DeserializationError err = deserializeJson(APP_doc, val.c_str()); //convert JSON data from app to Struct - UserData

      if (err)
      {

        ResetCharacteristic.writeValue("DeserializationError");
        return;
      }

      // const char *username_from_app = APP_doc["username"];
      // const char *password_from_app = APP_doc["password"];

      // if (strcmp(user_data.username, APP_doc["username"]) == 0)
      // {
      //   if (strcmp(user_data.password, APP_doc["password"]) == 0)
      //   {
      //     // if (!SD.remove(USER_FILE_NAME))
      //     // {
      //     //   Serial.print("RESET failed - SD ERROR");
      //     //   ResetCharacteristic.writeValue("SD ERROR");
      //     //   return;
      //     // }
      //     Serial.print("RESET success");
      //     ResetCharacteristic.writeValue("success");
      //     authenticated = false;
      //     central.disconnect();
      //   }
      // }
      ResetCharacteristic.writeValue("failed");
    }
  }
}

void NotifyTemp(){

}
bool InitSD()
{

  if (!SD.begin(sd_CS_pin))
  {
    return false;
  }

  SD.remove(USER_FILE_NAME);

  if (SD.exists(USER_FILE_NAME))
  {
    File user_file = SD.open(USER_FILE_NAME);                      //open user data file from SD
    StaticJsonDocument<200> SD_doc;                                //define json document for data convertion.
    DeserializationError err = deserializeJson(SD_doc, user_file); //convert the SD JSON data to Strings
    JsonObject SD_obj = SD_doc.as<JsonObject>();

    if (err) //check for deserialize/convert error
    {
      return false;
    }
    user_file.close();

    String uname = SD_obj["username"];
    String pswd = SD_obj["password"];

    user_data.username = uname; //coping the user data for later comparison
    user_data.password = pswd;

  }
  else
  {
    AuthLoginCharacteristic.setValue("unregisterd");
  }
  return true;
}

void InitBLE()
{
  // begin BLE initialization
  while (!BLE.begin())

  // set the local name peripheral advertises
  BLE.setLocalName("MAT");

  // set the UUID for the service this peripheral advertises
  BLE.setAdvertisedService(AuthService);

  // add the characteristic to the service
  ignitonService.addCharacteristic(relayCharacteristic);

  AuthService.addCharacteristic(AuthRegisterCharacteristic);
  AuthService.addCharacteristic(AuthInitCharacteristic);
  AuthService.addCharacteristic(AuthLoginCharacteristic);
  AuthService.addCharacteristic(ResetCharacteristic);
  // AuthService.addCharacteristic(AuthValidateCharacteristic);
  tempService.addCharacteristic(tempCharacteristic);

  ///WIP check if user is 'awake'
  uint8_t const desc1_data[] = {0x01, 0x02};
  BLEDescriptor isCentralConnectedDescriptor("2902", desc1_data, sizeof(desc1_data));

  ResponseFromCentralService.addCharacteristic(responseFromCentralCharacteristic);
  responseFromCentralCharacteristic.addDescriptor(isCentralConnectedDescriptor);

  //add services to BLE adapter
  BLE.addService(AuthService);
  BLE.addService(ignitonService);
  BLE.addService(ResponseFromCentralService);
  BLE.addService(tempService);

  // assign event handlers for connected, disconnected to peripheral
  BLE.setEventHandler(BLEConnected, onConnectedHandler);
  BLE.setEventHandler(BLEDisconnected, onDisconnectHandler);

  // assign event handlers for characteristic
  relayCharacteristic.setEventHandler(BLEWritten, switchCharacteristicWritten);
  ResetCharacteristic.setEventHandler(BLEWritten, ResetDevice);
  //responseFromCentralCharacteristic.setEventHandler(BLEUpdated, isCentralConnected);
  // AuthValidateCharacteristic.setEventHandler(BLEWritten, onUserDataWritten);

  // set an initial value for the characteristic
  relayCharacteristic.setValue(0);
  AuthInitCharacteristic.setValue("null");
  AuthRegisterCharacteristic.setValue("null");
  responseFromCentralCharacteristic.setValue(0);

  // start advertising
  BLE.advertise();
}

bool imu;
void setup()
{
  pinMode(RelayPin,OUTPUT);


  //initialize the sd card and get signed user detail.
  InitSD();

  //setup BLE services,charactaristics and start advertising.
  InitBLE();

  imu = IMU.begin();

}

byte resFromCentral = 1;
unsigned long time_stamp = millis();
float x,y,z;

void loop()
{
  // poll for BLE events
  BLE.poll();
  if(imu){
    if(IMU.accelerationAvailable()){
      IMU.readAcceleration(x,y,z);

      tempCharacteristic.writeValue(x);
      delay(500);
    }
  }
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
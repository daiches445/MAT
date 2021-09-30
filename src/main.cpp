
#include <ArduinoBLE.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

#define INIT_CODE "12345678"
#define AUTH_FILE_NAME "auth.txt"
#define USER_FILE_NAME "user.txt"

BLEService ignitonService("19B10000-E8F2-537E-4F6C-D104768A1214"); // create service
BLEService AuthService("A0B10000-E8F2-537E-4F6C-D104768A1214");

// create switch characteristic and allow remote device to read and write
BLEByteCharacteristic relayCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

BLEStringCharacteristic AuthCodeCharacteristic("A0B10001-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 50);
BLEStringCharacteristic AuthUserDataCharacteristic("A0B10004-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);
BLEStringCharacteristic AuthRegisterCharacteristic("A0B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 300);
BLEStringCharacteristic AuthInitCharacteristic("A0B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 8);
BLEStringCharacteristic AuthValidateCharacteristic("A0B10005-E8F2-537E-4F6C-D104768A1214", BLEWrite | BLERead, 128);

struct AuthData
{
  bool registerd;
  bool authenticated;
  char init_code[8];
};

struct UserData
{
  char *username;
  char *password;
  char uuid[50];
};

File auth_file;
File user_file;

const int ledPin = LED_BUILTIN; // pin to use for the LED
// bool registerd = false;
//bool auth = false;
String auth_code;
UserData user_data;
AuthData auth_data;
int auth_attempts = 0;

bool Register(BLEDevice central)
{
  StaticJsonDocument<192> doc;
  bool isInit_code = false;
  String val = "";
  File af;
  File uf;
  DeserializationError error;

  if (central)
  {
    Serial.println("REGISTER === Central recognized");

    while (central.connected())
    {
      val = "";
      if (!isInit_code)
      {
        if (AuthInitCharacteristic.written())
        {
          Serial.println("REGISTER === Init code written");
          val = AuthInitCharacteristic.value();

          Serial.println("value === " + val);
          Serial.println("INIT CODE  === " INIT_CODE);

          if (val != INIT_CODE)
          {
            Serial.println("WRONG INIT CODE === register failed.");
            auth_attempts += 1;
            AuthInitCharacteristic.writeValue("false");
            return false;
          }
          else
          {
            Serial.println("CORRECt INIT CODE.");
            AuthInitCharacteristic.writeValue("true");
            isInit_code = true;
          }
        }
      }
      else
      {
        if (AuthRegisterCharacteristic.written())
        {
          Serial.println("USER DATE WRITTEN");

          //AuthData ad;
          val = AuthRegisterCharacteristic.value();
          Serial.print("USER DATE === :");
          Serial.println(val);

          uf = SD.open(USER_FILE_NAME, FILE_WRITE);
          if (!uf)
          {
            Serial.println("ERROR OCCURED +++ USER FILE OPENING");
            AuthRegisterCharacteristic.writeValue("ERROR OCCURED +++ USER FILE OPENING");
            isInit_code = false;
            break;
          }

          uf.write(val.c_str());
          uf.close();

          uf = SD.open(USER_FILE_NAME);

          if (uf)
          {
            Serial.print("WRITTEN DATA TO FILE ===== ");
            while (uf.available())
            {
              Serial.write(uf.read());
            }
            uf.close();
          }
          else
          {
            Serial.println("ERROR OPEN uf");
          }

          AuthRegisterCharacteristic.writeValue("true");

          return isInit_code;
        }
      }
    }
  }

  return false;
}

bool Authenticate(BLEDevice central)
{


  //move to Authhanlder
  String val = "";
  File uf = SD.open(USER_FILE_NAME);
  StaticJsonDocument<200> SD_doc;
  StaticJsonDocument<100> APP_doc;

  DeserializationError err = deserializeJson(SD_doc, uf);

  if (err)
  {
    Serial.print("DeserializationError ====");
    Serial.println(err.c_str());
    return false;
  }
  uf.close();

  Serial.println("USER DETAILS FROM SD");
  const char *username = SD_doc["username"];
  const char *password = SD_doc["password"];
  const char *uuid = SD_doc["uuid"];

  Serial.println(username);
  Serial.println(uuid);

  uf.close();

  if (central)
  {
    
    while (central.connected())
    {

      //+++++++++++++++++++++++++++++++++
      // UUID AUTHENTICATION
      //+++++++++++++++++++++++++++++++++

      if (AuthCodeCharacteristic.written())
      {
        Serial.println("Auth Char written");

        val = AuthCodeCharacteristic.value();
        Serial.print("VALUE FROM APP ====");
        Serial.println(val.c_str());

        if (strcmp(uuid, val.c_str()) == 0)
        {
          AuthCodeCharacteristic.writeValue("true");
        }
        Serial.println("LOGIN FALSE");
        AuthCodeCharacteristic.writeValue("false");

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
        }

        const char *username_from_app = APP_doc["username"];
        const char *password_from_app = APP_doc["password"];

        Serial.print("VALUE FROM APP ====");
        Serial.println(username_from_app);
        Serial.println(password_from_app);

        if (strcmp(username, username_from_app) == 0)
        {
          if (strcmp(password_from_app, password) == 0)
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
        AuthUserDataCharacteristic.writeValue("false");

        return false;
      }
    }
  }

  return false;
}

void AuthHandler(BLEDevice central)
{
  Serial.println("AUTH_HANDLER ===");

  Serial.print("Connected event, central: ");
  Serial.println(central.address());

  if (!SD.exists(USER_FILE_NAME))
  {
    while (!auth_data.registerd)
    {
      auth_data.registerd = Register(central);
    }
  }

  while (!auth_data.authenticated)
  {
    auth_data.authenticated = Authenticate(central);
  }

  if (auth_data.authenticated)
  {
    BLE.addService(ignitonService);
  }
}

void blePeripheralConnectHandler(BLEDevice central)
{

  // central connected event handler

  AuthHandler(central);
  // while(!registerd){
  //   registerd = Register(central);
  // }

  // auth = Authenticate(central);
  // if(!auth){
  //   central.disconnect();
  // }
}

void blePeripheralDisconnectHandler(BLEDevice central)
{
  // central disconnected event handler
  Serial.print("Disconnected event, central: ");
  Serial.println(central.address());
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

  if (relayCharacteristic.value())
  {
    Serial.println("LED on");
    digitalWrite(ledPin, HIGH);
  }
  else
  {
    Serial.println("LED off");
    digitalWrite(ledPin, LOW);
  }
}

void userDataWritten(BLEDevice central, BLECharacteristic characteristic)
{
  Serial.println("DataVAlidate Char Written =====");
  String val = "";
  File uf = SD.open(USER_FILE_NAME);
  StaticJsonDocument<200> Local_doc;
  StaticJsonDocument<100> APP_doc;

  DeserializationError err = deserializeJson(Local_doc, uf);
  uf.close();
  if (err)
  {
    Serial.print("DeserializationError ====");
    Serial.println(err.c_str());
    return;
  }

  const char *username = Local_doc["username"];
  const char *password = Local_doc["password"];

  Serial.println("USER DETAILS FROM SD");
  Serial.println(username);
  Serial.println(password);

  val = AuthValidateCharacteristic.value();
  err = deserializeJson(APP_doc, val.c_str());

  if (err)
  {
    Serial.print("DeserializationError ====");
    Serial.println(err.c_str());
  }

  const char *username_from_app = APP_doc["username"];
  const char *password_from_app = APP_doc["password"];

  Serial.println("VALUE FROM APP ====");
  Serial.println(username_from_app);
  Serial.println(password_from_app);

  if (strcmp(username, username_from_app) == 0)
  {
    if (strcmp(password_from_app, password) == 0)
    {
      Serial.print("LOGIN CORRECT");
      AuthValidateCharacteristic.writeValue("true");
      auth_data.authenticated = true;
      return;
    }
    else
    {
      AuthValidateCharacteristic.writeValue("password");
    }
  }

  Serial.println("LOGIN FALSE");
  AuthValidateCharacteristic.writeValue("username");
  auth_data.authenticated = false;
}

void setup()
{
  auth_data.authenticated = false;

  Serial.begin(9600);
  while (!Serial)
    ;

  pinMode(ledPin, OUTPUT); // use the LED pin as an output

  Serial.print("Initializing SD card...");

  if (!SD.begin(4))
  {
    Serial.println("initialization failed!");
    while (1)
      ;
  }
  Serial.println("initialization done.");

  // begin initialization
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
  //BLE.setAdvertisedService(ledService);
  BLE.setAdvertisedService(AuthService);

  // add the characteristic to the service

  //ledService.addCharacteristic(switchCharacteristic);
  AuthService.addCharacteristic(AuthRegisterCharacteristic);
  AuthService.addCharacteristic(AuthCodeCharacteristic);
  AuthService.addCharacteristic(AuthInitCharacteristic);
  AuthService.addCharacteristic(AuthUserDataCharacteristic);
  AuthService.addCharacteristic(AuthValidateCharacteristic);

  //add service
  //BLE.addService(ledService);
  BLE.addService(AuthService);

  // assign event handlers for connected, disconnected to peripheral
  BLE.setEventHandler(BLEConnected, AuthHandler);
  BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);

  // assign event handlers for characteristic
  relayCharacteristic.setEventHandler(BLEWritten, switchCharacteristicWritten);
  AuthValidateCharacteristic.setEventHandler(BLEWritten, userDataWritten);
  // set an initial value for the characteristic
  relayCharacteristic.setValue(0);

  AuthInitCharacteristic.setValue("null");
  AuthRegisterCharacteristic.setValue("null");
  // start advertising
  BLE.advertise();

  Serial.println(("Bluetooth device active, waiting for connections..."));
}

void loop()
{
  // poll for BLE events
  BLE.poll();
}

// SD.remove(AUTH_FILE_NAME);
// SD.remove(USER_FILE_NAME);

// auth_file = SD.open(AUTH_FILE_NAME,FILE_WRITE);
// if (auth_file)
// {
//   auth_file.write("{\"registerd\":false,\"authenticated\":false,\"init_code\":\"12345678\"}");
// }
// else
// {
//   Serial.print("Would able to open AUTH_FILE");
// }
// auth_file.close();

// user_file = SD.open(USER_FILE_NAME,FILE_WRITE);

// if (user_file)
// {
//   user_file.write("{\"username\":\"init_user\",\"password\":\"init_pass\",\"mat_code\":\"28916d26-a0c7-42c4-b45c-0069ed7c37fc\"}");
//   user_file.close();
// }
// else
// {
//   Serial.print("Would able to open user_file");
// }
// user_file.close();
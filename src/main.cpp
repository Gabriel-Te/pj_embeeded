#include <Arduino.h>
#include <WiFi.h>

// put function declarations here:
int myFunction(int, int);
void initWifi(char *, char *, unsigned long);
int result = 0;
char *ssid = "";
char *pw = "";
unsigned long TIMEOUT_CONNECTION = 10000;

void setup()
{
  Serial.begin(115200);
  delay(2000); // Aguarda 1 segundo entre cada envio

  unsigned long startedWifiFunc = millis();
  initWifi(ssid, pw, startedWifiFunc);
  result = myFunction(2, 3);
}

void loop()
{
  // put your main code here, to run repeatedly:

  delay(1000); // Aguarda 1 segundo entre cada envio
}

// put function definitions here:
int myFunction(int x, int y)
{
  return x + y;
}

void initWifi(char *ssid, char *pw, unsigned long startedWifiFunc)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pw);
  Serial.print("Conectando ao wifi");

  while (WiFi.status() != WL_CONNECTED && (millis() - startedWifiFunc < TIMEOUT_CONNECTION))
  {
    Serial.print('.');
    delay(1000);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("\nConexao mal-sucedida, delay de ");
    Serial.print(TIMEOUT_CONNECTION / 1000);
    Serial.println("s");
  }
  else
  {
    Serial.print("\nWiFi conectado! IP = ");
    Serial.println(WiFi.localIP());
  }
}
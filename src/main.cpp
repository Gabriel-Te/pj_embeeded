#include <Arduino.h>
#include <WiFi.h>

// put function declarations here:
int myFunction(int, int);
void initWifi(char *, char *, unsigned long);
void quandoDispositivoConectar(WiFiEvent_t, WiFiEventInfo_t);

int result = 0;
char *ssid = "";
char *pw = "";

const char* ap_ssid = "Telemetry_AP";
const char* ap_password = "telemetria"; // Mínimo 8 caracteres

unsigned long TIMEOUT_CONNECTION = 10000;


void setup()
{
  Serial.begin(115200);
  delay(2000); // Aguarda 1 segundo entre cada envio

// Configura o ESP32 como Ponto de Acesso
    WiFi.softAP(ap_ssid, ap_password);

    WiFi.onEvent(quandoDispositivoConectar, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

    Serial.println("Ponto de Acesso Wi-Fi Iniciado!");
    Serial.print("Conecte-se na rede: ");
    Serial.println(ap_ssid);
    Serial.print("Acesse o IP do servidor: ");
    Serial.println(WiFi.softAPIP()); // O IP padrão costuma ser 192.168.4.1

    
  // unsigned long startedWifiFunc = millis();
  // initWifi(ssid, pw, startedWifiFunc);
  // result = myFunction(2, 3);
}

void loop()
{
  // put your main code here, to run repeatedly:
  Serial.print("Dispositivos conectados à rede: ");
    Serial.println(WiFi.softAPgetStationNum());

    delay(2000);
}

// put function definitions here:

void quandoDispositivoConectar(WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.print("Novo dispositivo ligado! IP atribuido: ");
    Serial.println(IPAddress(info.wifi_ap_staipassigned.ip.addr));
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
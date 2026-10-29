#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// =========================
// Configuração Wi-Fi
// =========================

const char* WIFI_SSID = "Nome da rede";
const char* WIFI_PASSWORD = "senha";

// IP do notebook onde o backend .NET está executando.
// NÃO use localhost aqui.
const char* BACKEND_HOST = "192.168.18.18";

const uint16_t BACKEND_PORT = 5000;
const char* BACKEND_PATH = "/ws/device";

// =========================
// Objetos e estado
// =========================

WebSocketsClient webSocket;

bool webSocketConnected = false;
bool subscriptionReceived = false;

unsigned long lastReadingTime = 0;
const unsigned long readingIntervalMs = 100;


void connectToWiFi();

void webSocketEvent(
    WStype_t type,
    uint8_t* payload,
    size_t length
);

void sendSupportedPids();

void handleBackendMessage(
    uint8_t* payload,
    size_t length
);

void sendRpmReading();
// =========================
// Configuração inicial
// =========================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    randomSeed(analogRead(0));

    connectToWiFi();

    webSocket.begin(
        BACKEND_HOST,
        BACKEND_PORT,
        BACKEND_PATH
    );

    webSocket.onEvent(webSocketEvent);

    // Tenta reconectar automaticamente a cada 5 segundos.
    webSocket.setReconnectInterval(5000);

    Serial.println("Cliente WebSocket configurado.");
}

// =========================
// Loop principal
// =========================

void loop()
{
    // Necessário para manter a conexão WebSocket funcionando.
    webSocket.loop();

    if (!webSocketConnected || !subscriptionReceived)
    {
        return;
    }

    unsigned long now = millis();

    if (now - lastReadingTime >= readingIntervalMs)
    {
        lastReadingTime = now;
        sendRpmReading();
    }
}

// =========================
// Wi-Fi
// =========================

void connectToWiFi()
{
    Serial.print("Conectando ao Wi-Fi");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Wi-Fi conectado.");
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
}

// =========================
// Eventos do WebSocket
// =========================

void webSocketEvent(
    WStype_t type,
    uint8_t* payload,
    size_t length)
{
    switch (type)
    {
        case WStype_DISCONNECTED:
            webSocketConnected = false;
            subscriptionReceived = false;

            Serial.println("WebSocket desconectado.");
            break;

        case WStype_CONNECTED:
            webSocketConnected = true;
            subscriptionReceived = false;

            Serial.println("WebSocket conectado ao backend.");

            sendSupportedPids();
            break;

        case WStype_TEXT:
            handleBackendMessage(payload, length);
            break;

        default:
            break;
    }
}

// =========================
// supportedPids
// =========================

void sendSupportedPids()
{
    JsonDocument document;

    document["Type"] = "supportedPids";
    document["DeviceId"] = "esp32-real-wifi";

    JsonArray pids = document["Pids"].to<JsonArray>();

    pids.add("0C");

    String json;
    serializeJson(document, json);

    webSocket.sendTXT(json);

    Serial.print("Enviado supportedPids: ");
    Serial.println(json);
}

// =========================
// Mensagens do backend
// =========================

void handleBackendMessage(uint8_t* payload, size_t length)
{
    String message;

    for (size_t i = 0; i < length; i++)
    {
        message += static_cast<char>(payload[i]);
    }

    Serial.print("Mensagem recebida do backend: ");
    Serial.println(message);

    JsonDocument document;
    DeserializationError error = deserializeJson(document, message);

    if (error)
    {
        Serial.print("Erro ao interpretar JSON: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = document["type"];

    if (type != nullptr && strcmp(type, "subscribe") == 0)
    {
        subscriptionReceived = true;

        Serial.println("Assinatura recebida.");
        Serial.println("Iniciando envio das leituras...");
    }
}

// =========================
// rawReading do PID 0C
// =========================

void sendRpmReading()
{
    // Valor físico que queremos simular.
    int rpm = random(800, 4001);

    // O backend calcula:
    //
    // RPM = (256 * A + B) / 4
    //
    // Portanto, os bytes crus precisam representar RPM * 4.
    int rawValue = rpm * 4;

    char dataHex[5];
    snprintf(
        dataHex,
        sizeof(dataHex),
        "%04X",
        rawValue
    );

    JsonDocument document;

    document["Type"] = "rawReading";
    document["DeviceId"] = "esp32-real-wifi";
    document["Pid"] = "0C";
    document["Data"] = dataHex;
    document["Timestamp"] = millis();

    String json;
    serializeJson(document, json);

    webSocket.sendTXT(json);

    Serial.print("Enviado rawReading: ");
    Serial.print(json);
    Serial.print(" | RPM simulado: ");
    Serial.println(rpm);
}
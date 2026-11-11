#include <Arduino.h>
#include <WiFi.h>
#include <BluetoothSerial.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

const char* AP_SSID = "Telemetry_AP";
const char* AP_PASSWORD = "telemetria";

const char* DEVICE_ID = "esp32-carro-01";

IPAddress BACKEND_IP(192, 168, 4, 2);
const uint16_t BACKEND_PORT = 5000;
const char* BACKEND_PATH = "/ws/device";

uint8_t ELM327_ADDRESS[6] =
{
    0x00,
    0x10,
    0xCC,
    0x4F,
    0x36,
    0x03
};

const char* ELM327_PIN = "1234";

BluetoothSerial ElmBluetooth;
WebSocketsClient BackendSocket;

bool elmConnected = false;
bool bluetoothStarted = false;
bool backendConnected = false;
bool subscriptionReceived = false;

unsigned long lastElmReconnectAttempt = 0;
unsigned long lastPoll = 0;
unsigned long subscriptionPeriodMs = 200;

struct Subscription
{
    uint8_t pids[32];
    size_t count = 0;
};

Subscription currentSubscription;

const uint8_t PRIORITY_PIDS[] =
{
    0x0C, // RPM
    0x0D, // Velocidade
    0x11, // TPS
    0x0B, // MAP
    0x04, // Carga calculada
    0x05, // Temperatura do líquido
    0x0F, // Temperatura do ar
    0x10, // MAF
    0x0E, // Avanço
    0x1F, // Temperatura do combustível
    0x2F, // Nível de combustível
    0x46  // Temperatura ambiente
};

const size_t PRIORITY_PID_COUNT =
    sizeof(PRIORITY_PIDS) / sizeof(PRIORITY_PIDS[0]);

String pidToHex(uint8_t pid)
{
    char buffer[3];
    snprintf(buffer, sizeof(buffer), "%02X", pid);
    return String(buffer);
}

String bytesToHex(const uint8_t* bytes, size_t count)
{
    String result;

    for (size_t i = 0; i < count; i++)
    {
        if (bytes[i] < 0x10)
        {
            result += "0";
        }

        result += String(bytes[i], HEX);
    }

    result.toUpperCase();
    return result;
}

void clearSubscription()
{
    currentSubscription.count = 0;
    subscriptionReceived = false;
}

bool subscriptionContains(uint8_t pid)
{
    for (size_t i = 0; i < currentSubscription.count; i++)
    {
        if (currentSubscription.pids[i] == pid)
        {
            return true;
        }
    }

    return false;
}

void addSubscriptionPid(uint8_t pid)
{
    if (currentSubscription.count >= 32)
    {
        return;
    }

    if (!subscriptionContains(pid))
    {
        currentSubscription.pids[currentSubscription.count++] = pid;
    }
}

void sortSubscriptionByPriority()
{
    Subscription sorted;

    for (size_t i = 0; i < PRIORITY_PID_COUNT; i++)
    {
        uint8_t priorityPid = PRIORITY_PIDS[i];

        if (subscriptionContains(priorityPid))
        {
            sorted.pids[sorted.count++] = priorityPid;
        }
    }

    for (size_t i = 0; i < currentSubscription.count; i++)
    {
        uint8_t pid = currentSubscription.pids[i];
        bool alreadyAdded = false;

        for (size_t j = 0; j < sorted.count; j++)
        {
            if (sorted.pids[j] == pid)
            {
                alreadyAdded = true;
                break;
            }
        }

        if (!alreadyAdded && sorted.count < 32)
        {
            sorted.pids[sorted.count++] = pid;
        }
    }

    currentSubscription = sorted;
}

bool responseHasNoData(const String& response)
{
    String normalized = response;
    normalized.toUpperCase();

    return normalized.indexOf("NO DATA") >= 0 ||
           normalized.indexOf("ERROR") >= 0 ||
           normalized.indexOf("UNABLE") >= 0;
}

bool readElmResponse(
    String& response,
    unsigned long timeoutMs = 2500)
{
    response = "";

    unsigned long startedAt = millis();

    while (millis() - startedAt < timeoutMs)
    {
        while (ElmBluetooth.available())
        {
            char received =
                static_cast<char>(ElmBluetooth.read());

            response += received;

            if (response.indexOf('>') >= 0)
            {
                response.replace(">", "");
                response.trim();

                return true;
            }
        }

        delay(1);
    }

    response.trim();
    return response.length() > 0;
}

bool sendElmCommand(
    const String& command,
    String& response)
{
    if (!elmConnected)
    {
        Serial.println(
            "Comando não enviado: ELM desconectado.");

        return false;
    }

    while (ElmBluetooth.available())
    {
        ElmBluetooth.read();
    }

    Serial.print("ELM <- ");
    Serial.println(command);

    ElmBluetooth.print(command);
    ElmBluetooth.print("\r");

    bool received = readElmResponse(response);

    Serial.print("ELM -> ");
    Serial.println(response);

    return received;
}

bool initializeElm327()
{
    Serial.println("Inicializando ELM327...");

    String response;

    if (!sendElmCommand("ATZ", response))
    {
        Serial.println("ELM327 não respondeu ao ATZ.");
        return false;
    }

    delay(1500);

    const char* commands[] =
    {
        "ATE0",
        "ATS1",
        "ATH0",
        "ATSP0"
    };

    for (const char* command : commands)
    {
        if (!sendElmCommand(command, response))
        {
            Serial.print("Falha no comando AT: ");
            Serial.println(command);

            return false;
        }

        if (responseHasNoData(response))
        {
            Serial.print("Resposta de erro para ");
            Serial.print(command);
            Serial.print(": ");
            Serial.println(response);

            return false;
        }
    }

    Serial.println("ELM327 inicializado.");
    return true;
}

bool connectToElm327()
{
    if (elmConnected)
    {
        return true;
    }

    Serial.println(
        "Conectando ao ELM327 por Bluetooth Classic...");

    Serial.print("MAC: ");

    for (int i = 0; i < 6; i++)
    {
        if (ELM327_ADDRESS[i] < 0x10)
        {
            Serial.print("0");
        }

        Serial.print(
            ELM327_ADDRESS[i],
            HEX);

        if (i < 5)
        {
            Serial.print(":");
        }
    }

    Serial.println();

    bool connected =
        ElmBluetooth.connect(ELM327_ADDRESS);

    Serial.println(
        "Tentativa Bluetooth finalizada.");

    if (!connected)
    {
        Serial.println(
            "Não foi possível conectar ao ELM327.");

        return false;
    }

    Serial.println(
        "Conexão Bluetooth estabelecida.");

    // Deve ser true antes dos comandos AT.
    elmConnected = true;

    delay(1000);

    if (!initializeElm327())
    {
        Serial.println(
            "Falha ao inicializar o ELM327.");

        elmConnected = false;
        ElmBluetooth.disconnect();

        return false;
    }

    Serial.println(
        "Bluetooth com ELM327 conectado e inicializado.");

    return true;
}

bool parseObdResponse(
    const String& response,
    uint8_t requestedPid,
    uint8_t* data,
    size_t expectedBytes)
{
    if (responseHasNoData(response))
    {
        return false;
    }

    String normalized = response;

    normalized.toUpperCase();
    normalized.replace("\r", " ");
    normalized.replace("\n", " ");
    normalized.replace(">", " ");

    int responseIndex =
        normalized.indexOf("41");

    if (responseIndex < 0)
    {
        return false;
    }

    String payload =
        normalized.substring(responseIndex);

    char buffer[160];
    payload.toCharArray(buffer, sizeof(buffer));

    char* token = strtok(buffer, " \t");

    uint8_t values[16];
    size_t valueCount = 0;

    while (token != nullptr && valueCount < 16)
    {
        char* end = nullptr;
        long value = strtol(token, &end, 16);

        if (end != token &&
            *end == '\0' &&
            value >= 0 &&
            value <= 255)
        {
            values[valueCount++] =
                static_cast<uint8_t>(value);
        }

        token = strtok(nullptr, " \t");
    }

    if (valueCount < 3)
    {
        return false;
    }

    if (values[0] != 0x41 ||
        values[1] != requestedPid)
    {
        return false;
    }

    if (valueCount - 2 < expectedBytes)
    {
        return false;
    }

    for (size_t i = 0; i < expectedBytes; i++)
    {
        data[i] = values[i + 2];
    }

    return true;
}

bool readPidFromElm327(
    uint8_t pid,
    String& rawData)
{
    size_t expectedBytes =
        pid == 0x0C || pid == 0x10 ? 2 : 1;

    String command = "01" + pidToHex(pid);
    String response;

    if (!sendElmCommand(command, response))
    {
        return false;
    }

    uint8_t data[4];

    if (!parseObdResponse(
            response,
            pid,
            data,
            expectedBytes))
    {
        Serial.print(
            "Resposta inválida para PID ");

        Serial.println(pidToHex(pid));
        return false;
    }

    rawData =
        bytesToHex(data, expectedBytes);

    return true;
}

bool isPidSupportedInMask(
    const uint8_t* mask,
    uint8_t maskBytes,
    uint8_t pidOffset)
{
    if (pidOffset == 0 ||
        pidOffset > maskBytes * 8)
    {
        return false;
    }

    uint8_t zeroBasedIndex = pidOffset - 1;
    uint8_t byteIndex = zeroBasedIndex / 8;
    uint8_t bitIndex = 7 - (zeroBasedIndex % 8);

    return (mask[byteIndex] &
            (1 << bitIndex)) != 0;
}

bool readSupportedPidBlock(
    uint8_t blockStart,
    uint8_t* supportedMask)
{
    String command = "01" + pidToHex(blockStart);
    String response;

    Serial.print(
        "Consultando bloco de PIDs: ");

    Serial.println(command);

    if (!sendElmCommand(command, response))
    {
        Serial.println(
            "Sem resposta para o bloco de PIDs.");

        return false;
    }

    Serial.print(
        "Resposta bruta do bloco ");

    Serial.print(command);
    Serial.print(": ");
    Serial.println(response);

    if (responseHasNoData(response))
    {
        Serial.println(
            "ELM retornou NO DATA ou ERROR.");

        return false;
    }

    uint8_t values[4];

    if (!parseObdResponse(
            response,
            blockStart,
            values,
            4))
    {
        Serial.println(
            "Não foi possível interpretar a máscara de PIDs.");

        return false;
    }

    for (size_t i = 0; i < 4; i++)
    {
        supportedMask[i] = values[i];
    }

    Serial.print("Máscara recebida: ");

    for (size_t i = 0; i < 4; i++)
    {
        Serial.print(
            pidToHex(supportedMask[i]));

        if (i < 3)
        {
            Serial.print(" ");
        }
    }

    Serial.println();

    return true;
}

void discoverSupportedPids()
{
    Serial.println(
        "Descobrindo PIDs suportados...");

    uint8_t discoveredPids[96];
    size_t discoveredCount = 0;

    for (uint8_t blockStart = 0x00;
         blockStart <= 0x60;
         blockStart += 0x20)
    {
        uint8_t mask[4] =
        {
            0,
            0,
            0,
            0
        };

        if (!readSupportedPidBlock(
                blockStart,
                mask))
        {
            Serial.print(
                "Falha no bloco 01");

            Serial.println(
                pidToHex(blockStart));

            break;
        }

        for (uint8_t offset = 1;
             offset <= 0x20;
             offset++)
        {
            uint8_t pid =
                blockStart + offset;

            if (pid == 0 || pid > 0x60)
            {
                continue;
            }

            if (isPidSupportedInMask(
                    mask,
                    4,
                    offset))
            {
                if (discoveredCount <
                    sizeof(discoveredPids))
                {
                    discoveredPids[
                        discoveredCount++] = pid;
                }
            }
        }

        // O último bit indica o próximo bloco.
        if ((mask[3] & 0x01) == 0)
        {
            break;
        }
    }

    clearSubscription();

    // Mantém apenas PIDs conhecidos pelo backend.
    for (size_t i = 0;
         i < PRIORITY_PID_COUNT;
         i++)
    {
        uint8_t priorityPid =
            PRIORITY_PIDS[i];

        for (size_t j = 0;
             j < discoveredCount;
             j++)
        {
            if (discoveredPids[j] ==
                priorityPid)
            {
                addSubscriptionPid(priorityPid);
                break;
            }
        }
    }

    sortSubscriptionByPriority();

    Serial.print(
        "Quantidade de PIDs descobertos: ");

    Serial.println(
        currentSubscription.count);

    Serial.println(
        "PIDs suportados e conhecidos pelo backend:");

    for (size_t i = 0;
         i < currentSubscription.count;
         i++)
    {
        Serial.println(
            pidToHex(
                currentSubscription.pids[i]));
    }
}

void sendHello()
{
    JsonDocument document;

    document["Type"] = "hello";
    document["DeviceId"] = DEVICE_ID;
    document["Adapter"] = "ELM327_BT";
    document["Protocol"] = "OBD-II";

    String json;
    serializeJson(document, json);

    if (!BackendSocket.sendTXT(json))
    {
        Serial.println("Falha ao enviar hello pelo WebSocket.");
        return;
    }

    Serial.print("Hello enviado: ");
    Serial.println(json);
}

void sendSupportedPids()
{
    Serial.print(
        "Quantidade de PIDs para envio: ");

    Serial.println(
        currentSubscription.count);

    if (!backendConnected)
    {
        Serial.println(
            "supportedPids não enviado: backend desconectado.");

        return;
    }

    if (currentSubscription.count == 0)
    {
        Serial.println(
            "supportedPids não enviado: nenhum PID descoberto.");

        return;
    }

    JsonDocument document;

    document["Type"] = "supportedPids";
    document["DeviceId"] = DEVICE_ID;

    JsonArray pids =
        document["Pids"].to<JsonArray>();

    for (size_t i = 0;
         i < currentSubscription.count;
         i++)
    {
        pids.add(
            pidToHex(
                currentSubscription.pids[i]));
    }

    String json;
    serializeJson(document, json);

    if (!BackendSocket.sendTXT(json))
    {
        Serial.println("Falha ao enviar supportedPids pelo WebSocket.");
        return;
    }

    Serial.print(
        "supportedPids enviado: ");

    Serial.println(json);
}

void sendRawReading(
    uint8_t pid,
    const String& rawData)
{
    if (!backendConnected)
    {
        return;
    }

    JsonDocument document;

    document["Type"] = "rawReading";
    document["DeviceId"] = DEVICE_ID;
    document["Pid"] = pidToHex(pid);
    document["Data"] = rawData;
    document["Timestamp"] = millis();

    String json;
    serializeJson(document, json);

    if (!BackendSocket.sendTXT(json))
    {
        Serial.println("Falha ao enviar rawReading pelo WebSocket.");
        return;
    }

    Serial.print(
        "rawReading enviado: ");

    Serial.println(json);
}

void processSubscribeMessage(
    const String& json)
{
    JsonDocument document;

    DeserializationError error =
        deserializeJson(document, json);

    if (error)
    {
        Serial.print(
            "Erro no subscribe: ");

        Serial.println(error.c_str());
        return;
    }

    if (document["type"] != "subscribe")
    {
        Serial.println(
            "Mensagem recebida não é subscribe.");

        return;
    }

    JsonArray groups =
        document["groups"].as<JsonArray>();

    if (groups.isNull())
    {
        Serial.println(
            "Subscribe sem groups.");

        return;
    }

    clearSubscription();

    for (JsonObject group : groups)
    {
        if (group["periodMs"].is<unsigned long>())
        {
            subscriptionPeriodMs =
                group["periodMs"]
                    .as<unsigned long>();
        }

        JsonArray pids =
            group["pids"].as<JsonArray>();

        if (pids.isNull())
        {
            continue;
        }

        for (JsonVariant pidValue : pids)
        {
            const char* pidText =
                pidValue.as<const char*>();

            if (pidText == nullptr)
            {
                continue;
            }

            char* end = nullptr;

            long pid =
                strtol(pidText, &end, 16);

            if (end != pidText &&
                *end == '\0' &&
                pid >= 1 &&
                pid <= 0xFF)
            {
                addSubscriptionPid(
                    static_cast<uint8_t>(pid));
            }
        }
    }

    sortSubscriptionByPriority();

    subscriptionReceived =
        currentSubscription.count > 0;

    Serial.print(
        "Subscribe recebido. PIDs: ");

    for (size_t i = 0;
         i < currentSubscription.count;
         i++)
    {
        Serial.print(
            pidToHex(
                currentSubscription.pids[i]));

        if (i + 1 <
            currentSubscription.count)
        {
            Serial.print(", ");
        }
    }

    Serial.print(
        " | Período: ");

    Serial.print(subscriptionPeriodMs);
    Serial.println(" ms");
}

void backendWebSocketEvent(
    WStype_t type,
    uint8_t* payload,
    size_t length)
{
    switch (type)
    {
        case WStype_DISCONNECTED:
            backendConnected = false;
            subscriptionReceived = false;

            Serial.println(
                "WebSocket com backend desconectado.");

            break;

        case WStype_CONNECTED:
            backendConnected = true;

            Serial.println(
                "WebSocket com backend conectado.");

            sendHello();
            sendSupportedPids();

            break;

        case WStype_TEXT:
        {
            String json;

            for (size_t i = 0;
                 i < length;
                 i++)
            {
                json +=
                    static_cast<char>(
                        payload[i]);
            }

            Serial.print(
                "Mensagem recebida do backend: ");

            Serial.println(json);

            processSubscribeMessage(json);

            break;
        }

        case WStype_ERROR:
            Serial.println(
                "Erro no WebSocket com o backend.");

            break;

        default:
            break;
    }
}

void connectToBackend()
{
    BackendSocket.onEvent(
        backendWebSocketEvent);

    BackendSocket.begin(
        BACKEND_IP,
        BACKEND_PORT,
        BACKEND_PATH);

    BackendSocket.setReconnectInterval(5000);

    Serial.print(
        "WebSocket configurado para ");

    Serial.print(BACKEND_IP);
    Serial.print(":");
    Serial.println(BACKEND_PORT);
}

void startAccessPoint()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(
        AP_SSID,
        AP_PASSWORD);

    Serial.println(
        "Access Point iniciado.");

    Serial.print("SSID: ");
    Serial.println(AP_SSID);

    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.softAPIP());
}

bool startBluetooth()
{
    // true = modo Master.
    if (!ElmBluetooth.begin(
            "ESP32_OBD",
            true))
    {
        Serial.println(
            "Falha ao iniciar Bluetooth Classic.");

        return false;
    }

    if (!ElmBluetooth.setPin(
            ELM327_PIN))
    {
        Serial.println(
            "Falha ao configurar PIN.");

        return false;
    }

    Serial.println(
        "Bluetooth Classic iniciado em modo Master.");

    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    startAccessPoint();

    bluetoothStarted = startBluetooth();

    if (bluetoothStarted && connectToElm327())
    {
        discoverSupportedPids();
    }
    else
    {
        Serial.println(
            "ELM327 ainda não conectado.");
    }

    connectToBackend();
}

void loop()
{
    BackendSocket.loop();

    if (!elmConnected &&
        millis() - lastElmReconnectAttempt >= 5000)
    {
        lastElmReconnectAttempt = millis();

        if (!bluetoothStarted)
        {
            bluetoothStarted = startBluetooth();
        }

        if (bluetoothStarted)
        {
            elmConnected = connectToElm327();

            if (elmConnected)
            {
                discoverSupportedPids();

                if (backendConnected)
                {
                    sendSupportedPids();
                }
            }
        }
    }

    if (backendConnected &&
        elmConnected &&
        subscriptionReceived &&
        currentSubscription.count > 0 &&
        millis() - lastPoll >= subscriptionPeriodMs)
    {
        lastPoll = millis();

        // A lista está ordenada por prioridade.
        for (size_t i = 0;
             i < currentSubscription.count;
             i++)
        {
            uint8_t pid =
                currentSubscription.pids[i];

            String rawData;

            if (readPidFromElm327(
                    pid,
                    rawData))
            {
                sendRawReading(
                    pid,
                    rawData);
            }
            else
            {
                Serial.print(
                    "Sem resposta para PID ");

                Serial.println(
                    pidToHex(pid));
            }
        }
    }

    if (WiFi.softAPgetStationNum() == 0)
    {
        static unsigned long lastWarning = 0;

        if (millis() - lastWarning >= 5000)
        {
            lastWarning = millis();

            Serial.println(
                "Nenhum notebook conectado ao Access Point.");
        }
    }

    delay(1);
}
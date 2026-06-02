// =============================================================
// PULSO URBANO — Firmware ESP32
// Disciplina: Disruptive Architectures: IoT, IoB & Generative AI
// Owner: Brisola · GS 2026/1 · FIAP 2TDS
//
// O satélite Sentinel-5P mede NO₂ com resolução de 3.5km.
// Este ESP32 preenche a lacuna: mede ar e temperatura no ponto
// exato, publica via MQTT, e o backend Java cruza os dois dados.
// =============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHTesp.h>
#include <LiquidCrystal_I2C.h>

// ----------------------------------------------------------
// CONFIGURAÇÕES — edite aqui antes de flashar
// ----------------------------------------------------------
#define WIFI_SSID     "SUA_REDE_WIFI"
#define WIFI_PASSWORD "SUA_SENHA_WIFI"

#define MQTT_BROKER   "broker.hivemq.com"
#define MQTT_PORT     1883
#define MQTT_CLIENT_ID "ESP32-PULSO-01"

// Identificação da zona monitorada
#define ZONA_ID    1
#define ZONA_NOME  "Centro SP"

// Intervalos de tempo (milissegundos)
#define INTERVALO_LEITURA 10000UL   // 10s — leitura + publicação telemetria
#define INTERVALO_STATUS  60000UL   // 60s — publicação status do dispositivo

// ----------------------------------------------------------
// PINAGEM
// ----------------------------------------------------------
#define PIN_DHT22    4
#define PIN_MQ135   34   // ADC — entrada analógica (0–4095)

#define PIN_LED_VERDE   27
#define PIN_LED_AMARELO 26
#define PIN_LED_VERMELHO 25
#define PIN_BUZZER      32

// LCD 16x2 I2C — endereço padrão 0x27, SDA=21, SCL=22
#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// ----------------------------------------------------------
// TÓPICOS MQTT
// ----------------------------------------------------------
#define TOPICO_TELEMETRIA "pulso/sp/zona-centro/telemetria"
#define TOPICO_ALERTA     "pulso/sp/zona-centro/alerta"
#define TOPICO_STATUS     "pulso/sp/zona-centro/status"

// ----------------------------------------------------------
// OBJETOS GLOBAIS
// ----------------------------------------------------------
WiFiClient       wifiClient;
PubSubClient     mqttClient(wifiClient);
WebServer        server(80);
DHTesp           dht;
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ----------------------------------------------------------
// ESTADO GLOBAL — snapshot da última leitura
// ----------------------------------------------------------
struct LeituraAtual {
  float temperatura  = 0.0;
  float umidade      = 0.0;
  int   mq135Raw     = 0;
  float scoreLocal   = 0.0;
  String classificacao = "AGUARDANDO";
  bool  dht22Ok      = false;
  bool  mq135Ok      = false;
} leitura;

// Controle de tempo sem delay()
unsigned long ultimaLeitura = 0;
unsigned long ultimoStatus  = 0;
unsigned long inicioMs      = 0;   // para calcular uptime

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  inicioMs = millis();

  // Pinos de saída
  pinMode(PIN_LED_VERDE,    OUTPUT);
  pinMode(PIN_LED_AMARELO,  OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER,       OUTPUT);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Pulso Urbano");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");

  // DHT22
  dht.setup(PIN_DHT22, DHTesp::DHT22);

  // Wi-Fi
  conectarWifi();

  // MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);

  // REST API
  configurarEndpoints();
  server.begin();

  lcd.clear();
  lcd.print("Sistema OK");
}

// =============================================================
// LOOP — sem delay(); tudo via millis()
// =============================================================
void loop() {
  // Mantém conexões ativas
  if (WiFi.status() != WL_CONNECTED) {
    conectarWifi();
  }
  if (!mqttClient.connected()) {
    reconectarMQTT();
  }
  mqttClient.loop();

  // Mantém a REST API atendendo requisições
  server.handleClient();

  unsigned long agora = millis();

  // --- Ciclo de leitura (10s) ---
  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    executarCicloLeitura();
  }

  // --- Publicação de status (60s) ---
  if (agora - ultimoStatus >= INTERVALO_STATUS) {
    ultimoStatus = agora;
    publicarStatus();
  }
}

// =============================================================
// CONEXÃO WI-FI
// =============================================================
void conectarWifi() {
  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Tentativas limitadas para não bloquear indefinidamente
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);   // delay() só aqui, fora do loop() principal
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWi-Fi conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha no Wi-Fi — continuando sem rede.");
  }
}

// =============================================================
// RECONEXÃO MQTT com backoff de 5s
// =============================================================
void reconectarMQTT() {
  if (mqttClient.connected()) return;

  Serial.print("Reconectando MQTT ao HiveMQ Cloud...");
  // Tenta uma vez; se falhar, aguarda 5s e retorna ao loop()
  // O backoff real acontece porque loop() só chama esta função
  // quando connected() == false, e o millis() já avançou 5s
  // antes da próxima chamada efetiva.
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println(" conectado.");
  } else {
    Serial.print(" falhou (rc=");
    Serial.print(mqttClient.state());
    Serial.println("). Próxima tentativa em 5s.");
    delay(5000);   // backoff — único delay() permitido fora do ciclo de leitura
  }
}

// =============================================================
// CICLO COMPLETO DE LEITURA — acionado a cada 10s
// =============================================================
void executarCicloLeitura() {
  lerSensores();

  leitura.scoreLocal    = calcularScore(leitura.temperatura, leitura.mq135Raw);
  leitura.classificacao = classificar(leitura.scoreLocal);

  atualizarLED(leitura.classificacao);
  atualizarBuzzer(leitura.classificacao);
  atualizarLCD(leitura.scoreLocal, leitura.classificacao);
  publicarTelemetria();

  // Alerta MQTT apenas quando condição crítica — dado local complementa
  // o dado orbital: se o satélite não enxerga a rua, o ESP32 enxerga.
  if (leitura.classificacao == "CRITICO") {
    publicarAlerta();
  }

  Serial.printf("[Leitura] Temp=%.1f°C  Umid=%.1f%%  MQ135=%d  Score=%.1f  Class=%s\n",
    leitura.temperatura, leitura.umidade, leitura.mq135Raw,
    leitura.scoreLocal, leitura.classificacao.c_str());
}

// =============================================================
// LEITURA DE SENSORES
// =============================================================
void lerSensores() {
  // DHT22
  TempAndHumidity dados = dht.getTempAndHumidity();
  leitura.dht22Ok = (dht.getStatus() == DHTesp::ERROR_NONE);

  if (leitura.dht22Ok) {
    leitura.temperatura = dados.temperature;
    leitura.umidade     = dados.humidity;
  } else {
    // Mantém último valor válido — não invalida o score por falha pontual
    Serial.println("[WARN] DHT22: leitura inválida.");
  }

  // MQ135 — leitura analógica raw 0-4095 (ADC 12 bits do ESP32)
  leitura.mq135Raw = analogRead(PIN_MQ135);
  leitura.mq135Ok  = (leitura.mq135Raw >= 0 && leitura.mq135Raw <= 4095);
}

// =============================================================
// ALGORITMO DE SCORE LOCAL
// Mesmo pesos do backend Java — consistência narrativa para
// que o dado local e o orbital sejam comparáveis.
//
// score_temp = max(0, 1 - max(0, (temp - 30) / 20)) * 40
// score_ar   = max(0, 1 - (mq135Raw / 4095.0))      * 60
// scoreLocal = score_temp + score_ar  → [0, 100]
// =============================================================
float calcularScore(float temp, int mq135Raw) {
  // Componente temperatura: penaliza acima de 30°C até zerar em 50°C
  float fatorTemp = max(0.0f, 1.0f - max(0.0f, (temp - 30.0f) / 20.0f));
  float scoreTemp = fatorTemp * 40.0f;

  // Componente qualidade do ar: MQ135 alto = ar ruim = score baixo
  float fatorAr  = max(0.0f, 1.0f - ((float)mq135Raw / 4095.0f));
  float scoreAr  = fatorAr * 60.0f;

  return scoreTemp + scoreAr;
}

// =============================================================
// CLASSIFICAÇÃO DO SCORE
// =============================================================
String classificar(float score) {
  if (score >= 80.0) return "BOM";
  if (score >= 60.0) return "MODERADO";
  if (score >= 40.0) return "RUIM";
  return "CRITICO";
}

// =============================================================
// SAÍDAS — LED, BUZZER, LCD
// =============================================================
void atualizarLED(String classificacao) {
  // Desliga todos antes de acender o correto
  digitalWrite(PIN_LED_VERDE,    LOW);
  digitalWrite(PIN_LED_AMARELO,  LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);

  if (classificacao == "BOM") {
    digitalWrite(PIN_LED_VERDE, HIGH);
  } else if (classificacao == "MODERADO") {
    digitalWrite(PIN_LED_AMARELO, HIGH);
  } else {
    // RUIM e CRITICO — LED vermelho
    digitalWrite(PIN_LED_VERMELHO, HIGH);
  }
}

void atualizarBuzzer(String classificacao) {
  if (classificacao != "CRITICO") {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  // 3 pulsos de 200ms — alerta sonoro local quando condição crítica
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
    delay(200);
  }
}

void atualizarLCD(float score, String classificacao) {
  lcd.clear();

  // Linha 1: score formatado
  lcd.setCursor(0, 0);
  lcd.print("Score: ");
  lcd.print(score, 1);

  // Linha 2: classificação (truncada em 16 chars)
  lcd.setCursor(0, 1);
  String linha2 = classificacao;
  if (linha2.length() > 16) linha2 = linha2.substring(0, 16);
  lcd.print(linha2);
}

// =============================================================
// PUBLICAÇÃO MQTT — TELEMETRIA (a cada 10s)
// =============================================================
void publicarTelemetria() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<256> doc;
  doc["zonaId"]        = ZONA_ID;
  doc["zonaNome"]      = ZONA_NOME;
  doc["temperatura"]   = leitura.temperatura;
  doc["umidade"]       = leitura.umidade;
  doc["mq135Raw"]      = leitura.mq135Raw;
  doc["nivelAr"]       = leitura.classificacao;
  doc["scoreLocal"]    = leitura.scoreLocal;
  doc["classificacao"] = leitura.classificacao;
  doc["ts"]            = (unsigned long)(millis() / 1000);

  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPICO_TELEMETRIA, buffer);
}

// =============================================================
// PUBLICAÇÃO MQTT — ALERTA (apenas quando CRÍTICO)
// Dados orbitais têm 3.5km de resolução — o ESP32 é o único
// que enxerga esse quarteirão. Quando crítico, o backend Java
// precisa saber imediatamente para atualizar o score do app.
// =============================================================
void publicarAlerta() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<256> doc;
  doc["zonaId"]      = ZONA_ID;
  doc["nivel"]       = "CRITICO";
  doc["scoreLocal"]  = leitura.scoreLocal;
  doc["temperatura"] = leitura.temperatura;
  doc["mq135Raw"]    = leitura.mq135Raw;
  doc["mensagem"]    = "Score critico detectado no sensor local";
  doc["ts"]          = (unsigned long)(millis() / 1000);

  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPICO_ALERTA, buffer);

  Serial.println("[ALERTA] Publicado topico alerta CRITICO.");
}

// =============================================================
// PUBLICAÇÃO MQTT — STATUS DO DISPOSITIVO (a cada 60s)
// =============================================================
void publicarStatus() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<256> doc;
  doc["deviceId"] = MQTT_CLIENT_ID;
  doc["zonaId"]   = ZONA_ID;
  doc["uptime"]   = (unsigned long)((millis() - inicioMs) / 1000);
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["ts"]       = (unsigned long)(millis() / 1000);

  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPICO_STATUS, buffer);
}

// =============================================================
// REST API — ENDPOINTS
// Expostos na rede local para inspeção direta sem depender do MQTT.
// =============================================================
void configurarEndpoints() {

  // GET /leitura — snapshot da última leitura dos sensores
  server.on("/leitura", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["zonaId"]        = ZONA_ID;
    doc["temperatura"]   = leitura.temperatura;
    doc["umidade"]       = leitura.umidade;
    doc["mq135Raw"]      = leitura.mq135Raw;
    doc["scoreLocal"]    = leitura.scoreLocal;
    doc["classificacao"] = leitura.classificacao;
    doc["ts"]            = (unsigned long)(millis() / 1000);

    String resposta;
    serializeJson(doc, resposta);
    server.send(200, "application/json", resposta);
  });

  // GET /status — saúde do dispositivo (uptime, Wi-Fi, heap)
  server.on("/status", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["deviceId"]     = MQTT_CLIENT_ID;
    doc["uptime"]       = (unsigned long)((millis() - inicioMs) / 1000);
    doc["mqttConectado"]= mqttClient.connected();
    doc["wifiSSID"]     = WIFI_SSID;
    doc["wifiRSSI"]     = WiFi.RSSI();
    doc["freeHeap"]     = ESP.getFreeHeap();

    String resposta;
    serializeJson(doc, resposta);
    server.send(200, "application/json", resposta);
  });

  // GET /health — verificação rápida dos subsistemas
  server.on("/health", HTTP_GET, []() {
    StaticJsonDocument<128> doc;
    doc["status"]      = "ok";
    doc["sensor_dht22"] = leitura.dht22Ok ? "ok" : "erro";
    doc["sensor_mq135"] = leitura.mq135Ok ? "ok" : "erro";
    doc["mqtt"]         = mqttClient.connected() ? "conectado" : "desconectado";

    String resposta;
    serializeJson(doc, resposta);
    server.send(200, "application/json", resposta);
  });

  // Rota padrão para URLs não mapeadas
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"erro\":\"rota nao encontrada\"}");
  });
}

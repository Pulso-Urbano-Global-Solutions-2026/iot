// =============================================================
// PULSO URBANO — Firmware ESP32 
// Disciplina: Disruptive Architectures: IoT, IoB & Generative AI
// Owner: Brisola · GS 2026/1 · FIAP 2TDS
// =============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>             
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// ----------------------------------------------------------
// CONFIGURAÇÕES
// ----------------------------------------------------------
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define MQTT_BROKER    "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_CLIENT_ID "ESP32-PULSO-01"

#define ZONA_ID   1
#define ZONA_NOME "Centro SP"

#define INTERVALO_LEITURA 10000UL
#define INTERVALO_STATUS  60000UL

// ----------------------------------------------------------
// PINAGEM
// ----------------------------------------------------------
#define PIN_DHT22        4
#define DHTTYPE      DHT22   
#define PIN_MQ135       34

#define PIN_LED_VERDE    27
#define PIN_LED_AMARELO  26
#define PIN_LED_VERMELHO 25
#define PIN_BUZZER       32

#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS  2

// ----------------------------------------------------------
// CALIBRACAO MQ135
// O MQ135 precisa ter o R0 (raw em "ar limpo") calibrado no momento
// da instalacao -- pratica padrao para sensores desse tipo, pois o
// baseline varia com ventilacao, gases residuais do ambiente e, no
// simulador, com a propria seed de cada execucao. Por isso o baseline
// e medido automaticamente no boot (ver calibrarMQ135() no setup),
// em vez de um valor fixo.
#define MQ135_AMOSTRAS_CALIBRACAO 5     // leituras usadas para calibrar no boot
#define MQ135_RANGE               1500.0f  // delta acima do baseline até score = 0

float mq135Baseline = 3000.0f; // valor padrao; sobrescrito por calibrarMQ135()

// ----------------------------------------------------------
// TÓPICOS MQTT
// ----------------------------------------------------------
#define TOPICO_TELEMETRIA "pulso/sp/zona-centro/telemetria"
#define TOPICO_ALERTA     "pulso/sp/zona-centro/alerta"
#define TOPICO_STATUS     "pulso/sp/zona-centro/status"

// ----------------------------------------------------------
// OBJETOS GLOBAIS
// ----------------------------------------------------------
WiFiClient        wifiClient;
PubSubClient      mqttClient(wifiClient);
WebServer         server(80);
DHT               dht(PIN_DHT22, DHTTYPE); 
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ----------------------------------------------------------
// ESTADO GLOBAL
// ----------------------------------------------------------
struct LeituraAtual {
  float  temperatura   = 0.0;
  float  umidade       = 0.0;
  int    mq135Raw      = 0;
  float  scoreLocal    = 0.0;
  String classificacao = "AGUARDANDO";
  bool   dht22Ok       = false;
  bool   mq135Ok       = false;
} leitura;

unsigned long ultimaLeitura = 0;
unsigned long ultimoStatus  = 0;
unsigned long inicioMs      = 0;

// ----------------------------------------------------------
// Buzzer não-bloqueante
// ----------------------------------------------------------
struct Buzzer {
  bool          ativo        = false;
  int           pulsosTotal  = 3;
  int           pulsoAtual   = 0;
  bool          emHigh       = false;
  unsigned long ultimoT      = 0;
  const unsigned long DURACAO = 200UL; 
} buzzerState;

void conectarWifi();
void reconectarMQTT();
void executarCicloLeitura();
void lerSensores();
void calibrarMQ135();
void verificarComandoSerial();
float calcularScore(float temp, int mq135Raw);
String classificar(float score);
void atualizarLED(String classificacao);
void acionarBuzzerSeNecessario(String classificacao);
void tickBuzzer();
void atualizarLCD(float score, String classificacao);
void publicarTelemetria();
void publicarAlerta();
void publicarStatus();
void configurarEndpoints();

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(2000);                   
  
  Serial.println("\n=============================================");
  Serial.println("   PULSO URBANO - INICIALIZANDO FIRMWARE     ");
  Serial.println("=============================================");

  Serial.println("[SETUP] Inicializando barramento I2C...");
  Wire.begin(21, 22); 

  Serial.println("[SETUP] Configurando pinos de atuacao (LEDs e Buzzer)...");
  pinMode(PIN_LED_VERDE,    OUTPUT);
  pinMode(PIN_LED_AMARELO,  OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER,       OUTPUT);

  Serial.println("[SETUP] Inicializando Display LCD I2C...");
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Pulso Urbano");
  lcd.setCursor(0, 1); lcd.print("Iniciando...");

  Serial.println("[SETUP] Inicializando Sensor DHT22...");
  dht.begin(); 

  conectarWifi();

  Serial.println("[SETUP] Configurando cliente MQTT HiveMQ...");
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);

  Serial.println("[SETUP] Configurando rotas da API REST Local...");
  configurarEndpoints();
  server.begin();

  Serial.println("[SETUP] Aguardando estabilizacao dos sensores...");
  delay(2000); // Dá 2 segundos para o DHT22 e MQ135 iniciarem com energia estável

  calibrarMQ135();

  lerSensores(); 
  leitura.scoreLocal = calcularScore(leitura.temperatura, leitura.mq135Raw);
  leitura.classificacao = classificar(leitura.scoreLocal);

  lcd.clear();
  lcd.print("Sistema OK");
  Serial.println("[SETUP] Inicializacao concluida com SUCESSO!\n");
  
  inicioMs      = millis();
  ultimaLeitura = millis(); 
  ultimoStatus  = millis(); 
  
}


// =============================================================
// LOOP
// =============================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOP WARN] Conexao Wi-Fi perdida! Tentando recuperar...");
    conectarWifi();
  }
  
  if (!mqttClient.connected()) {
    reconectarMQTT();
  }
  
  mqttClient.loop();
  server.handleClient();

  tickBuzzer();
  verificarComandoSerial();

  unsigned long agora = millis();

  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    executarCicloLeitura();
  }

  if (agora - ultimoStatus >= INTERVALO_STATUS) {
    ultimoStatus = agora;
    publicarStatus();
  }
}

void conectarWifi() {
  Serial.printf("[WIFI] Tentando conectar a rede: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI OK] Conectado com sucesso! Endereco IP Local: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WIFI ERROR] Falha ao conectar no Wi-Fi. Operando em modo offline.");
  }
}

void reconectarMQTT() {
  static unsigned long ultimaTentativaMqtt = 0;
  if (millis() - ultimaTentativaMqtt < 5000) return; // Nao bloqueia o loop, tenta a cada 5s
  
  ultimaTentativaMqtt = millis();
  Serial.printf("[MQTT] Tentando conectar ao Broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);
  
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.printf("[MQTT OK] Conectado com o ID: %s\n", MQTT_CLIENT_ID);
  } else {
    Serial.printf("[MQTT ERROR] Falhou. Codigo de erro rc=%d (Tentara novamente em 5s)\n", mqttClient.state());
  }
}

void executarCicloLeitura() {
  Serial.println("\n--- [NOVO CICLO DE LEITURA] ---");
  lerSensores();

  leitura.scoreLocal    = calcularScore(leitura.temperatura, leitura.mq135Raw);
  leitura.classificacao = classificar(leitura.scoreLocal);

  Serial.printf("[PROCESSADOR] Score Calculado: %.1f | Classificacao: %s\n", leitura.scoreLocal, leitura.classificacao.c_str());

  atualizarLED(leitura.classificacao);
  acionarBuzzerSeNecessario(leitura.classificacao);
  atualizarLCD(leitura.scoreLocal, leitura.classificacao);
  
  publicarTelemetria();

  if (leitura.classificacao == "CRITICO") {
    publicarAlerta();
  }
}

// ----------------------------------------------------------
// FAIXA PLAUSIVEL DE TEMPERATURA (contexto: clima de Sao Paulo)
// Valores fora desta faixa sao tratados como falha de sensor
// (cabo solto, ruido eletrico, sensor danificado), e NAO como
// "frio extremo real" -- o score nao deve reagir a -40C, pois
// isso nunca ocorre na pratica e indica dado invalido, nao um
// cenario ambiental valido para o algoritmo.
#define TEMP_MIN_PLAUSIVEL -10.0f
#define TEMP_MAX_PLAUSIVEL  55.0f

void lerSensores() {
  Serial.println("[SENSORES] Solicitando dados ao DHT22...");
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  bool leituraNumerica = (!isnan(t) && !isnan(h));
  bool leituraPlausivel = (t >= TEMP_MIN_PLAUSIVEL && t <= TEMP_MAX_PLAUSIVEL);

  leitura.dht22Ok = leituraNumerica && leituraPlausivel;

  if (leitura.dht22Ok) {
    leitura.temperatura = t;
    leitura.umidade     = h;
    Serial.printf("[SENSORES OK] DHT22 -> Temp: %.1f C | Umidade: %.1f%%\n", t, h);
  } else if (!leituraNumerica) {
    Serial.println("[SENSORES ERROR] Falha de leitura no DHT22 (NaN)! Mantendo ultimos valores.");
  } else {
    // Numerico, porem fora da faixa plausivel -- provavel falha de sensor/cabo.
    Serial.printf("[SENSORES ERROR] Temperatura implausivel (%.1f C) -- fora de [%.1f, %.1f]. Mantendo ultimos valores.\n",
      t, TEMP_MIN_PLAUSIVEL, TEMP_MAX_PLAUSIVEL);
  }

  Serial.println("[SENSORES] Lendo canal analgico do MQ135...");
  leitura.mq135Raw = analogRead(PIN_MQ135);
  leitura.mq135Ok  = (leitura.mq135Raw >= 0 && leitura.mq135Raw <= 4095);
  Serial.printf("[SENSORES OK] MQ135 -> Leitura Crua (Raw): %d\n", leitura.mq135Raw);
}

// =============================================================================
// CALIBRACAO MQ135 — executada uma vez no setup()
// Calcula a media de N leituras do ADC para usar como baseline ("ar
// limpo") deste ciclo de operacao. Substitui a abordagem de baseline
// fixo, que se mostrou instavel entre execucoes do simulador.
// =============================================================================
void calibrarMQ135() {
  Serial.println("[CALIBRACAO] Calibrando baseline do MQ135...");
  long soma = 0;
  for (int i = 0; i < MQ135_AMOSTRAS_CALIBRACAO; i++) {
    int lectura_i = analogRead(PIN_MQ135);
    soma += lectura_i;
    Serial.printf("[CALIBRACAO]   amostra %d/%d = %d\n", i + 1, MQ135_AMOSTRAS_CALIBRACAO, lectura_i);
    delay(200);
  }
  mq135Baseline = (float)soma / MQ135_AMOSTRAS_CALIBRACAO;
  Serial.printf("[CALIBRACAO OK] Baseline definido: %.1f (raw)\n", mq135Baseline);
}

// =============================================================================
// VERIFICAR COMANDO SERIAL — limpa e desativada
// =============================================================================
void verificarComandoSerial() {
  // Função limpa.
}

float calcularScore(float temp, int mq135Raw) {
  // REBALANCEAMENTO (peso temperatura 65% / ar 35%, ponto de corte 25C):
  // No simulador, o MQ135 oscila pouco em torno do baseline calibrado
  // (variacao tipica de ate +-10%), enquanto o slider de temperatura do
  // DHT22 cobre uma faixa ampla (ex: 20C a 60C+). Com os pesos originais
  // (60% ar / 40% temp, corte em 30C), o score minimo possivel ficava
  // travado em ~54-60 -- RUIM/CRITICO eram matematicamente inalcancaveis.
  // Com 65% temp / 35% ar e corte em 25C, a temperatura sozinha percorre
  // as 4 faixas (BOM->MODERADO->RUIM->CRITICO), e o ar continua atuando
  // como modulador real quando sua leitura varia.
  float fFactor = max(0.0f, 1.0f - max(0.0f, (temp - 25.0f) / 25.0f));
  float scoreTemp = fFactor * 65.0f;

  // Normaliza em torno do baseline calibrado no boot (mq135Baseline).
  // raw <= baseline  -> fatorAr = 1   (score maximo, ar dentro do esperado)
  // raw aumenta acima do baseline -> fatorAr cai linearmente até 0
  float fAir = max(0.0f, 1.0f - max(0.0f, ((float)mq135Raw - mq135Baseline) / MQ135_RANGE));
  float scoreAr = fAir * 35.0f;

  return scoreTemp + scoreAr;
}

String classificar(float score) {
  if (score >= 80.0) return "BOM";
  if (score >= 60.0) return "MODERADO";
  if (score >= 40.0) return "RUIM";
  return "CRITICO";
}

void atualizarLED(String classificacao) {
  digitalWrite(PIN_LED_VERDE,    LOW);
  digitalWrite(PIN_LED_AMARELO,  LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);

  if (classificacao == "BOM") {
    digitalWrite(PIN_LED_VERDE, HIGH);
    Serial.println("[ATUADORES] LED Verde ACESO.");
  } else if (classificacao == "MODERADO") {
    digitalWrite(PIN_LED_AMARELO, HIGH);
    Serial.println("[ATUADORES] LED Amarelo ACESO.");
  } else {
    digitalWrite(PIN_LED_VERMELHO, HIGH);
    Serial.println("[ATUADORES] LED Vermelho ACESO.");
  }
}

void acionarBuzzerSeNecessario(String classificacao) {
  if (classificacao != "CRITICO") {
    if (buzzerState.ativo) {
      Serial.println("[ATUADORES] Desativando alarme sonoro do Buzzer.");
    }
    buzzerState.ativo = false;
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (!buzzerState.ativo) {
    Serial.println("[⚠️ ALERTA ATUADOR] Estado CRITICO detectado! Armando sequencia do Buzzer.");
    buzzerState.ativo       = true;
    buzzerState.pulsoAtual  = 0;
    buzzerState.emHigh      = true;
    buzzerState.ultimoT     = millis();
    digitalWrite(PIN_BUZZER, HIGH);
  }
}

void tickBuzzer() {
  if (!buzzerState.ativo) return;

  unsigned long agora = millis();
  if (agora - buzzerState.ultimoT < buzzerState.DURACAO) return;

  buzzerState.ultimoT = agora;

  if (buzzerState.emHigh) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerState.emHigh = false;
  } else {
    buzzerState.pulsoAtual++;
    if (buzzerState.pulsoAtual >= buzzerState.pulsosTotal) {
      buzzerState.ativo = false;
      Serial.println("[ATUADORES] Sequencia de bips do buzzer concluida.");
    } else {
      digitalWrite(PIN_BUZZER, HIGH);
      buzzerState.emHigh = true;
    }
  }
}

void atualizarLCD(float score, String classificacao) {
  Serial.println("[LCD] Atualizando informacoes no display...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Score: ");
  lcd.print(score, 1);

  lcd.setCursor(0, 1);
  String linha2 = classificacao;
  if (linha2.length() > 16) linha2 = linha2.substring(0, 16);
  lcd.print(linha2);
}

void publicarTelemetria() {
  if (!mqttClient.connected()) {
    Serial.println("[MQTT SEND WARN] Falha no envio: Cliente desconectado do Broker.");
    return;
  }

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
  
  Serial.printf("[MQTT SEND] Publicando Telemetria no topico [%s]...\n", TOPICO_TELEMETRIA);
  if (mqttClient.publish(TOPICO_TELEMETRIA, buffer)) {
    Serial.println("[MQTT SEND OK] JSON enviado com sucesso!");
  } else {
    Serial.println("[MQTT SEND ERROR] Falha interna ao tentar publicar telemetria.");
  }
}

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
  
  Serial.printf("[MQTT SEND ALERTA] ⚠️ Enviando payload de emergencia para [%s]...\n", TOPICO_ALERTA);
  mqttClient.publish(TOPICO_ALERTA, buffer);
}

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
  
  Serial.println("[MQTT SEND STATUS] Enviando batimento de coracao (Uptime Heartbeat)...");
  mqttClient.publish(TOPICO_STATUS, buffer);
}

void configurarEndpoints() {
  server.on("/leitura", HTTP_GET, []() {
    Serial.println("[API REST] Requisicao GET recebida em /leitura");
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

  server.on("/status", HTTP_GET, []() {
    Serial.println("[API REST] Requisicao GET recebida em /status");
    StaticJsonDocument<256> doc;
    doc["deviceId"]      = MQTT_CLIENT_ID;
    doc["uptime"]        = (unsigned long)((millis() - inicioMs) / 1000);
    doc["mqttConectado"] = mqttClient.connected();
    doc["wifiSSID"]      = WIFI_SSID;
    doc["wifiRSSI"]      = WiFi.RSSI();
    doc["freeHeap"]      = ESP.getFreeHeap();

    String resposta;
    serializeJson(doc, resposta);
    server.send(200, "application/json", resposta);
  });

  server.on("/health", HTTP_GET, []() {
    Serial.println("[API REST] Requisicao GET recebida em /health");
    StaticJsonDocument<128> doc;
    doc["status"]       = "ok";
    doc["sensor_dht22"] = leitura.dht22Ok ? "ok" : "erro";
    doc["sensor_mq135"] = leitura.mq135Ok ? "ok" : "erro";
    doc["mqtt"]         = mqttClient.connected() ? "conectado" : "desconectado";

    String resposta;
    serializeJson(doc, resposta);
    server.send(200, "application/json", resposta);
  });

  server.onNotFound([]() {
    Serial.println("[API REST WARN] Tentativa de acesso a rota inexistente!");
    server.send(404, "application/json", "{\"erro\":\"rota nao encontrada\"}");
  });
}
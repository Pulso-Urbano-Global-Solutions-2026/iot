# 🛰️ Pulso Urbano — Módulo IoT

> **"O Sentinel-5P mede NO₂ em São Paulo com 3.5km de resolução — não enxerga uma rua. Nosso ESP32 resolve isso: mede a qualidade do ar localmente, no ponto exato, a cada 10 segundos. Os dois dados se encontram no backend Java, que combina satélite e sensor local para calcular um score mais preciso. Quando o ar fica crítico, o ESP32 acende o LED vermelho, dispara o buzzer e publica um alerta MQTT — em segundos, o Oracle registra e o app exibe para o usuário."**

---

## 1. Arquitetura do Módulo IoT

```
┌──────────────────────────────────────────────────────────────────┐
│                     PULSO URBANO — FLUXO IoT                     │
│                                                                    │
│  ┌──────────────┐     MQTT (10s)     ┌────────────────────────┐  │
│  │   ESP32      │ ──────────────────▶│  HiveMQ Cloud Broker   │  │
│  │ DHT22 + MQ135│                    └────────────┬───────────┘  │
│  │ LED + Buzzer │                                 │              │
│  │ LCD 16x2     │                    ┌────────────▼───────────┐  │
│  │ WebServer:80 │                    │       Node-RED         │  │
│  └──────────────┘                    │  (parse + dashboard)   │  │
│         │                            └────────────┬───────────┘  │
│         │ REST (LAN)                              │ HTTP POST     │
│         ▼                            ┌────────────▼───────────┐  │
│  ┌──────────────┐                    │  Java Spring Boot API  │  │
│  │ GET /leitura │                    │  POST /api/v1/iot/...  │  │
│  │ GET /status  │                    └────────────┬───────────┘  │
│  │ GET /health  │                                 │ JPA          │
│  └──────────────┘                    ┌────────────▼───────────┐  │
│                                      │     Oracle 19c          │  │
│                                      │   (leitura_iot)        │  │
│                                      └────────────┬───────────┘  │
│                                                   │ REST API      │
│                                      ┌────────────▼───────────┐  │
│                                      │  Mobile React Native   │  │
│                                      └────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. Tópicos MQTT

| Tópico | Direção | Frequência | Payload (campos principais) |
|--------|---------|------------|-----------------------------|
| `pulso/sp/zona-centro/telemetria` | ESP32 → Broker | A cada 10s | `zonaId`, `temperatura`, `umidade`, `mq135Raw`, `scoreLocal`, `classificacao`, `ts` |
| `pulso/sp/zona-centro/alerta` | ESP32 → Broker | Só quando CRÍTICO | `zonaId`, `nivel`, `scoreLocal`, `temperatura`, `mq135Raw`, `mensagem`, `ts` |
| `pulso/sp/zona-centro/status` | ESP32 → Broker | A cada 60s | `deviceId`, `zonaId`, `uptime`, `wifiRSSI`, `freeHeap`, `ts` |

**Broker:** `broker.hivemq.com:1883` (HiveMQ Cloud gratuito, sem autenticação)

---

## 3. Endpoints REST do ESP32

| Rota | Método | Resposta |
|------|--------|----------|
| `/leitura` | GET | JSON com temperatura, umidade, mq135Raw, scoreLocal, classificacao |
| `/status` | GET | JSON com deviceId, uptime, mqttConectado, wifiSSID, wifiRSSI, freeHeap |
| `/health` | GET | JSON com status geral, sensor_dht22, sensor_mq135, mqtt |

Acesso: `http://<IP_DO_ESP32>/leitura` (IP exibido no Serial Monitor após conexão Wi-Fi)

---

## 4. Como Simular no Wokwi

**Passo 1 — Importe o projeto:**
Acesse [wokwi.com](https://wokwi.com), clique em **New Project > ESP32**, abra o arquivo `wokwi/diagram.json` e cole o conteúdo no editor de diagrama.

**Passo 2 — Adicione o firmware:**
Cole o conteúdo de `firmware/pulso_urbano_iot/pulso_urbano_iot.ino` no editor de código. Instale as bibliotecas quando solicitado (`PubSubClient`, `ArduinoJson`, `DHTesp`, `LiquidCrystal_I2C`).

**Passo 3 — Execute e observe:**
Clique em **Play**. O Serial Monitor exibirá as leituras a cada 10s. Ajuste o slider de temperatura do DHT22 para ver a classificação mudar e o LED/buzzer reagirem. O Wokwi simula MQTT com o broker HiveMQ público automaticamente.

---

## 5. Como Rodar o Node-RED Localmente

**Passo 1 — Instale o Node-RED:**
```bash
npm install -g --unsafe-perm node-red
node-red
# Acesse: http://localhost:1880
```

**Passo 2 — Instale os nós de dashboard:**
No menu Node-RED → **Manage palette** → Install:
- `node-red-dashboard`
- `node-red-contrib-mqtt-broker` (opcional — para broker local)

**Passo 3 — Importe o fluxo:**
Copie o JSON do array `fluxo_nodered` em `nodered/nodered_pulso_urbano.js`, abra o Node-RED, vá em **Menu > Import > Clipboard**, cole e clique **Import**. Configure o nó MQTT Broker com `broker.hivemq.com:1883`. Clique **Deploy**. Acesse o dashboard em `http://localhost:1880/ui`.

---

## 6. Variáveis de Configuração do Firmware

Todas as configurações ficam nos `#define` no topo do arquivo `.ino`:

| Variável | Padrão | Descrição |
|----------|--------|-----------|
| `WIFI_SSID` | `"SUA_REDE_WIFI"` | Nome da rede Wi-Fi |
| `WIFI_PASSWORD` | `"SUA_SENHA_WIFI"` | Senha do Wi-Fi |
| `MQTT_BROKER` | `"broker.hivemq.com"` | Endereço do broker MQTT |
| `MQTT_PORT` | `1883` | Porta do broker |
| `MQTT_CLIENT_ID` | `"ESP32-PULSO-01"` | ID único do dispositivo |
| `ZONA_ID` | `1` | ID da zona monitorada (FK para `zona_cidade`) |
| `ZONA_NOME` | `"Centro SP"` | Nome legível da zona |
| `INTERVALO_LEITURA` | `10000` | Intervalo de leitura em ms (10s) |
| `INTERVALO_STATUS` | `60000` | Intervalo de status em ms (60s) |

---

## 7. Evidência da Integração — O que aparece no Oracle

Quando o ESP32 publica no tópico `telemetria`, o fluxo é:

```
ESP32 publica JSON →  Node-RED recebe e faz POST →  Java API processa
→  JPA executa INSERT na tabela leitura_iot

INSERT gerado:
INSERT INTO leitura_iot
  (id_zona, device_id, temperatura, umidade, mq135_raw, nivel_ar, score_local, dt_leitura)
VALUES
  (1, 'ESP32-PULSO-01', 28.5, 65.2, 1240, 'MODERADO', 61.4, SYSDATE);
```

Para verificar no SQL Developer:
```sql
SELECT * FROM leitura_iot ORDER BY dt_leitura DESC FETCH FIRST 10 ROWS ONLY;
```

Resultado esperado a cada 10s: uma nova linha com os valores do sensor local, cruzável com os dados orbitais via `id_zona`.

---

## 8. Hardware — Checklist de Componentes

| Componente | Pino ESP32 | Alimentação | Função |
|------------|-----------|-------------|--------|
| DHT22 | GPIO 4 | 3.3V | Temperatura + Umidade (entrada 1) |
| MQ135 (AO) | GPIO 34 | 5V | Qualidade do ar — analógico (entrada 2) |
| LED Verde | GPIO 27 + R 220Ω | — | Status BOM (saída 1) |
| LED Amarelo | GPIO 26 + R 220Ω | — | Status MODERADO (saída 1) |
| LED Vermelho | GPIO 25 + R 220Ω | — | Status RUIM/CRÍTICO (saída 1) |
| Buzzer ativo | GPIO 32 | — | Alerta sonoro quando CRÍTICO (saída 2) |
| LCD 16x2 I2C | SDA=21, SCL=22 | 5V | Score + classificação (interface) |

---

## 9. Algoritmo de Score — Referência Rápida

```
score_temp = max(0, 1 - max(0, (temperatura - 30) / 20)) × 40
score_ar   = max(0, 1 - (mq135_raw / 4095))              × 60
score_local = score_temp + score_ar  →  0 a 100

BOM      ≥ 80  →  LED verde  | sem buzzer
MODERADO ≥ 60  →  LED amarelo | sem buzzer
RUIM     ≥ 40  →  LED vermelho | sem buzzer
CRITICO  < 40  →  LED vermelho | 3 pulsos de 200ms
```

Mesmo algoritmo usado no backend Java — permite comparar o score local (ESP32) com o score orbital (Sentinel-5P) na mesma escala.

---

## 10. Dependências Arduino (instalar via Library Manager)

```
PubSubClient          by Nick O'Leary
ArduinoJson           by Benoit Blanchon
DHTesp                by beegee-tokyo
LiquidCrystal_I2C     by Frank de Brabander
WebServer             (built-in ESP32 core)
```

---

*Pulso Urbano IoT Module · Owner: Brisola · GS 2026/1 · FIAP 2TDS*

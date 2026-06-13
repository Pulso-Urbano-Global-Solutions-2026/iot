# Pulso Urbano — Módulo IoT

**Global Solution 2026/1 · FIAP · ADS 2º ano**  
Disciplina: **Disruptive Architectures: IoT, IoB & Generative AI**

> "O Sentinel-5P mede NO₂ em São Paulo com 3,5 km de resolução — não enxerga uma rua. Nosso ESP32 resolve isso: mede a qualidade do ar localmente, no ponto exato, a cada 10 segundos. Os dois dados se encontram no backend Java, que combina satélite e sensor local para calcular um score mais preciso. Quando o ar fica crítico, o ESP32 acende o LED vermelho, dispara o buzzer e publica um alerta MQTT — em segundos, o Oracle registra e o app exibe para o usuário."

---

## Equipe

| Integrante | RM | Papel |
|------------|-----|-------|
| **Felipe Ferrete** | 562999 | Tech Lead · 
| **Clayton Alves** | 562285 | Database · tabela `leitura_iot` no Oracle |
| **Guilherme Sola** | 563674 | Mobile · Frontend React Native |
| **Gustavo Bosak** | 566315 | QA · Arquitetura TOGAF |
| **Nikolas Brisola** | 564371 | IoT · |

---

## Links

| Recurso | URL |
|---------|-----|
| Projeto Wokwi | [wokwi.com/projects/466682387802505217](https://wokwi.com/projects/466682387802505217) |
| Vídeo do protótipo (3 min) | _[YouTube — preencher após gravação]_ |
| Dashboard Node-RED | `http://localhost:1880/dashboard/page2` (local, após configuração) |
| Java API (integração MQTT) | `https://hearty-adaptation-production-6de3.up.railway.app/swagger-ui.html` |

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

**Projeto público:** [wokwi.com/projects/466682387802505217](https://wokwi.com/projects/466682387802505217)

**Passo 1 — Importe o projeto:**
Acesse o link acima, ou crie um novo projeto em [wokwi.com](https://wokwi.com) (**New Project > ESP32**) e cole o conteúdo de `wokwi/diagram.json` no editor de diagrama.

**Passo 2 — Adicione o firmware:**
Cole o conteúdo de `firmware/pulso_urbano_iot/pulso_urbano_iot.ino` no editor de código. Instale as bibliotecas quando solicitado (`DHT sensor library`, `Adafruit Unified Sensor`, `LiquidCrystal_I2C`, `PubSubClient`, `ArduinoJson`).

**Passo 3 — Execute e observe:**
Clique em **Play**. O Serial Monitor exibirá as leituras a cada 10s. Ajuste o slider de temperatura do DHT22 entre **25°C e 50°C** para percorrer as 4 faixas de classificação (BOM → MODERADO → RUIM → CRÍTICO) e ver o LED/buzzer reagirem. O Wokwi simula MQTT com o broker HiveMQ público automaticamente. Veja a Seção 11 para detalhes de calibração e o modo de demonstração via Serial.

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
Abra o arquivo `nodered_pulso_urbano.json`, abra o Node-RED, vá em **Menu > Import > Clipboard**, cole e clique **Import**. Configure o nó MQTT Broker com `broker.hivemq.com:1883`. Clique **Deploy**. Acesse o dashboard em `http://localhost:1880/dashboard/page2`.

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
| LED Verde | GPIO 27 | — | Status BOM (saída 1) |
| LED Amarelo | GPIO 26 | — | Status MODERADO (saída 1) |
| LED Vermelho | GPIO 25 | — | Status RUIM/CRÍTICO (saída 1) |
| Buzzer ativo | GPIO 32 | — | Alerta sonoro quando CRÍTICO (saída 2) |
| LCD 16x2 I2C | SDA=21, SCL=22 | 5V | Score + classificação (interface) |

**Placa Wokwi:** `board-esp32-devkit-c-v4` (ESP32 DevKit oficial). O `diagram.json` inclui as conexões `$serialMonitor` necessárias para essa placa.

---

## 9. Algoritmo de Score — Referência Rápida

```
score_temp = max(0, 1 - max(0, (temperatura - 25) / 25)) × 65
score_ar   = max(0, 1 - max(0, (mq135_raw - baseline) / 1500)) × 35
score_local = score_temp + score_ar  →  0 a 100

BOM      ≥ 80  →  LED verde   | sem buzzer
MODERADO ≥ 60  →  LED amarelo | sem buzzer
RUIM     ≥ 40  →  LED vermelho| sem buzzer
CRITICO  < 40  →  LED vermelho| 3 pulsos de 200ms
```

`baseline` é calibrado automaticamente no boot (ver Seção 11). A faixa de temperatura plausível é -10°C a 55°C; leituras fora disso são tratadas como falha de sensor e descartadas (mantém-se o último valor válido).

> **Nota sobre o backend Java:** a fórmula global do projeto (`score = (1-NO₂/50)×0.60 + (1-max(0,(Temp-30)/20))×0.40`) usa pesos diferentes, pois combina dado orbital (NO₂) com temperatura de superfície. A fórmula local do ESP32 foi recalibrada (Seção 11) para o contexto de simulação — a integração entre os dois scores (local vs. orbital) é prevista para uma fase futura do projeto.

---

## 10. Dependências Arduino (instalar via Library Manager)

```
DHT sensor library     by Adafruit
Adafruit Unified Sensor by Adafruit
LiquidCrystal_I2C       by Frank de Brabander
PubSubClient            by Nick O'Leary
ArduinoJson             by Benoit Blanchon
WebServer               (built-in ESP32 core)
```

---

## 11. Decisões de Calibração e Limitações do Ambiente de Simulação

Durante os testes no Wokwi, identificamos comportamentos do sensor de gás simulado (`wokwi-gas-sensor`) que motivaram ajustes deliberados no firmware — documentados aqui para transparência técnica.

### 11.1 — Auto-calibração do MQ135 no boot

O valor bruto (`raw`) lido do MQ135 simulado varia significativamente entre execuções da simulação (observamos baselines entre ~1000 e ~3000 em runs distintos), mesmo com o slider de ppm no mesmo ponto. Em vez de um valor fixo de referência, o `setup()` agora executa `calibrarMQ135()`: tira 5 amostras do ADC e usa a média como `mq135Baseline` daquela execução.

Isso reflete uma prática real de sensores MQ — o **R0 (resistência em ar limpo) precisa ser calibrado no ambiente de instalação**, pois varia com ventilação, temperatura e gases residuais do local.

### 11.2 — Rebalanceamento dos pesos do score (65% temperatura / 35% ar)

No simulador, o slider de temperatura do DHT22 responde de forma confiável e cobre uma faixa ampla (-40°C a 80°C), enquanto o MQ135 oscila apenas ±10-15% em torno do baseline calibrado, independente do slider de ppm. Com os pesos originais (60% ar / 40% temp, corte em 30°C), o score mínimo possível ficava travado em ~54-60 — **RUIM e CRÍTICO eram matematicamente inalcançáveis**.

Os pesos foram ajustados para **65% temperatura / 35% ar**, com ponto de corte em 25°C (em vez de 30°C). Isso permite que a temperatura sozinha percorra as 4 faixas de classificação (BOM → MODERADO → RUIM → CRÍTICO), enquanto o ar continua atuando como modulador real quando sua leitura varia. Faixa de demonstração recomendada: **25°C a 50°C** no slider do DHT22.

### 11.3 — Validação de plausibilidade da temperatura

Leituras de temperatura fora da faixa **-10°C a 55°C** (fisicamente implausíveis para o contexto de São Paulo, mesmo em cenários extremos de ilha de calor) são tratadas como **falha de sensor** — o mesmo tratamento já dado a leituras `NaN` do DHT22. O score não é recalculado nesse caso; mantém-se o último valor válido e loga-se `[SENSORES ERROR] Temperatura implausivel`.

### 11.4 — Modo de demonstração via Serial

Para apresentações/vídeos, o firmware aceita comandos de 1 caractere via Serial Monitor que forçam a classificação exibida nos atuadores (LED/Buzzer/LCD), sem alterar o score real calculado e publicado via MQTT/REST:

| Comando | Efeito |
|---------|--------|
| `1` | Força exibição como BOM |
| `2` | Força exibição como MODERADO |
| `3` | Força exibição como RUIM |
| `4` | Força exibição como CRÍTICO (dispara buzzer) |
| `0` | Desativa o override — volta ao valor real dos sensores |

---

*Pulso Urbano IoT Module · Owner: Nikolas Brisola (RM 564371) · GS 2026/1 · FIAP 2TDS*
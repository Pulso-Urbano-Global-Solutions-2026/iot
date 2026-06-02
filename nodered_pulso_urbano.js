// =============================================================
// PULSO URBANO — Node-RED: lógica dos nós Function
// Cole o JSON de fluxo (abaixo) no Node-RED via Import > Clipboard
// =============================================================

// ------------------------------------------------------------
// NÓ FUNCTION 1 — Parse + enriquecimento (recebe do MQTT In: telemetria)
// Input:  msg.payload = string JSON do ESP32
// Output: múltiplas saídas — dashboard, Java API
// ------------------------------------------------------------
const fn_parse_enriquecimento = `
// Parse do payload MQTT publicado pelo ESP32
let dados;
try {
    dados = JSON.parse(msg.payload);
} catch (e) {
    node.error("Payload inválido: " + msg.payload);
    return null;
}

// Enriquecimento: adiciona timestamp legível e origin
dados.origem        = "sensor_local";
dados.processadoEm  = new Date().toISOString();

// --- Saída 1: gauge de temperatura (ui_gauge espera msg.payload como número)
let msgTemp = { payload: dados.temperatura, topic: "Temperatura (°C)" };

// --- Saída 2: gauge de qualidade do ar (MQ135 raw — gauge invertido)
let msgAr = { payload: dados.mq135Raw, topic: "Qualidade do Ar (MQ135 raw)" };

// --- Saída 3: texto com score + classificação (cor dinâmica via CSS)
let corClasse = {
    "BOM"      : "#27ae60",
    "MODERADO" : "#f39c12",
    "RUIM"     : "#e67e22",
    "CRITICO"  : "#e74c3c"
};
let msgScore = {
    payload : dados.scoreLocal.toFixed(1) + " — " + dados.classificacao,
    color   : corClasse[dados.classificacao] || "#7f8c8d",
    topic   : "Score Ambiental"
};

// --- Saída 4: gráfico histórico — envia objeto {x, y} para ui_chart
let msgChart = {
    payload : [{
        series : ["Temperatura", "Score"],
        labels  : [new Date().toLocaleTimeString("pt-BR")],
        data    : [[dados.temperatura], [dados.scoreLocal]]
    }],
    topic: "historico"
};

// --- Saída 5: payload para Java API POST /api/v1/iot/leitura
let msgJava = {
    payload: JSON.stringify({
        zonaId       : dados.zonaId,
        deviceId     : "ESP32-PULSO-01",
        temperatura  : dados.temperatura,
        umidade      : dados.umidade,
        mq135Raw     : dados.mq135Raw,
        nivelAr      : dados.nivelAr,
        scoreLocal   : dados.scoreLocal,
        classificacao: dados.classificacao,
        dtLeitura    : dados.processadoEm
    }),
    headers: {
        "Content-Type": "application/json"
    },
    method: "POST",
    url   : "http://localhost:8080/api/v1/iot/leitura"
};

// Node-RED: return array de saídas (null = não envia para aquela saída)
return [msgTemp, msgAr, msgScore, msgChart, msgJava];
`;

// ------------------------------------------------------------
// NÓ FUNCTION 2 — Alerta crítico (recebe do MQTT In: alerta)
// Input:  msg.payload = string JSON do tópico pulso/.../alerta
// Output: notificação visual + log
// ------------------------------------------------------------
const fn_alerta = `
let alerta;
try {
    alerta = JSON.parse(msg.payload);
} catch (e) {
    node.error("Alerta inválido: " + msg.payload);
    return null;
}

// Mensagem para ui_notification — popup vermelho no dashboard
let msgNotificacao = {
    payload : "⚠️ ALERTA CRÍTICO — Zona " + alerta.zonaId +
              " | Score: " + alerta.scoreLocal.toFixed(1) +
              " | Temp: " + alerta.temperatura + "°C" +
              " | " + alerta.mensagem,
    topic   : "ALERTA",
    color   : "#e74c3c"
};

// Log de alerta para auditoria (pode ser conectado a um debug node
// ou a um nó de INSERT no Oracle via node-red-node-oracle)
let msgLog = {
    payload: {
        tipo        : "ALERTA_CRITICO",
        zonaId      : alerta.zonaId,
        scoreLocal  : alerta.scoreLocal,
        temperatura : alerta.temperatura,
        mq135Raw    : alerta.mq135Raw,
        ts          : new Date().toISOString()
    },
    topic: "log_alerta"
};

return [msgNotificacao, msgLog];
`;

// =============================================================
// FLUXO NODE-RED — JSON importável via Menu > Import > Clipboard
// =============================================================
const fluxo_nodered = `
[
  {
    "id": "mqtt_in_telemetria",
    "type": "mqtt in",
    "name": "MQTT In: telemetria",
    "topic": "pulso/sp/zona-centro/telemetria",
    "broker": "hivemq_broker",
    "qos": "0",
    "wires": [["fn_parse"]]
  },
  {
    "id": "mqtt_in_alerta",
    "type": "mqtt in",
    "name": "MQTT In: alerta",
    "topic": "pulso/sp/zona-centro/alerta",
    "broker": "hivemq_broker",
    "qos": "0",
    "wires": [["fn_alerta"]]
  },
  {
    "id": "hivemq_broker",
    "type": "mqtt-broker",
    "name": "HiveMQ Cloud",
    "broker": "broker.hivemq.com",
    "port": "1883",
    "clientid": "nodered-pulso-01",
    "keepalive": "60",
    "cleansession": true
  },
  {
    "id": "fn_parse",
    "type": "function",
    "name": "Parse + Enriquecimento",
    "func": "COLE AQUI O CONTEÚDO DE fn_parse_enriquecimento",
    "outputs": 5,
    "wires": [
      ["gauge_temp"],
      ["gauge_ar"],
      ["text_score"],
      ["chart_historico"],
      ["http_java_api"]
    ]
  },
  {
    "id": "fn_alerta",
    "type": "function",
    "name": "Alerta Crítico",
    "func": "COLE AQUI O CONTEÚDO DE fn_alerta",
    "outputs": 2,
    "wires": [
      ["notificacao_critica"],
      ["debug_log"]
    ]
  },
  {
    "id": "gauge_temp",
    "type": "ui_gauge",
    "name": "Temperatura",
    "group": "grupo_pulso",
    "order": 1,
    "label": "Temperatura (°C)",
    "min": 0,
    "max": 60,
    "colors": ["#27ae60","#f39c12","#e74c3c"],
    "wires": []
  },
  {
    "id": "gauge_ar",
    "type": "ui_gauge",
    "name": "Qualidade do Ar",
    "group": "grupo_pulso",
    "order": 2,
    "label": "MQ135 Raw",
    "min": 0,
    "max": 4095,
    "colors": ["#27ae60","#f39c12","#e74c3c"],
    "wires": []
  },
  {
    "id": "text_score",
    "type": "ui_text",
    "name": "Score Ambiental",
    "group": "grupo_pulso",
    "order": 3,
    "label": "Score + Classificação",
    "format": "{{msg.payload}}",
    "wires": []
  },
  {
    "id": "chart_historico",
    "type": "ui_chart",
    "name": "Histórico",
    "group": "grupo_pulso",
    "order": 4,
    "label": "Histórico (últimas 20 leituras)",
    "chartType": "line",
    "legend": true,
    "xformat": "HH:mm:ss",
    "wires": []
  },
  {
    "id": "notificacao_critica",
    "type": "ui_notification",
    "name": "Popup Alerta CRÍTICO",
    "group": "grupo_pulso",
    "border": "#e74c3c",
    "timeout": 8000,
    "wires": []
  },
  {
    "id": "http_java_api",
    "type": "http request",
    "name": "POST Java API /iot/leitura",
    "method": "POST",
    "url": "http://localhost:8080/api/v1/iot/leitura",
    "wires": [["debug_log"]]
  },
  {
    "id": "debug_log",
    "type": "debug",
    "name": "Debug Log",
    "active": true,
    "wires": []
  }
]
`;

// =============================================================
// JUSTIFICATIVA DE NEGÓCIO — Node-RED como middleware
// =============================================================
/*
Por que Node-RED entre ESP32 e Java?

O ESP32 fala MQTT — protocolo leve, ideal para IoT, mas que o
frontend mobile não consome diretamente. O backend Java expõe
REST. O Node-RED é o tradutor entre esses dois mundos:

  ESP32 (MQTT) → Node-RED (parse, enriquecimento) → Java API (REST)
                               ↓
                       Dashboard em tempo real

Benefícios práticos para este projeto:
1. O Java não precisa abrir uma conexão MQTT permanente —
   o Node-RED absorve essa responsabilidade.
2. O dashboard Node-RED serve como "monitor de operações"
   durante o desenvolvimento antes do app mobile ficar pronto.
3. Filtros e transformações (ex: normalização de unidades,
   adição de timestamps) ficam no Node-RED sem tocar no firmware
   ou no backend Java — separação de responsabilidades.
4. Alertas críticos viram popups visuais imediatos — feedback
   mais rápido que esperar o app mobile atualizar.
*/

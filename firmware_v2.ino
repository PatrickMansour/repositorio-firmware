/*
 * PROJETO MOTIVA - CP2 | FIRMWARE 2.0
 * No de monitoramento de vegetacao (ESP32 no Wokwi)
 *
 * Mantem tudo do Firmware 1.0 e acrescenta:
 *  - Ordenacao (bubble sort) de uma COPIA das 5 leituras
 *  - Exibicao da ordem original e da ordem crescente
 *  - Mediana (3o elemento do vetor ordenado)
 *  - Histerese sobre a mediana:  >= 16 -> ALERTA | <= 14 -> NORMAL | entre -> mantem
 *  - LED: verde = NORMAL, vermelho = ALERTA
 *  - Modo de teste pelo Serial Monitor (para provar os testes 6, 7 e 8 rapidamente)
 *
 * O FW 2.0 tambem consulta o manifesto: como version.json = "2.0" e a versao
 * instalada = "2.0", ele informa "ja e a mais recente" (evita loop de atualizacao).
 *
 * Ligacao: igual ao FW 1.0 (GPIO25=R, GPIO26=G, GPIO27=B, COM no GND).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

// ======================= CONFIGURACAO =======================
const char* FW_VERSION   = "2.0";
const char* WIFI_SSID    = "Wokwi-GUEST";
const char* WIFI_SENHA   = "";

// >>> TROQUE pelo link RAW do seu version.json no GitHub <<<
const char* MANIFEST_URL =
  "https://raw.githubusercontent.com/PatrickMansour/repositorio-firmware/main/version.json";

const int PIN_LED_R = 25;
const int PIN_LED_G = 26;
const int PIN_LED_B = 27;

const int NUM_LEITURAS = 5;
const unsigned long INTERVALO_LEITURA_MS = 2000UL;
const unsigned long INTERVALO_SESSAO_MS  = 48000UL;
const int SESSOES_ATE_VERIFICAR_OTA = 3;

// Histerese (cm)
const int LIMITE_ALERTA = 16;   // mediana >= 16 -> ALERTA
const int LIMITE_NORMAL = 14;   // mediana <= 14 -> NORMAL

// ======================= ESTADO GLOBAL =======================
int leituras[NUM_LEITURAS];
int indiceLeitura = 0;
bool sessaoAtiva = false;
int numeroSessao = 0;
int sessoesConcluidas = 0;
unsigned long proximaSessaoMs = 0;
unsigned long proximaLeituraMs = 0;

bool estadoAlerta = false;   // false = NORMAL (estado inicial), true = ALERTA
char modoTeste = 'r';        // r = aleatorio | a = alerta | n = normal | h = histerese

// Declaracao antecipada: PlatformIO (ao contrario do Arduino IDE) nao gera
// prototipos automaticos, entao precisamos declarar antes do primeiro uso.
void verificarAtualizacao();

// ======================= LED =======================
void setLed(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r ? HIGH : LOW);
  digitalWrite(PIN_LED_G, g ? HIGH : LOW);
  digitalWrite(PIN_LED_B, b ? HIGH : LOW);
}

void aplicarLed() {
  if (estadoAlerta) setLed(true, false, false);   // vermelho = ALERTA
  else              setLed(false, true, false);   // verde    = NORMAL
}

void ledAtualizando() { setLed(true, true, false); }   // amarelo = OTA em andamento

const char* nomeEstado() { return estadoAlerta ? "ALERTA" : "NORMAL"; }

// ======================= LEITURAS / ESTATISTICA =======================
int gerarLeitura(int indice) {
  switch (modoTeste) {
    case 'a': return random(17, 21);   // 17..20 -> mediana sempre >= 17 (ALERTA)
    case 'n': return random(10, 14);   // 10..13 -> mediana sempre <= 13 (NORMAL)
    case 'h': {                        // padrao fixo cuja mediana e 15 (zona de histerese)
      const int padrao[NUM_LEITURAS] = {15, 14, 16, 15, 15};
      return padrao[indice];
    }
    default:  return random(10, 21);   // aleatorio normal: 10..20
  }
}

float calcularMedia(const int v[], int n) {
  long soma = 0;
  for (int i = 0; i < n; i++) soma += v[i];
  return (float)soma / n;
}

void copiarVetor(const int origem[], int destino[], int n) {
  for (int i = 0; i < n; i++) destino[i] = origem[i];
}

// Bubble sort implementado no proprio programa (exigencia do enunciado)
void ordenarCrescente(int v[], int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - 1 - i; j++) {
      if (v[j] > v[j + 1]) {
        int aux = v[j];
        v[j] = v[j + 1];
        v[j + 1] = aux;
      }
    }
  }
}

// Para 5 valores ordenados, a mediana e o elemento do meio (indice 2 = 3o elemento)
int calcularMediana(const int ordenado[], int n) {
  return ordenado[n / 2];
}

void imprimirVetor(const int v[], int n) {
  for (int i = 0; i < n; i++) {
    Serial.print(v[i]);
    Serial.print(i < n - 1 ? " " : "\n");
  }
}

// ======================= HISTERESE =======================
void atualizarEstadoHisterese(int mediana) {
  if (mediana >= LIMITE_ALERTA) {
    Serial.printf("Histerese: mediana %d >= %d -> ALERTA\n", mediana, LIMITE_ALERTA);
    estadoAlerta = true;
  } else if (mediana <= LIMITE_NORMAL) {
    Serial.printf("Histerese: mediana %d <= %d -> NORMAL\n", mediana, LIMITE_NORMAL);
    estadoAlerta = false;
  } else {
    Serial.printf("Histerese: mediana %d entre %d e %d -> estado anterior mantido (%s)\n",
                  mediana, LIMITE_NORMAL, LIMITE_ALERTA, nomeEstado());
  }
}

// ======================= MODO DE TESTE (Serial Monitor) =======================
void lerComandosSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'a' || c == 'n' || c == 'h') {
      modoTeste = c;
      Serial.printf("\n[TESTE] Modo '%c' ativo (r=aleatorio, a=alerta, n=normal, h=histerese)\n", c);
    }
  }
}

// ======================= SESSAO =======================
void imprimirCabecalho() {
  Serial.println();
  Serial.println("========================================");
  Serial.printf("MONITORAMENTO DE VEGETACAO - FW %s\n", FW_VERSION);
  Serial.println("========================================");
  Serial.printf("[Sessao #%d | t = %lu s]\n", numeroSessao, millis() / 1000UL);
}

void iniciarSessao() {
  sessaoAtiva = true;
  indiceLeitura = 0;
  numeroSessao++;
  proximaLeituraMs = proximaSessaoMs;
  proximaSessaoMs += INTERVALO_SESSAO_MS;
  imprimirCabecalho();
}

void finalizarSessao() {
  int ordenados[NUM_LEITURAS];
  copiarVetor(leituras, ordenados, NUM_LEITURAS);   // ordena uma COPIA
  ordenarCrescente(ordenados, NUM_LEITURAS);

  float media = calcularMedia(leituras, NUM_LEITURAS);
  int mediana = calcularMediana(ordenados, NUM_LEITURAS);

  Serial.print("Leituras (ordem original):  ");
  imprimirVetor(leituras, NUM_LEITURAS);
  Serial.print("Valores ordenados:          ");
  imprimirVetor(ordenados, NUM_LEITURAS);
  Serial.printf("Media da sessao:   %.1f cm\n", media);
  Serial.printf("Mediana da sessao: %d cm\n", mediana);

  atualizarEstadoHisterese(mediana);
  aplicarLed();
  Serial.printf("Estado do sistema: %s\n", nomeEstado());
  Serial.println("Proxima sessao em 48 segundos (contados do inicio desta).");

  sessoesConcluidas++;
  if (sessoesConcluidas % SESSOES_ATE_VERIFICAR_OTA == 0) {
    verificarAtualizacao();
  }
}

void executarLeitura() {
  leituras[indiceLeitura] = gerarLeitura(indiceLeitura);
  Serial.printf("Leitura %d: %d cm\n", indiceLeitura + 1, leituras[indiceLeitura]);
  indiceLeitura++;
  proximaLeituraMs += INTERVALO_LEITURA_MS;

  if (indiceLeitura >= NUM_LEITURAS) {
    sessaoAtiva = false;
    finalizarSessao();
  }
}

// ======================= OTA =======================
String extrairCampo(const String &json, const String &chave) {
  String busca = "\"" + chave + "\"";
  int i = json.indexOf(busca);
  if (i < 0) return "";
  i = json.indexOf(':', i + busca.length());
  if (i < 0) return "";
  int ini = json.indexOf('"', i + 1);
  if (ini < 0) return "";
  int fim = json.indexOf('"', ini + 1);
  if (fim < 0) return "";
  return json.substring(ini + 1, fim);
}

int compararVersoes(const String &a, const String &b) {
  int ia = 0, ib = 0;
  int la = a.length(), lb = b.length();
  while (ia < la || ib < lb) {
    int fa = a.indexOf('.', ia); if (fa < 0) fa = la;
    int fb = b.indexOf('.', ib); if (fb < 0) fb = lb;
    long na = (ia < la) ? a.substring(ia, fa).toInt() : 0;
    long nb = (ib < lb) ? b.substring(ib, fb).toInt() : 0;
    if (na != nb) return (na > nb) ? 1 : -1;
    ia = fa + 1;
    ib = fb + 1;
  }
  return 0;
}

bool conectarWiFi() {
  Serial.println("[OTA] Conectando ao Wi-Fi Wokwi-GUEST...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA][ERRO] Sem conexao Wi-Fi. Atualizacao cancelada; seguindo com o firmware atual.");
    return false;
  }
  Serial.print("[OTA] Wi-Fi conectado. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

void desconectarWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool obterManifesto(String &versaoRemota, String &urlFirmware) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);

  Serial.println("[OTA] Consultando manifesto version.json...");
  if (!http.begin(client, MANIFEST_URL)) {
    Serial.println("[OTA][ERRO] URL do manifesto invalida.");
    return false;
  }
  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("[OTA][ERRO] Manifesto nao pode ser acessado (HTTP %d).\n", codigo);
    http.end();
    return false;
  }
  String corpo = http.getString();
  http.end();

  versaoRemota = extrairCampo(corpo, "version");
  urlFirmware  = extrairCampo(corpo, "url");
  if (versaoRemota.length() == 0 || urlFirmware.length() == 0) {
    Serial.println("[OTA][ERRO] Manifesto invalido (faltam os campos 'version' e/ou 'url').");
    return false;
  }
  return true;
}

bool baixarEAtualizar(const String &url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  if (!http.begin(client, url)) {
    Serial.println("[OTA][ERRO] URL do firmware invalida.");
    return false;
  }
  Serial.println("[OTA] Baixando firmware...");
  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("[OTA][ERRO] Arquivo de firmware nao pode ser baixado (HTTP %d).\n", codigo);
    http.end();
    return false;
  }

  int tamanho = http.getSize();
  Serial.printf("[OTA] Tamanho do arquivo: %d bytes\n", tamanho);

  if (!Update.begin(tamanho > 0 ? (size_t)tamanho : UPDATE_SIZE_UNKNOWN)) {
    Serial.print("[OTA][ERRO] Update.begin falhou: ");
    Update.printError(Serial);
    http.end();
    return false;
  }

  Serial.println("[OTA] Gravando na flash (no Wokwi isso pode levar alguns minutos)...");
  size_t escritos = Update.writeStream(*http.getStreamPtr());
  if (tamanho > 0 && escritos != (size_t)tamanho) {
    Serial.printf("[OTA][ERRO] Gravou %u de %d bytes.\n", (unsigned)escritos, tamanho);
    Update.abort();
    http.end();
    return false;
  }
  if (!Update.end()) {
    Serial.print("[OTA][ERRO] Processo de atualizacao retornou erro: ");
    Update.printError(Serial);
    http.end();
    return false;
  }
  if (!Update.isFinished()) {
    Serial.println("[OTA][ERRO] Atualizacao nao foi finalizada.");
    http.end();
    return false;
  }
  http.end();
  return true;
}

void verificarAtualizacao() {
  Serial.println();
  Serial.println("[OTA] ===== Verificando atualizacao =====");
  Serial.printf("[OTA] Versao instalada: %s\n", FW_VERSION);

  if (!conectarWiFi()) return;

  String versaoRemota, urlFirmware;
  if (!obterManifesto(versaoRemota, urlFirmware)) {
    desconectarWiFi();
    return;
  }
  Serial.printf("[OTA] Versao disponivel: %s\n", versaoRemota.c_str());
  Serial.printf("[OTA] URL do firmware:   %s\n", urlFirmware.c_str());

  if (compararVersoes(versaoRemota, FW_VERSION) <= 0) {
    Serial.println("[OTA] A versao instalada ja e a mais recente. Nenhuma atualizacao necessaria.");
    desconectarWiFi();
    return;
  }

  Serial.println("[OTA] Nova versao disponivel! Iniciando atualizacao...");
  ledAtualizando();
  if (baixarEAtualizar(urlFirmware)) {
    Serial.println("[OTA] Atualizacao concluida com sucesso. Reiniciando em 3 s...");
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("[OTA][ERRO] Falha na atualizacao. Continuando com o firmware atual.");
    aplicarLed();
    desconectarWiFi();
  }
}

// ======================= SETUP / LOOP =======================
void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  aplicarLed();

  Serial.println();
  Serial.printf("[BOOT] Equipamento executando FIRMWARE %s\n", FW_VERSION);
  Serial.println("[TESTE] Digite no Serial Monitor: a=alerta, n=normal, h=histerese, r=aleatorio");
  proximaSessaoMs = millis();
}

void loop() {
  lerComandosSerial();
  unsigned long agora = millis();

  if (!sessaoAtiva && (long)(agora - proximaSessaoMs) >= 0) {
    iniciarSessao();
  }
  if (sessaoAtiva && (long)(agora - proximaLeituraMs) >= 0) {
    executarLeitura();
  }
}

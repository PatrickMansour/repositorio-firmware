/*
 * PROJETO MOTIVA - CP2 | FIRMWARE 1.0
 * No de monitoramento de vegetacao (ESP32 no Wokwi)
 *
 * O que este firmware faz:
 *  - Sessao de 5 leituras simuladas (10 a 20 cm), uma leitura a cada 2 s
 *  - Nova sessao a cada 48 s, contados a partir do INICIO da sessao anterior
 *  - Calcula e exibe a media aritmetica
 *  - LED azul = Firmware 1.0 em execucao
 *  - A cada 3 sessoes, consulta o manifesto version.json (HTTPS) e, se existir
 *    versao mais nova, baixa o .bin, grava via OTA e reinicia o ESP32
 *
 * Ligacao no Wokwi (LED RGB catodo comum + 3 resistores de 220 ohms):
 *   GPIO25 -> resistor -> R      GPIO26 -> resistor -> G
 *   GPIO27 -> resistor -> B      GND    -> COM
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

// ======================= CONFIGURACAO =======================
const char* FW_VERSION   = "1.0";
const char* WIFI_SSID    = "Wokwi-GUEST";
const char* WIFI_SENHA   = "";

// >>> TROQUE pelo link RAW do seu version.json no GitHub <<<
const char* MANIFEST_URL =
  "https://raw.githubusercontent.com/PatrickMansour/repositorio-firmware/main/version.json";

const int PIN_LED_R = 25;
const int PIN_LED_G = 26;
const int PIN_LED_B = 27;

const int NUM_LEITURAS = 5;
const unsigned long INTERVALO_LEITURA_MS = 2000UL;   // 2 s entre leituras
const unsigned long INTERVALO_SESSAO_MS  = 48000UL;  // 48 s entre INICIOS de sessao
const int SESSOES_ATE_VERIFICAR_OTA = 3;             // consulta o manifesto apos 3 ciclos

// ======================= ESTADO GLOBAL =======================
int leituras[NUM_LEITURAS];
int indiceLeitura = 0;
bool sessaoAtiva = false;
int numeroSessao = 0;
int sessoesConcluidas = 0;
unsigned long proximaSessaoMs = 0;    // instante (millis) do inicio da proxima sessao
unsigned long proximaLeituraMs = 0;   // instante (millis) da proxima leitura

// Declaracao antecipada: PlatformIO (ao contrario do Arduino IDE) nao gera
// prototipos automaticos, entao precisamos declarar antes do primeiro uso.
void verificarAtualizacao();

// ======================= LED =======================
void setLed(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r ? HIGH : LOW);
  digitalWrite(PIN_LED_G, g ? HIGH : LOW);
  digitalWrite(PIN_LED_B, b ? HIGH : LOW);
}

void aplicarLed()     { setLed(false, false, true); }  // azul = FW 1.0
void ledAtualizando() { setLed(true, true, false); }   // amarelo = OTA em andamento

// ======================= LEITURAS / ESTATISTICA =======================
int gerarLeitura() {
  return random(10, 21);   // inteiros de 10 a 20 (o limite superior e exclusivo)
}

float calcularMedia(const int v[], int n) {
  long soma = 0;
  for (int i = 0; i < n; i++) soma += v[i];
  return (float)soma / n;
}

// ======================= SESSAO (temporizacao com millis) =======================
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
  proximaLeituraMs = proximaSessaoMs;           // 1a leitura no inicio da sessao
  proximaSessaoMs += INTERVALO_SESSAO_MS;       // agenda a proxima a partir do INICIO desta
  imprimirCabecalho();
}

void finalizarSessao() {
  float media = calcularMedia(leituras, NUM_LEITURAS);
  Serial.printf("Media da sessao: %.1f cm\n", media);
  Serial.println("Proxima sessao em 48 segundos (contados do inicio desta).");

  sessoesConcluidas++;
  if (sessoesConcluidas % SESSOES_ATE_VERIFICAR_OTA == 0) {
    verificarAtualizacao();
  }
}

void executarLeitura() {
  leituras[indiceLeitura] = gerarLeitura();
  Serial.printf("Leitura %d: %d cm\n", indiceLeitura + 1, leituras[indiceLeitura]);
  indiceLeitura++;
  proximaLeituraMs += INTERVALO_LEITURA_MS;

  if (indiceLeitura >= NUM_LEITURAS) {
    sessaoAtiva = false;
    finalizarSessao();
  }
}

// ======================= OTA: manifesto e comparacao de versoes =======================
// Extrai o valor de uma chave simples de um JSON: {"chave": "valor"}
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

// Retorna >0 se a > b, <0 se a < b, 0 se iguais. Ex.: "2.0" vs "1.0" -> 1
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
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);   // canal 6 acelera a conexao no Wokwi
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
  client.setInsecure();   // simplificacao didatica: nao valida o certificado do servidor
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
  proximaSessaoMs = millis();   // primeira sessao comeca imediatamente
}

void loop() {
  unsigned long agora = millis();

  // (long) na subtracao: seguro mesmo quando millis() estoura (~49 dias)
  if (!sessaoAtiva && (long)(agora - proximaSessaoMs) >= 0) {
    iniciarSessao();
  }
  if (sessaoAtiva && (long)(agora - proximaLeituraMs) >= 0) {
    executarLeitura();
  }
}

# Projeto Motiva — CP2 | Atualização Remota de Firmware (OTA)

Simulação de um nó IoT de monitoramento de vegetação em ESP32 (Wokwi), com
atualização remota de firmware (OTA) via HTTP a partir de um repositório
GitHub.

## Integrantes

Fernando Melo - RM 564297
Patrick Mansour - RM 562970
Pedro Henrique Ribeiro - RM 565090
Pietro Mauer - RM 564345
Ryan Santos - RM 565102
Samir Assad - RM 561562

## Arquitetura da solução

```
ESP32 (Wokwi)  --Wi-Fi (Wokwi-GUEST)-->  Internet  -->  Repositório remoto (GitHub)
   Firmware 1.0                                          |-- version.json
                                                          |-- firmware_v2.bin
                                                          |-- firmware_v1.ino
                                                          |-- firmware_v2.ino

Fluxo do OTA:
consultar version.json -> comparar versao -> baixar .bin -> gravar (Update.h) -> reiniciar
```

O ESP32 roda o **Firmware 1.0**. A cada 3 sessões de leitura (~104 s), ele
conecta ao Wi-Fi, consulta o manifesto `version.json` no GitHub e, se a
versão remota for mais nova, baixa o `firmware_v2.bin`, grava via OTA
(`Update.h`) e reinicia — passando a executar o **Firmware 2.0**.

### Hardware simulado

- ESP32 Dev Kit (Wokwi)
- LED RGB catodo comum + 3 resistores de 220 Ω
  - GPIO25 → R · GPIO26 → G · GPIO27 → B · COM → GND

Não é usado nenhum sensor físico: as leituras são valores pseudoaleatórios
entre 10 e 20 cm, simulando a altura da vegetação.

## Firmware 1.0

- Identifica no Serial Monitor que está executando a versão 1.0.
- Gera 5 leituras por sessão (uma a cada 2 s), guarda num vetor.
- Calcula e exibe a média aritmética da sessão.
- Nova sessão a cada 48 s, contados a partir do **início** da sessão
  anterior (temporização feita com `millis()`, sem `delay()` longos).
- LED azul aceso enquanto essa versão está rodando.
- A cada 3 sessões, consulta o manifesto remoto e executa o OTA quando há
  versão mais nova.

## Firmware 2.0

Mantém tudo do Firmware 1.0 e acrescenta:

- Ordena uma **cópia** das 5 leituras (ordenação implementada no próprio
  código — bubble sort) e exibe a ordem original e a ordenada.
- Calcula e exibe a **mediana** (3º elemento do vetor ordenado).
- Aplica **histerese** sobre a mediana para decidir o estado do sistema:

  | Condição | Comportamento |
  |---|---|
  | Mediana ≥ 16 cm | Entra em **ALERTA** |
  | 14 cm < Mediana < 16 cm | Mantém o estado anterior |
  | Mediana ≤ 14 cm | Entra/retorna a **NORMAL** |

- LED verde (NORMAL) ou vermelho (ALERTA).
- Também consulta o manifesto remoto: como já está na versão mais recente,
  informa isso no Serial Monitor (evita loop de atualização).
- **Modo de teste** via Serial Monitor, para provar os cenários de
  histerese sem esperar minutos: digite `a` (força ALERTA), `n` (força
  NORMAL), `h` (força zona de histerese) ou `r` (aleatório).

## Por que média, mediana e histerese

- **Média**: resume o comportamento geral da sessão, mas é sensível a
  valores fora da curva (outliers).
- **Mediana**: mais robusta a leituras isoladas discrepantes, por isso é
  usada como base da decisão de estado — evita que uma leitura pontual
  dispare um alerta indevido.
- **Histerese**: evita que o estado fique "piscando" entre NORMAL e ALERTA
  quando a mediana oscila perto do limite (16 cm), exigindo que ela saia
  claramente da faixa intermediária antes de mudar o estado.

## Por que OTA é útil em dispositivos de campo

Sensores de vegetação instalados em campo (como os do Projeto Motiva)
podem estar em locais de difícil acesso. A atualização remota evita
deslocamento físico para atualizar cada equipamento manualmente, permite
corrigir bugs e adicionar funcionalidades em escala, e reduz custo e tempo
de manutenção — o firmware evolui sem intervenção local no hardware.

## Tratamento de erros

O programa informa no Serial Monitor cada uma das situações abaixo, sem
travar:

| Situação | Mensagem |
|---|---|
| Sem Wi-Fi | `[OTA][ERRO] Sem conexao Wi-Fi. Atualizacao cancelada...` |
| Manifesto inacessível | `[OTA][ERRO] Manifesto nao pode ser acessado (HTTP ...)` |
| Já está na versão mais recente | `[OTA] A versao instalada ja e a mais recente...` |
| Firmware não pode ser baixado | `[OTA][ERRO] Arquivo de firmware nao pode ser baixado (HTTP ...)` |
| Erro na gravação OTA | `[OTA][ERRO] Update.begin falhou: ...` / `Processo de atualizacao retornou erro` |

## Testes realizados

| # | Condição | Resultado esperado |
|---|---|---|
| 1 | Firmware 1.0 | 5 leituras, média e LED azul |
| 2 | Sessão completa | Nova sessão em 48 s (não 56 s) |
| 3 | Manifesto indica versão 2.0 | ESP32 identifica atualização após 3 ciclos |
| 4 | OTA executada | ESP32 reinicia executando Firmware 2.0 |
| 5 | Firmware 2.0 | Média, ordenação e mediana exibidas corretamente |
| 6 | Mediana ≥ 16 cm | Estado ALERTA, LED vermelho |
| 7 | Mediana entre 14 e 16 cm | Estado anterior mantido |
| 8 | Mediana ≤ 14 cm | Estado NORMAL, LED verde |

## Como executar

### Opção 1 — Wokwi (navegador)

1. Abra o link do projeto Wokwi listado acima.
2. Clique em ▶ para iniciar a simulação.
3. Acompanhe o Serial Monitor.

### Opção 2 — Wokwi para VS Code (PlatformIO)

1. Instale as extensões **PlatformIO IDE** e **Wokwi for VS Code**.
2. Ative a licença gratuita do Wokwi (`Wokwi: Request a New License`).
3. Abra a pasta do firmware desejado (`firmware_v1` ou `firmware_v2`).
4. `PlatformIO: Build`.
5. `Wokwi: Start Simulator`.

## Estrutura do repositório remoto

```
repositorio-firmware/
├── version.json        # manifesto: versao disponivel + URL do .bin
├── firmware_v2.bin      # binario da versao 2.0, usado no OTA
├── firmware_v1.ino      # codigo-fonte completo do Firmware 1.0
└── firmware_v2.ino      # codigo-fonte completo do Firmware 2.0
```

## Bibliotecas utilizadas

| Biblioteca | Função no projeto |
|---|---|
| `WiFi.h` | Conecta o ESP32 à rede Wokwi-GUEST |
| `WiFiClientSecure.h` | Cliente HTTPS para acessar o repositório no GitHub |
| `HTTPClient.h` | Requisições HTTP/HTTPS (consulta ao manifesto e download do `.bin`) |
| `Update.h` | Gravação do novo firmware na flash (OTA) e validação da imagem |

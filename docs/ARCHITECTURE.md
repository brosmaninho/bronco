# Arquitetura do Bronco

## Visao Geral

O Bronco funciona como uma DLL proxy de d3d11.dll que intercepta as chamadas graficas
do Guild Wars 2 para renderizar traducoes na tela do jogo.

## Fluxo de Dados

```
GW2.exe carrega d3d11.dll (nosso proxy)
    |
    v
Proxy carrega d3d11.dll real do System32
    |
    v
D3D11CreateDeviceAndSwapChain -> Hook do Present()
    |
    v
A cada frame (Present):
    1. Captura backbuffer
    2. OCR na regiao ao redor do cursor (modo padrao) ou nas regioes fixas
    3. Busca traducao nos dicionarios (com cache LRU)
    4. Renderiza traducao via Dear ImGui overlay
    5. Chama Present() original
```

## Regioes de OCR: seguir o mouse

Os tooltips do Guild Wars 2 (itens, habilidades) aparecem onde o cursor esta, e nao
em posicoes fixas da tela. Por isso o modo padrao do Bronco e "seguir o mouse"
(`ocr_follow_mouse: true`).

A cada ciclo de leitura, o worker do pipeline:

1. Le a posicao do cursor com `GetCursorPos` (coordenadas de tela).
2. Converte para coordenadas de cliente com `ScreenToClient(bronco::overlay::gameWindow(), ...)`,
   que correspondem ao espaco de pixels do backbuffer capturado. Se a janela do jogo
   nao estiver disponivel, usa as coordenadas de tela como fallback.
3. Monta uma unica regiao centrada no cursor, com tamanho `ocr_follow_width` x
   `ocr_follow_height` (padrao 500x400).
4. Ajusta (clamp) a regiao para caber inteiramente dentro da tela capturada
   (`x >= 0`, `y >= 0`, `x + width <= largura`, `y + height <= altura`). Esse ajuste e
   obrigatorio porque `OcrEngine::recognizeRegions` descarta silenciosamente qualquer
   regiao fora dos limites.

Quando `ocr_follow_mouse` e `false`, o pipeline usa a lista `ocr_regions` como
fallback, exatamente como antes.

O pipeline tambem registra em log (via `bronco::log::info`) a regiao calculada, o texto
reconhecido pelo OCR e se uma traducao foi encontrada no dicionario, para facilitar o
diagnostico.

### Novas chaves de configuracao

| Chave | Descricao | Padrao |
|-------|-----------|--------|
| `ocr_follow_mouse` | Ativa o modo seguir o mouse (padrao) | `true` |
| `ocr_follow_width` | Largura da regiao ao redor do cursor (px) | `500` |
| `ocr_follow_height` | Altura da regiao ao redor do cursor (px) | `400` |

## Modulos

### proxy/ - DLL Proxy
- Carrega a d3d11.dll real do System32
- Encaminha todas as funcoes exportadas
- Intercepta CreateDeviceAndSwapChain para instalar o hook

### hook/ - Present Hook
- Modifica a vtable do IDXGISwapChain
- Intercepta Present() para renderizar antes da apresentacao
- Inicializa o overlay na primeira chamada

### overlay/ - ImGui Overlay
- Inicializa Dear ImGui com backend DX11
- Renderiza texto traduzido nas posicoes corretas
- Hook do WndProc para capturar input
- Passivo por padrao: o painel NAO intercepta o mouse. So captura o mouse e
  fica arrastavel enquanto o usuario segura ALT (detectado via
  `GetKeyState(VK_MENU)`). Quando ALT nao esta pressionado, toda mensagem de
  mouse passa direto para o jogo (o jogo nunca perde o controle do mouse).
- F8 (configuravel) liga/desliga a visibilidade do overlay

### ocr/ - Motor OCR
- Usa Tesseract 5 para reconhecimento de texto
- Opera em regioes especificas da tela
- Suporta threshold de confianca configuravel

### translation/ - Motor de Traducao
- Carrega dicionarios JSON por categoria
- Busca case-insensitive
- Cache LRU integrado para performance

### cache/ - Cache LRU
- Template header-only
- Operacoes O(1) get/put
- Thread-safe com mutex
- Capacidade configuravel

### config/ - Configuracao
- Carrega/salva JSON
- Singleton global
- Define regioes OCR, idioma, hotkeys etc.

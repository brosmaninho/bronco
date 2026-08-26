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
    2. OCR nas regioes configuradas (tooltips, dialogos)
    3. Busca traducao nos dicionarios (com cache LRU)
    4. Renderiza traducao via Dear ImGui overlay
    5. Chama Present() original
```

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

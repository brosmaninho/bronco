# Gerador de Dicionarios PT-BR (Bronco)

Ferramenta standalone em Python que gera os dicionarios de traducao PT-BR do
Bronco a partir da **API REST oficial do Guild Wars 2** (`api.guildwars2.com/v2`)
e traduz de ingles para portugues usando um **modelo local offline**.

> **Conformidade ArenaNet:** o uso da API REST publica e **explicitamente
> permitido** pelo documento de restricoes da ArenaNet (categoria "Paineis que
> consomem a API REST oficial via chave publica"). Esta ferramenta esta 100%
> dentro das regras.

Esta ferramenta **NAO** faz parte do build C++/MSVC nem roda em CI. E um
utilitario manual, executado pelo maintainer, cuja unica saida sao os arquivos
JSON em `data/dictionaries/pt-br/`.

## Opcao C: traducao offline (Argos Translate)

A traducao usa [Argos Translate](https://www.argosopentech.com/): **gratis, sem
chave de API, sem limites de uso e 100% local/offline**. O modelo `en->pt` e
baixado uma unica vez (~100MB+) e reutilizado.

A camada de **busca de dados** (GW2 API) e **desacoplada** da camada de
**traducao** (Argos). Isso permite:

- Rodar `--dry-run` e `--no-translate` **sem** `argostranslate` instalado.
- Gerar dados brutos em ingles mesmo sem o modelo de traducao.
- A traducao so falha (com erro claro) quando de fato tentada sem o modelo.

## Estrutura

```
tools/dictionary-generator/
  gw2_api.py                # busca IDs + detalhes em lotes <=200, rate limit, retry, cache
  translator.py            # wrapper offline do Argos (import preguicoso) + cache de traducao
  generate_dictionaries.py # CLI principal (pipeline + self-checks)
  requirements.txt
  README.md
  cache/                   # gitignored: cache bruto da API, cache de traducao, modelo Argos
```

## Setup

```bash
# 1. Ambiente virtual
python3 -m venv .venv
source .venv/bin/activate            # Windows: .venv\Scripts\activate

# 2. Dependencias
pip install -r tools/dictionary-generator/requirements.txt

# 3. Modelo de traducao offline en->pt (baixa ~100MB+ uma vez)
python3 tools/dictionary-generator/generate_dictionaries.py --install-model
```

## Uso

```bash
# Self-checks offline (sem rede, sem Argos). Sai 0 se tudo passar.
python3 tools/dictionary-generator/generate_dictionaries.py --dry-run

# Amostra pequena de skills sem traduzir (demo de estrutura, sem Argos)
python3 tools/dictionary-generator/generate_dictionaries.py \
    --category skills --limit 50 --no-translate

# Skills completas, traduzindo (requer modelo instalado)
python3 tools/dictionary-generator/generate_dictionaries.py --category skills

# Todas as categorias
python3 tools/dictionary-generator/generate_dictionaries.py --category all

# Itens (dataset gigante, ~90k IDs) - roda por horas
python3 tools/dictionary-generator/generate_dictionaries.py --category items
```

### Opcoes

| Flag | Descricao |
| --- | --- |
| `--category {skills,items,traits,specializations,professions,pets,masteries,all}` | Categoria a gerar (padrao: `skills`). |
| `--limit N` | Limita a quantidade de IDs (amostragem). |
| `--install-model` | Baixa/instala o modelo Argos `en->pt` e sai. |
| `--output-dir DIR` | Diretorio de saida (padrao: `data/dictionaries/pt-br`). |
| `--cache-dir DIR` | Diretorio de cache (padrao: `tools/dictionary-generator/cache`). |
| `--min-delay S` | Delay minimo entre requests (rate limit, padrao 0.25s). |
| `--no-translate` | Nao traduz; `translated` recebe o ingles (demo de estrutura). |
| `--dry-run` | Roda self-checks offline e sai. |

## Idempotente e resumivel

Todo I/O de rede e traducao e **cacheado em disco**:

- Lotes brutos da API: `cache/raw/<endpoint>/`.
- Traducoes ja feitas: `cache/translations.json`.

Se o processo parar no meio, basta rodar de novo: ele **continua de onde parou**,
reaproveitando o cache e sem re-baixar nem re-traduzir. O cache e gitignored.

## Formato de saida

Compativel byte a byte com o loader C++ (`src/translation/dictionary.cpp`):

```json
{
    "locale": "pt-br",
    "category": "skills",
    "version": "1.0.0",
    "entries": [
        { "en": "Fireball", "translated": "Bola de Fogo" }
    ]
}
```

- UTF-8, `ensure_ascii=false`, indentacao de 4 espacos.
- Dedup por chave normalizada (minuscula) para bater com o lookup
  case-insensitive do C++; nomes vazios sao descartados.

## Rate limiting e resiliencia

- Limite da API: ~300 req/min. O cliente aplica um delay minimo configuravel
  (padrao 0.25s => <= 240 req/min).
- Lotes de detalhes: no maximo **200 IDs** por request (limite da API).
- Retry com **backoff exponencial** em HTTP 429/5xx e erros de conexao,
  honrando o header `Retry-After` quando presente.

## Tempo, disco e memoria

| Categoria | IDs aprox. | Tempo aprox. |
| --- | --- | --- |
| skills | ~alguns milhares | minutos |
| traits, specializations, professions, pets, masteries | dezenas a centenas | segundos a minutos |
| items | ~90.000 | horas (long-running) |

- **Disco:** cache bruto da API (dezenas a centenas de MB para `items`) +
  modelo Argos `en->pt` (~100MB+). Tudo em `cache/` (gitignored).
- **Memoria:** modesta; os dados sao processados em lotes. O modelo Argos usa
  algumas centenas de MB de RAM durante a traducao.

## CI

Esta ferramenta **NAO** roda em CI e **NAO** faz parte do build C++/MSVC. E
executada manualmente pelo maintainer para (re)gerar os dicionarios. Veja
tambem `docs/ADDING_LANGUAGES.md`.

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
                            # + extract_skill_tooltip / strip_markup (tooltip estruturado)
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

> **Nota (Python 3.9):** no Python 3.9 o `pip install -r requirements.txt`
> falha, porque o `argostranslate` puxa o `spacy` mais recente, cujo build de
> `thinc` exige Python >= 3.10. Solucao no Python 3.9: instale um `spacy`
> compativel **antes**:
>
> ```bash
> pip install "spacy<3.8"
> pip install -r tools/dictionary-generator/requirements.txt
> ```
>
> No **Python 3.10+** o `pip install -r requirements.txt` funciona
> diretamente, sem esse passo extra.

## Uso

```bash
# Self-checks offline (sem rede, sem Argos). Sai 0 se tudo passar.
python3 tools/dictionary-generator/generate_dictionaries.py --dry-run

# Amostra pequena de skills sem traduzir (demo de estrutura, sem Argos)
python3 tools/dictionary-generator/generate_dictionaries.py \
    --category skills --limit 50 --no-translate

# Skills completas, traduzindo (requer modelo instalado)
python3 tools/dictionary-generator/generate_dictionaries.py --category skills

# Todas as categorias (inclui skill_tooltips)
python3 tools/dictionary-generator/generate_dictionaries.py --category all

# Itens (dataset gigante, ~90k IDs) - roda por horas
python3 tools/dictionary-generator/generate_dictionaries.py --category items
```

### Opcoes

| Flag | Descricao |
| --- | --- |
| `--category {skills,items,traits,specializations,professions,pets,masteries,skill_tooltips,all}` | Categoria a gerar (padrao: `skills`). `skill_tooltips` produz o dataset de tooltip + `fact_labels.json`. |
| `--limit N` | Limita a quantidade de IDs (amostragem). |
| `--install-model` | Baixa/instala o modelo Argos `en->pt` e sai. |
| `--output-dir DIR` | Diretorio de saida dos dicionarios, incl. `fact_labels.json` (padrao: `data/dictionaries/pt-br`). |
| `--skilldata-output-dir DIR` | Diretorio de saida do dataset de tooltip de skills (padrao: `data/skilldata/pt-br`). |
| `--cache-dir DIR` | Diretorio de cache (padrao: `tools/dictionary-generator/cache`). |
| `--min-delay S` | Delay minimo entre requests (rate limit, padrao 0.25s). |
| `--no-translate` | Nao traduz; `translated`/campos traduzidos recebem o ingles (demo de estrutura). |
| `--no-descriptions` | Gera apenas nomes. Por padrao a ferramenta traduz nomes **E** descricoes. |
| `--dry-run` | Roda self-checks offline e sai. |

## Reconstrucao de tooltip de skills (Opcao B)

A categoria `skill_tooltips` gera dois arquivos que permitem ao Bronco
**reconstruir o tooltip COMPLETO de uma skill em PT-BR** a partir apenas do
**nome** identificado por OCR:

1. `data/skilldata/pt-br/skills_tooltips.json` — dataset estruturado por skill
   (nome, tipo, descricao, flags, categorias e a lista completa de `facts` com
   seus valores e frequencias).
2. `data/dictionaries/pt-br/fact_labels.json` — dicionario dos rotulos de facts
   distintos (ex.: `Damage` -> `Dano`), no **mesmo** schema do loader C++.

### Mapa curado de rotulos

Antes de recorrer ao Argos, um **mapa curado** garante a terminologia PT-BR
correta dos rotulos e status mais comuns (ex.: `Damage`->`Dano`,
`Number of Targets`->`Número de Alvos`, `Recharge`->`Recarga`, `Might`->`Poder`,
`Regeneration`->`Regeneração`, ...). O helper `translate_label` aplica o mapa de
forma **case-insensitive** sobre o rotulo ja limpo; so cai no Argos quando o
rotulo nao esta no mapa (e nao passa por Argos em `--no-translate`).

### Tratamento de dados reais

- Rotulos com marcacao tipo HTML (ex.: `<c=@abilitytype>Field Damage</c>`) sao
  limpos com `strip_markup` antes de traduzir/armazenar (`-> Field Damage`).
- Tipos de fact **nao listados** (ex.: `BuffArray` ou tipo vazio) sao
  preservados **verbatim** em `type` e nunca quebram o gerador.
- Facts `Buff`/`PrefixedBuff` carregam o termo relevante em `status` (ex.:
  Might, Crippled, Regeneration) alem de `duration`, `apply_count` e
  `description`; `PrefixedBuff` inclui um `prefix` aninhado.
- URLs de `icon` **nao** sao armazenadas.

### Schema de `skills_tooltips.json`

```json
{
    "locale": "pt-br",
    "category": "skills_tooltips",
    "version": "1.0.0",
    "skills": [
        {
            "id": 14384,
            "name_en": "Hammer Bash",
            "name": "Pancada de Martelo",
            "type": "Weapon",
            "next_chain": 14385,
            "prev_chain": 14358,
            "description": "...",
            "flags": ["NoUnderwater"],
            "categories": ["Glyph"],
            "facts": [
                { "type": "Recharge", "label_en": "Recharge", "label": "Recarga", "value": 20 },
                {
                    "type": "PrefixedBuff",
                    "label_en": "Apply Buff/Condition",
                    "label": "Apply Buff/Condition",
                    "duration": 20,
                    "apply_count": 3,
                    "status_en": "Might",
                    "status": "Poder",
                    "description": "...",
                    "prefix": { "text": "...", "status_en": "Fire Attunement", "status": "...", "description": "..." }
                }
            ]
        }
    ]
}
```

- Skills sao deduplicadas pela chave normalizada de `name_en` (primeira
  ocorrencia vence; nomes vazios descartados).
- `id` (inteiro) esta **sempre** presente: e o id da skill no `/v2/skills` e a
  chave usada para reconstruir cadeias de skills.
- `next_chain` e `prev_chain` (inteiros) sao **opcionais**: aparecem **apenas**
  para skills que fazem parte de uma cadeia. `next_chain` e o id da proxima
  skill da cadeia e `prev_chain` o id da anterior. Skills fora de cadeia **nao**
  tem essas chaves (elas sao omitidas, nunca inventadas), espelhando a forma
  opcional da API.
- Cada fact preserva `type` (verbatim), `label_en`/`label` (rotulo limpo EN +
  traduzido) e todos os campos de valor presentes: `value`, `duration`,
  `distance`, `percent`, `hit_count`, `dmg_multiplier`, `apply_count`, `status`
  (+ `status_en`), `description`, `field_type`, `finisher_type`, `target`.
- UTF-8, `ensure_ascii=false`, indentacao de 4 espacos, newline final.

### Skills encadeadas (chains)

Algumas skills ocorrem **encadeadas**: uma leva a proxima quando ativada em
sequencia (ex.: `Hammer Swing` -> `Hammer Bash` -> `Hammer Smash`). No GW2 isso
e modelado pelos campos opcionais `next_chain` (id da proxima skill) e
`prev_chain` (id da anterior) de cada objeto `/v2/skills`. Uma skill fora de
cadeia nao tem nenhum dos dois.

Para exibir a cadeia **inteira** a partir de qualquer skill identificada por
OCR, reconstruimos a cadeia por id:

1. Partindo do membro identificado, ande por `prev_chain` para tras ate a
   **cabeca** da cadeia (o membro que nao tem `prev_chain`).
2. A partir da cabeca, avance por `next_chain` para frente, coletando cada id
   em ordem, ate o **fim** (o membro que nao tem `next_chain`).

O resultado e a lista ordenada de ids da cadeia, valida a partir de qualquer
membro de origem. A implementacao de referencia e a funcao pura
`reconstruct_chain(skills_by_id, start_id)` em `generate_dictionaries.py` (o lado
C++ do Bronco espelha esse mesmo algoritmo). Skills isoladas (sem cadeia)
retornam uma lista com apenas o seu proprio id.

### Comandos exatos

```bash
# (1) Self-checks offline (sem rede, sem Argos)
python3 tools/dictionary-generator/generate_dictionaries.py --dry-run

# (2) Amostra offline SEM traducao (usa o cache bruto ja populado)
python3 tools/dictionary-generator/generate_dictionaries.py \
    --category skill_tooltips --limit 400 --no-translate

# (3) Dataset COMPLETO traduzido (requer modelo Argos instalado)
python3 tools/dictionary-generator/generate_dictionaries.py --category skill_tooltips
```

## Idempotente e resumivel

Todo I/O de rede e traducao e **cacheado em disco**:

- Lotes brutos da API: `cache/raw/<endpoint>/`.
- Traducoes ja feitas: `cache/translations.json`.

Se o processo parar no meio, basta rodar de novo: ele **continua de onde parou**,
reaproveitando o cache e sem re-baixar nem re-traduzir. O cache e gitignored.

## Nomes e descricoes

Por padrao a ferramenta traduz **tanto o `name` quanto a `description`** de cada
objeto da API. Cada texto em ingles vira uma entrada `{en, translated}`
independente no **mesmo** dicionario: o nome e a descricao sao, cada um, um par
`en`/traducao proprio. Isso mantem o loader C++ **inalterado** (ele continua
lendo `entries[]` com `{en, translated}`; nao ha campo novo). Descricoes vazias
sao descartadas e a deduplicacao por chave normalizada garante que um mesmo
texto (nome ou descricao) nao apareca duas vezes.

Use `--no-descriptions` para gerar apenas nomes (o comportamento antigo).

## Formato de saida

Compativel byte a byte com o loader C++ (`src/translation/dictionary.cpp`):

```json
{
    "locale": "pt-br",
    "category": "skills",
    "version": "1.0.0",
    "entries": [
        { "en": "Fireball", "translated": "Bola de Fogo" },
        { "en": "Throw gunk at target area to inflict a random condition.",
          "translated": "Atirar o lixo para a área do alvo para infligir uma condição aleatória." }
    ]
}
```

- UTF-8, `ensure_ascii=false`, indentacao de 4 espacos.
- Nomes **e** descricoes usam o mesmo formato `{en, translated}`; nao ha campo
  extra, entao o loader C++ nao muda.
- Dedup por chave normalizada (minuscula) para bater com o lookup
  case-insensitive do C++; textos vazios sao descartados.

## Rate limiting e resiliencia

- Limite da API: ~300 req/min. O cliente aplica um delay minimo configuravel
  (padrao 0.25s => <= 240 req/min).
- Lotes de detalhes: no maximo **200 IDs** por request (limite da API).
- Retry com **backoff exponencial** em HTTP 429/5xx e erros de conexao,
  honrando o header `Retry-After` quando presente (aceita tanto a forma
  numerica em segundos quanto a forma HTTP-date da RFC 7231).

## Tempo, disco e memoria

| Categoria | IDs aprox. | Tempo aprox. (nomes + descricoes) |
| --- | --- | --- |
| skills | ~4.700 IDs (~6.000 textos unicos) | ~5 min (traducao a frio) / ~0,2s (tudo em cache) |
| traits, specializations, professions, pets, masteries | dezenas a centenas | segundos a minutos |
| items | ~90.000 | horas (long-running) |

> Traduzir descricoes praticamente dobra o volume de traducao em relacao a so
> nomes. Use `--no-descriptions` se quiser apenas os nomes (mais rapido).

- **Disco:** cache bruto da API (dezenas a centenas de MB para `items`) +
  modelo Argos `en->pt` (~100MB+). Tudo em `cache/` (gitignored).
- **Memoria:** modesta; os dados sao processados em lotes. O modelo Argos usa
  algumas centenas de MB de RAM durante a traducao.

## CI

Esta ferramenta **NAO** roda em CI e **NAO** faz parte do build C++/MSVC. E
executada manualmente pelo maintainer para (re)gerar os dicionarios. Veja
tambem `docs/ADDING_LANGUAGES.md`.

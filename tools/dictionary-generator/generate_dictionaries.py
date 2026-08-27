#!/usr/bin/env python3
"""
Gerador de dicionarios de traducao PT-BR para o Bronco.

Pipeline por categoria:
  1. Busca a lista de IDs em /v2/<endpoint> (cacheada).
  2. Busca detalhes em lotes de <= 200 via /v2/<endpoint>?ids=...&lang=en (cacheado).
  3. Para cada nome em ingles unico (dedup por chave normalizada/minuscula),
     traduz para PT-BR via Argos Translate offline (cacheado).
  4. Monta o dicionario no schema que o loader C++ do Bronco espera.
  5. Escreve o JSON em data/dictionaries/pt-br/<categoria>.json.

Fonte de dados: API REST oficial do GW2 (api.guildwars2.com/v2), uso
EXPLICITAMENTE permitido pela ArenaNet (paineis que consomem a API REST publica).

Traducao: Opcao C (Argos Translate) - offline, gratis, sem chave, sem limites.

Modos que NAO exigem rede nem Argos:
  --dry-run       roda self-checks offline e sai (0 = ok, != 0 = falha)
  --no-translate  monta a estrutura com 'translated' = ingles (passthrough)

Idempotente e resumivel: todo I/O de rede e traducao e cacheado em disco.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Dict, List

# Imports locais (o script e pensado para rodar de qualquer cwd).
sys.path.insert(0, str(Path(__file__).resolve().parent))

import gw2_api  # noqa: E402
import translator  # noqa: E402

TOOL_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOL_DIR.parent.parent
DEFAULT_CACHE_DIR = TOOL_DIR / "cache"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "data/dictionaries/pt-br"
DEFAULT_SKILLDATA_OUTPUT_DIR = REPO_ROOT / "data/skilldata/pt-br"
VERSION = "1.0.0"
LOCALE = "pt-br"

# Categoria virtual (nao e um endpoint da API) que produz o dataset de tooltip
# de skills + o dicionario de rotulos de facts.
SKILL_TOOLTIPS_CATEGORY = "skill_tooltips"

# Categoria -> nome de arquivo de saida.
CATEGORY_FILENAMES = {
    "skills": "skills.json",
    "items": "items.json",
    "traits": "traits.json",
    "specializations": "specializations.json",
    "professions": "professions.json",
    "pets": "pets.json",
    "masteries": "masteries.json",
    "fact_labels": "fact_labels.json",
}

# Mapa curado de rotulos de facts EN -> PT-BR para garantir terminologia correta.
# Aplicado ANTES de recorrer ao Argos. Chaves comparadas de forma
# case-insensitive sobre o rotulo ja limpo (sem marcacao).
CURATED_LABELS = {
    "Damage": "Dano",
    "Number of Targets": "Número de Alvos",
    "Duration": "Duração",
    "Recharge": "Recarga",
    "Range": "Alcance",
    "Radius": "Raio",
    "Combo Field": "Campo de Combo",
    "Combo Finisher": "Finalizador de Combo",
    "Healing": "Cura",
    "Might": "Poder",
    "Fury": "Fúria",
    "Vulnerability": "Vulnerabilidade",
    "Regeneration": "Regeneração",
    "Movement Speed": "Velocidade de Movimento",
    "Bleeding": "Sangramento",
    "Burning": "Queimadura",
    "Poison": "Veneno",
    "Chilled": "Congelamento",
    "Crippled": "Aleijamento",
    "Immobile": "Imobilizado",
    "Weakness": "Fraqueza",
    "Stun": "Atordoamento",
    "Daze": "Tontura",
    "Protection": "Proteção",
    "Aegis": "Égide",
    "Quickness": "Rapidez",
    "Stability": "Estabilidade",
    "Swiftness": "Ligeireza",
    "Vigor": "Vigor",
    "Resistance": "Resistência",
    "Torment": "Tormento",
    "Confusion": "Confusão",
    "Number of Pulses": "Número de Pulsos",
    "Count Recharge": "Recarga de Carga",
    "Combo": "Combo",
    "Distance": "Distância",
    "Pulses": "Pulsos",
    "Cast Time": "Tempo de Conjuração",
    "Activation": "Ativação",
}

# Indice case-insensitive do mapa curado (chave normalizada -> PT-BR).
_CURATED_LABELS_CI = {k.strip().lower(): v for k, v in CURATED_LABELS.items()}


# ---------------------------------------------------------------------- #
# Builder do dicionario
# ---------------------------------------------------------------------- #
def normalize_key(text: str) -> str:
    """Mesma normalizacao do loader C++: minusculas + trim."""
    return (text or "").strip().lower()


def build_dictionary(category: str, pairs: List[tuple]) -> dict:
    """Monta o dict no schema exato do loader C++.

    `pairs` e uma lista de (en, translated). Dedup por chave normalizada
    (minuscula) para bater com o lookup case-insensitive do C++; nomes vazios
    sao descartados; a primeira ocorrencia de cada chave vence.
    """
    seen = set()
    entries = []
    for en, translated in pairs:
        en = (en or "").strip()
        if not en:
            continue
        key = normalize_key(en)
        if key in seen:
            continue
        seen.add(key)
        entries.append({"en": en, "translated": (translated or "").strip()})

    return {
        "locale": LOCALE,
        "category": category,
        "version": VERSION,
        "entries": entries,
    }


def write_dictionary(output_dir: Path, category: str, dictionary: dict) -> Path:
    filename = CATEGORY_FILENAMES.get(category, f"{category}.json")
    out_path = Path(output_dir) / filename
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as fh:
        json.dump(dictionary, fh, ensure_ascii=False, indent=4)
        fh.write("\n")
    return out_path


def write_skilldata(output_dir: Path, dataset: dict) -> Path:
    """Escreve o dataset de tooltip de skills (espelha write_dictionary)."""
    out_path = Path(output_dir) / "skills_tooltips.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as fh:
        json.dump(dataset, fh, ensure_ascii=False, indent=4)
        fh.write("\n")
    return out_path


def translate_label(label_en: str, trans, no_translate: bool) -> str:
    """Traduz um rotulo/status de fact.

    Ordem: (1) mapa curado (case-insensitive sobre o rotulo ja limpo);
    (2) Argos via trans.translate quando nao em no_translate; (3) passthrough
    do proprio label_en. Aceita None (retorna '').
    """
    label_en = (label_en or "").strip()
    if not label_en:
        return ""
    curated = _CURATED_LABELS_CI.get(label_en.lower())
    if curated is not None:
        return curated
    if not no_translate and trans is not None:
        return trans.translate(label_en)
    return label_en


# ---------------------------------------------------------------------- #
# Pipeline
# ---------------------------------------------------------------------- #
def _progress(iterable, desc: str, total=None):
    try:
        from tqdm import tqdm

        return tqdm(iterable, desc=desc, total=total)
    except ImportError:
        return iterable


def generate_category(
    category: str,
    client: gw2_api.GW2Client,
    trans: translator.Translator,
    output_dir: Path,
    limit: int = 0,
    no_translate: bool = False,
    include_descriptions: bool = True,
    batch_size: int = gw2_api.DEFAULT_BATCH_SIZE,
) -> dict:
    print(f"[{category}] buscando lista de IDs...")
    ids = client.fetch_id_list(category)
    if limit and limit > 0:
        ids = ids[:limit]
    print(f"[{category}] {len(ids)} IDs (limit={limit or 'nenhum'})")

    # Coleta textos unicos em ingles (nomes e, por padrao, descricoes),
    # preservando a ordem/primeira ocorrencia. Cada texto vira uma entrada
    # {en, translated} independente, mantendo o loader C++ inalterado: nomes
    # e descricoes compartilham o mesmo formato de par en/traducao.
    n_batches = len(gw2_api.chunk_ids(ids, batch_size))
    progress = lambda batches: _progress(  # noqa: E731
        batches, f"[{category}] baixando lotes", total=n_batches
    )

    seen_keys = set()
    english_texts: List[str] = []
    fetched = 0
    n_names = 0
    n_descs = 0
    # iter_details e o UNICO caminho de batching de detalhes (sem duplicar loop).
    for obj in client.iter_details(category, ids, lang="en", batch_size=batch_size, progress=progress):
        fetched += 1
        fields = gw2_api.extract_text_fields(obj)
        candidates = [("name", fields["name"])]
        if include_descriptions:
            candidates.append(("description", fields["description"]))
        for kind, text in candidates:
            if not text:
                continue
            key = normalize_key(text)
            if key in seen_keys:
                continue
            seen_keys.add(key)
            english_texts.append(text)
            if kind == "name":
                n_names += 1
            else:
                n_descs += 1

    desc_note = f", {n_descs} descricoes" if include_descriptions else " (descricoes desativadas)"
    print(f"[{category}] {fetched} objetos, {len(english_texts)} textos unicos ({n_names} nomes{desc_note})")

    pairs: List[tuple] = []
    if no_translate:
        pairs = [(text, text) for text in english_texts]
    else:
        for text in _progress(english_texts, f"[{category}] traduzindo", total=len(english_texts)):
            pairs.append((text, trans.translate(text)))
        if trans.cache is not None:
            trans.cache.save()

    dictionary = build_dictionary(category, pairs)
    out_path = write_dictionary(output_dir, category, dictionary)
    print(f"[{category}] escrito: {out_path} ({len(dictionary['entries'])} entradas)")
    return dictionary


def build_skill_tooltips_dataset(skills: List[dict]) -> dict:
    """Monta o dataset de tooltip de skills no schema esperado pelo lado C++.

    Faz dedup por chave normalizada de name_en (primeira ocorrencia vence;
    nomes vazios descartados). Puro/sem rede: usado tambem pelos self-checks.
    """
    seen = set()
    deduped = []
    for skill in skills:
        name_en = (skill.get("name_en") or "").strip()
        if not name_en:
            continue
        key = normalize_key(name_en)
        if key in seen:
            continue
        seen.add(key)
        deduped.append(skill)
    return {
        "locale": LOCALE,
        "category": "skills_tooltips",
        "version": VERSION,
        "skills": deduped,
    }


def generate_skill_tooltips(
    client: gw2_api.GW2Client,
    trans,
    skilldata_output_dir: Path,
    fact_labels_output_dir: Path,
    limit: int = 0,
    no_translate: bool = False,
    batch_size: int = gw2_api.DEFAULT_BATCH_SIZE,
) -> dict:
    """Gera skills_tooltips.json + fact_labels.json a partir do cache de skills."""
    print("[skill_tooltips] buscando lista de IDs...")
    ids = client.fetch_id_list("skills")
    if limit and limit > 0:
        ids = ids[:limit]
    print(f"[skill_tooltips] {len(ids)} IDs (limit={limit or 'nenhum'})")

    n_batches = len(gw2_api.chunk_ids(ids, batch_size))
    progress = lambda batches: _progress(  # noqa: E731
        batches, "[skill_tooltips] baixando lotes", total=n_batches
    )

    def _tr(label: str) -> str:
        return translate_label(label, trans, no_translate)

    # Acumula o mapa de rotulos de facts distintos (label_en -> traduzido).
    label_map: Dict[str, str] = {}

    def _record_label(label_en: str) -> str:
        label_en = (label_en or "").strip()
        translated = _tr(label_en)
        if label_en and label_en not in label_map:
            label_map[label_en] = translated
        return translated

    skills: List[dict] = []
    fetched = 0
    for obj in client.iter_details(
        "skills", ids, lang="en", batch_size=batch_size, progress=progress
    ):
        fetched += 1
        tip = gw2_api.extract_skill_tooltip(obj)
        name_en = tip["name"]
        if not name_en:
            continue

        facts_out = []
        for fact in tip["facts"]:
            fact_out = dict(fact)  # copia rasa; preserva campos de valor
            label_en = fact.get("text", "")
            fact_out["label_en"] = label_en
            fact_out["label"] = _record_label(label_en)
            # Buff/PrefixedBuff: status carrega o termo relevante (Might, etc.).
            if fact.get("status") is not None:
                fact_out["status_en"] = fact.get("status", "")
                fact_out["status"] = _tr(fact.get("status", ""))
            if fact.get("description") is not None:
                fact_out["description"] = (
                    _tr(fact["description"]) if fact["description"] else ""
                )
            prefix = fact.get("prefix")
            if isinstance(prefix, dict):
                fact_out["prefix"] = {
                    "text": prefix.get("text", ""),
                    "status_en": prefix.get("status", ""),
                    "status": _tr(prefix.get("status", "")),
                    "description": (
                        _tr(prefix["description"]) if prefix.get("description") else ""
                    ),
                }
            facts_out.append(fact_out)

        skills.append(
            {
                "name_en": name_en,
                "name": _tr(name_en),
                "type": _tr(tip["type"]) if tip["type"] else "",
                "description": (
                    _tr(tip["description"]) if tip["description"] else ""
                ),
                "flags": tip["flags"],
                "categories": tip["categories"],
                "facts": facts_out,
            }
        )

    if not no_translate and trans is not None and trans.cache is not None:
        trans.cache.save()

    dataset = build_skill_tooltips_dataset(skills)
    skilldata_path = write_skilldata(skilldata_output_dir, dataset)
    print(
        f"[skill_tooltips] escrito: {skilldata_path} "
        f"({len(dataset['skills'])} skills, {fetched} objetos)"
    )

    # Dicionario de rotulos de facts no MESMO schema do loader C++.
    pairs = [(en, translated) for en, translated in label_map.items()]
    fact_dict = build_dictionary("fact_labels", pairs)
    fact_labels_path = write_dictionary(fact_labels_output_dir, "fact_labels", fact_dict)
    print(
        f"[skill_tooltips] escrito: {fact_labels_path} "
        f"({len(fact_dict['entries'])} rotulos de facts)"
    )
    return dataset


# ---------------------------------------------------------------------- #
# Self-checks offline (--dry-run): sem rede, sem Argos
# ---------------------------------------------------------------------- #
def run_self_checks() -> int:
    failures = []

    def check(name: str, cond: bool):
        status = "OK" if cond else "FALHOU"
        print(f"  [{status}] {name}")
        if not cond:
            failures.append(name)

    print("Rodando self-checks offline (sem rede, sem Argos)...")

    # (a) Schema do dicionario a partir de amostra em memoria.
    sample = [("Fireball", "Bola de Fogo"), ("Meteor Shower", "Chuva de Meteoros")]
    d = build_dictionary("skills", sample)
    schema_ok = (
        d.get("locale") == LOCALE
        and d.get("category") == "skills"
        and d.get("version") == VERSION
        and isinstance(d.get("entries"), list)
        and len(d["entries"]) == 2
        and all(
            isinstance(e.get("en"), str)
            and e["en"]
            and isinstance(e.get("translated"), str)
            and e["translated"]
            for e in d["entries"]
        )
    )
    check("schema do dicionario (locale/category/version/entries + en/translated)", schema_ok)

    # (b) Round-trip do cache de traducao.
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        cache_path = Path(tmp) / "translations.json"
        c1 = translator.TranslationCache(cache_path)
        c1.put("Fireball", "Bola de Fogo")
        c1.put("Água", "Água")  # verifica UTF-8
        c1.save()
        c2 = translator.TranslationCache(cache_path)
        roundtrip_ok = c2.get("Fireball") == "Bola de Fogo" and c2.get("Água") == "Água"
    check("round-trip do cache de traducao (UTF-8)", roundtrip_ok)

    # (c) Batching em lotes de <= 200 sem perda.
    ids = list(range(1, 1005))  # 1004 ids
    batches = gw2_api.chunk_ids(ids, 200)
    reassembled = [i for b in batches for i in b]
    batch_ok = (
        all(len(b) <= gw2_api.MAX_IDS_PER_BATCH for b in batches)
        and len(batches) == 6
        and reassembled == ids
    )
    check("batching <= 200 divide e reassembla sem perda", batch_ok)

    # (d) Dedup por chave normalizada (case-insensitive) descarta duplicatas.
    pairs = [
        ("Fireball", "Bola de Fogo"),
        ("fireball", "IGNORADO"),  # duplicata por caso
        ("FIREBALL", "IGNORADO2"),
        ("", "vazio deve sumir"),  # nome vazio descartado
        ("Meteor Shower", "Chuva de Meteoros"),
    ]
    dd = build_dictionary("skills", pairs)
    dedup_ok = (
        len(dd["entries"]) == 2
        and dd["entries"][0]["en"] == "Fireball"
        and dd["entries"][0]["translated"] == "Bola de Fogo"
        and dd["entries"][1]["en"] == "Meteor Shower"
    )
    check("dedup por chave normalizada descarta duplicatas de caso e nomes vazios", dedup_ok)

    # (e) Translator importa sem argostranslate; so falha ao traduzir de fato.
    lazy_ok = True
    try:
        t = translator.Translator(cache=None)  # construcao nao deve exigir Argos
    except Exception:  # noqa: BLE001
        lazy_ok = False
    check("Translator instancia sem exigir argostranslate (import preguicoso)", lazy_ok)

    # (f) Nomes E descricoes viram entradas {en, translated} separadas.
    #     Simula dois objetos: name+description; cada texto nao-vazio e unico
    #     deve produzir uma entrada. Descricao vazia e descartada.
    objs = [
        {"id": 1, "name": "Fireball", "description": "Deals fire damage."},
        {"id": 2, "name": "Ice Shard", "description": ""},  # descricao vazia
        {"id": 3, "name": "Fireball", "description": "Deals fire damage."},  # dup
    ]
    seen_keys = set()
    texts = []
    for obj in objs:
        fields = gw2_api.extract_text_fields(obj)
        for text in (fields["name"], fields["description"]):
            if not text:
                continue
            key = normalize_key(text)
            if key in seen_keys:
                continue
            seen_keys.add(key)
            texts.append(text)
    df = build_dictionary("skills", [(t2, t2) for t2 in texts])
    ens = {e["en"] for e in df["entries"]}
    desc_ok = (
        len(df["entries"]) == 3
        and "Fireball" in ens
        and "Ice Shard" in ens
        and "Deals fire damage." in ens
        and all(e["en"] and e["translated"] for e in df["entries"])
    )
    check("nomes e descricoes viram entradas {en, translated} (dedup + descarta vazias)", desc_ok)

    # (g) Retry-After: aceita forma numerica e HTTP-date (RFC 7231).
    from datetime import datetime, timedelta, timezone

    future = datetime.now(timezone.utc) + timedelta(seconds=120)
    http_date = future.strftime("%a, %d %b %Y %H:%M:%S GMT")
    ra_numeric = gw2_api._parse_retry_after("30")
    ra_date = gw2_api._parse_retry_after(http_date)
    ra_none = gw2_api._parse_retry_after(None)
    ra_junk = gw2_api._parse_retry_after("not-a-date")
    retry_after_ok = (
        ra_numeric == 30.0
        and ra_date is not None
        and 60.0 <= ra_date <= 130.0
        and ra_none is None
        and ra_junk is None
    )
    check("Retry-After interpreta segundos e HTTP-date (RFC 7231)", retry_after_ok)

    # (h) strip_markup remove marcacao tipo HTML mantendo o texto interno.
    strip_ok = (
        gw2_api.strip_markup("<c=@abilitytype>Field Damage</c>") == "Field Damage"
        and gw2_api.strip_markup("Plain") == "Plain"
        and gw2_api.strip_markup(None) == ""
    )
    check("strip_markup remove '<...>' e mantem o texto interno", strip_ok)

    # (i) extract_skill_tooltip com tipos de fact mistos (incluindo um NAO listado
    #     'BuffArray', um Buff com status/duration/apply_count e um PrefixedBuff
    #     com prefixo aninhado): estrutura normalizada, valores preservados,
    #     marcacao removida e SEM chaves 'icon'.
    sample_skill = {
        "id": 42,
        "name": "Test Skill",
        "type": "Weapon",
        "description": "A test.",
        "flags": ["NoUnderwater"],
        "categories": ["Arcane"],
        "facts": [
            {
                "text": "<c=@abilitytype>Field Damage</c>",
                "type": "Damage",
                "icon": "https://example/icon.png",
                "hit_count": 1,
                "dmg_multiplier": 0.9,
            },
            {"type": "BuffArray", "icon": "https://example/ba.png"},
            {
                "text": "Apply Buff/Condition",
                "type": "Buff",
                "icon": "https://example/b.png",
                "duration": 10,
                "status": "Might",
                "apply_count": 3,
                "description": "Increased outgoing damage.",
            },
            {
                "text": "Apply Buff/Condition",
                "type": "PrefixedBuff",
                "icon": "https://example/pb.png",
                "duration": 20,
                "status": "Regeneration",
                "apply_count": 1,
                "description": "Gain health.",
                "prefix": {
                    "text": "Apply Buff/Condition",
                    "icon": "https://example/pfx.png",
                    "status": "Water Attunement",
                    "description": "Cast water spells.",
                },
            },
        ],
    }
    tip = gw2_api.extract_skill_tooltip(sample_skill)

    def _no_icon(d):
        if isinstance(d, dict):
            return "icon" not in d and all(_no_icon(v) for v in d.values())
        if isinstance(d, list):
            return all(_no_icon(v) for v in d)
        return True

    f_damage, f_ba, f_buff, f_pbuff = tip["facts"]
    extract_ok = (
        tip["id"] == 42
        and tip["type"] == "Weapon"
        and tip["flags"] == ["NoUnderwater"]
        and tip["categories"] == ["Arcane"]
        and len(tip["facts"]) == 4
        # markup removida do rotulo
        and f_damage["text"] == "Field Damage"
        and f_damage["type"] == "Damage"
        and f_damage["hit_count"] == 1
        and f_damage["dmg_multiplier"] == 0.9
        # tipo nao listado preservado verbatim, sem crash
        and f_ba["type"] == "BuffArray"
        # Buff carrega status/duration/apply_count/description
        and f_buff["type"] == "Buff"
        and f_buff["status"] == "Might"
        and f_buff["duration"] == 10
        and f_buff["apply_count"] == 3
        and f_buff["description"] == "Increased outgoing damage."
        # PrefixedBuff com prefixo aninhado
        and f_pbuff["type"] == "PrefixedBuff"
        and f_pbuff["status"] == "Regeneration"
        and isinstance(f_pbuff.get("prefix"), dict)
        and f_pbuff["prefix"]["status"] == "Water Attunement"
        and f_pbuff["prefix"]["description"] == "Cast water spells."
        # NENHUMA chave 'icon' em lugar algum
        and _no_icon(tip)
    )
    check("extract_skill_tooltip normaliza facts mistos (BuffArray/Buff/PrefixedBuff), sem icon", extract_ok)

    # (j) Schema do dataset de skill_tooltips + dedup por nome normalizado.
    ds = build_skill_tooltips_dataset(
        [
            {"name_en": "Fireball", "name": "Bola de Fogo", "facts": []},
            {"name_en": "fireball", "name": "IGNORADO", "facts": []},  # dup por caso
            {"name_en": "", "name": "vazio", "facts": []},  # vazio descartado
            {"name_en": "Meteor Shower", "name": "Chuva de Meteoros", "facts": []},
        ]
    )
    ds_ok = (
        ds.get("locale") == LOCALE
        and ds.get("category") == "skills_tooltips"
        and ds.get("version") == VERSION
        and isinstance(ds.get("skills"), list)
        and len(ds["skills"]) == 2
        and ds["skills"][0]["name_en"] == "Fireball"
        and ds["skills"][1]["name_en"] == "Meteor Shower"
    )
    check("dataset skill_tooltips (schema + dedup por nome normalizado)", ds_ok)

    # (k) translate_label: curado case-insensitive, passthrough em no-translate.
    label_ok = (
        translate_label("Damage", None, no_translate=True) == "Dano"
        and translate_label("dAmAgE", None, no_translate=True) == "Dano"
        and translate_label("Número de Alvos EN?", None, no_translate=True) == "Número de Alvos EN?"
        and translate_label("Unknown Fancy Label", None, no_translate=True) == "Unknown Fancy Label"
        and translate_label("", None, no_translate=True) == ""
    )
    check("translate_label usa curado case-insensitive e passa desconhecidos (no-translate)", label_ok)

    if failures:
        print(f"\n{len(failures)} self-check(s) falharam: {', '.join(failures)}")
        return 1
    print("\nTodos os self-checks passaram.")
    return 0


# ---------------------------------------------------------------------- #
# CLI
# ---------------------------------------------------------------------- #
def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Gerador de dicionarios PT-BR do Bronco (GW2 API + Argos Translate offline).",
    )
    p.add_argument(
        "--category",
        choices=gw2_api.SUPPORTED_ENDPOINTS + [SKILL_TOOLTIPS_CATEGORY, "all"],
        default="skills",
        help="categoria a gerar (ou 'all' para todas, incluindo skill_tooltips).",
    )
    p.add_argument("--limit", type=int, default=0, help="limita a quantidade de IDs (amostragem).")
    p.add_argument(
        "--install-model",
        action="store_true",
        help="baixa/instala o modelo Argos en->pt e sai.",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="diretorio de saida (padrao: data/dictionaries/pt-br).",
    )
    p.add_argument(
        "--skilldata-output-dir",
        type=Path,
        default=DEFAULT_SKILLDATA_OUTPUT_DIR,
        help="diretorio de saida do dataset de tooltip de skills (padrao: data/skilldata/pt-br).",
    )
    p.add_argument(
        "--cache-dir",
        type=Path,
        default=DEFAULT_CACHE_DIR,
        help="diretorio de cache (padrao: tools/dictionary-generator/cache).",
    )
    p.add_argument(
        "--min-delay",
        type=float,
        default=gw2_api.DEFAULT_MIN_DELAY,
        help="delay minimo entre requests em segundos (rate limit).",
    )
    p.add_argument(
        "--no-translate",
        action="store_true",
        help="nao traduz; 'translated' recebe o ingles (demo de estrutura, sem Argos/rede de traducao).",
    )
    p.add_argument(
        "--no-descriptions",
        action="store_true",
        help="gera apenas nomes; por padrao nomes E descricoes viram entradas {en, translated}.",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="roda self-checks offline (sem rede, sem Argos) e sai.",
    )
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    if args.dry_run:
        return run_self_checks()

    if args.install_model:
        print("Instalando modelo Argos en->pt (pode baixar ~100MB+)...")
        translator.install_model()
        print("Modelo en->pt instalado.")
        return 0

    cache_dir = Path(args.cache_dir)
    client = gw2_api.GW2Client(cache_dir=cache_dir, min_delay=args.min_delay)

    # So construimos o Translator quando ele sera de fato usado. Em
    # --no-translate nenhuma traducao acontece, entao nao criamos nem o cache
    # nem o wrapper (evita instanciar algo inutil e deixa o intento explicito).
    trans = None
    if not args.no_translate:
        trans_cache = translator.TranslationCache(cache_dir / "translations.json")
        trans = translator.Translator(cache=trans_cache)

    if args.category == "all":
        categories = gw2_api.SUPPORTED_ENDPOINTS + [SKILL_TOOLTIPS_CATEGORY]
    else:
        categories = [args.category]

    for category in categories:
        if category == SKILL_TOOLTIPS_CATEGORY:
            generate_skill_tooltips(
                client=client,
                trans=trans,
                skilldata_output_dir=args.skilldata_output_dir,
                fact_labels_output_dir=args.output_dir,
                limit=args.limit,
                no_translate=args.no_translate,
            )
            continue
        generate_category(
            category=category,
            client=client,
            trans=trans,
            output_dir=args.output_dir,
            limit=args.limit,
            no_translate=args.no_translate,
            include_descriptions=not args.no_descriptions,
        )

    print("\nResumo:")
    print(f"  requests HTTP: {client.stats['requests']}")
    print(f"  cache hits (API): {client.stats['cache_hits']}")
    print(f"  retries: {client.stats['retries']}")
    if trans is not None:
        print(f"  traducoes feitas: {trans.stats['translated']}")
        print(f"  cache hits (traducao): {trans.stats['cache_hits']}")
    else:
        print("  traducao: desativada (--no-translate)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""
Camada de acesso a API REST oficial do Guild Wars 2 (api.guildwars2.com/v2).

Uso da API REST publica e EXPLICITAMENTE permitido pelo documento de restricoes
da ArenaNet (categoria "Paineis que consomem a API REST oficial via chave publica").

Esta camada e DESACOPLADA da traducao: ela produz apenas dados brutos em ingles.
Nao depende de argostranslate nem de qualquer modelo de traducao.

Recursos:
  - Busca da lista completa de IDs de um endpoint: GET /v2/<endpoint>
  - Busca de detalhes em lotes de ate 200 IDs: GET /v2/<endpoint>?ids=...&lang=en
  - Rate limiting configuravel (padrao ~0.25s entre requests, abaixo de ~300 req/min)
  - Retry com backoff exponencial em 429/5xx e erros de conexao (honra Retry-After)
  - requests.Session reutilizada
  - Cache em disco dos lotes brutos em cache/raw/<endpoint>/ para runs resumiveis
"""

from __future__ import annotations

import json
import os
import time
from pathlib import Path
from typing import Callable, Iterable, List, Optional

# `requests` e importado de forma PREGUICOSA (dentro de _get_requests) para que
# este modulo importe mesmo sem a dependencia instalada. Assim os modos
# --dry-run e --no-translate rodam sem rede e sem `requests`. O import so e
# exigido quando uma chamada de rede e de fato realizada.

API_BASE = "https://api.guildwars2.com/v2"

# Endpoints suportados. Todos expoem a lista de IDs em /v2/<endpoint> e detalhes
# em /v2/<endpoint>?ids=...&lang=en com campos name/description.
SUPPORTED_ENDPOINTS = [
    "skills",
    "items",
    "traits",
    "specializations",
    "professions",
    "pets",
    "masteries",
]

# Limite duro da API: no maximo 200 IDs por request de detalhes.
MAX_IDS_PER_BATCH = 200

# Tamanho de lote padrao (fica no limite da API).
DEFAULT_BATCH_SIZE = 200

# Rate limit: ~300 req/min. Um delay minimo de 0.25s mantem <= 240 req/min.
DEFAULT_MIN_DELAY = 0.25


def _get_requests():
    """Import preguicoso de `requests` com erro claro se ausente."""
    try:
        import requests  # noqa: WPS433
    except ImportError as exc:
        raise RuntimeError(
            "a biblioteca 'requests' nao esta instalada. Rode:\n"
            "  pip install -r tools/dictionary-generator/requirements.txt\n"
            "Ou use --dry-run / --no-translate para rodar sem rede."
        ) from exc
    return requests


def _parse_retry_after(value: Optional[str]) -> Optional[float]:
    """Interpreta o header Retry-After em segundos.

    Aceita as duas formas definidas pela RFC 7231:
      - delay-seconds numerico (ex.: "120") -> retorna 120.0
      - HTTP-date (ex.: "Wed, 21 Oct 2015 07:28:00 GMT") -> segundos ate a data

    A API do GW2 retorna a forma numerica, mas tratamos a forma de data para
    nao cair silenciosamente no backoff caso um proxy/CDN a devolva. Retorna
    None quando o valor esta ausente ou nao e interpretavel.
    """
    if value is None:
        return None
    value = value.strip()
    if not value:
        return None
    # Forma numerica (delay-seconds).
    try:
        return float(value)
    except (TypeError, ValueError):
        pass
    # Forma HTTP-date.
    try:
        from email.utils import parsedate_to_datetime

        when = parsedate_to_datetime(value)
        if when is None:
            return None
        from datetime import datetime, timezone

        now = datetime.now(when.tzinfo or timezone.utc)
        return max(0.0, (when - now).total_seconds())
    except (TypeError, ValueError, OverflowError):
        return None


def chunk_ids(ids: Iterable, batch_size: int = DEFAULT_BATCH_SIZE) -> List[list]:
    """Divide uma lista de IDs em lotes de no maximo `batch_size` (<= 200).

    Puro/deterministico e sem rede: usado tambem pelos self-checks do --dry-run.
    """
    if batch_size > MAX_IDS_PER_BATCH:
        batch_size = MAX_IDS_PER_BATCH
    if batch_size < 1:
        batch_size = 1
    ids = list(ids)
    return [ids[i : i + batch_size] for i in range(0, len(ids), batch_size)]


class GW2Client:
    """Cliente com rate limiting, retry/backoff e cache em disco."""

    def __init__(
        self,
        cache_dir: Path,
        min_delay: float = DEFAULT_MIN_DELAY,
        max_retries: int = 5,
        backoff_base: float = 1.5,
        timeout: float = 30.0,
        session=None,
    ):
        self.cache_dir = Path(cache_dir)
        self.raw_dir = self.cache_dir / "raw"
        self.min_delay = float(min_delay)
        self.max_retries = int(max_retries)
        self.backoff_base = float(backoff_base)
        self.timeout = float(timeout)
        requests = _get_requests()
        self.session = session or requests.Session()
        self.session.headers.update({"User-Agent": "bronco-dictionary-generator/1.0"})
        self._last_request_ts = 0.0
        # Estatisticas simples para o resumo final.
        self.stats = {"requests": 0, "cache_hits": 0, "retries": 0}

    # ------------------------------------------------------------------ #
    # Rate limiting
    # ------------------------------------------------------------------ #
    def _throttle(self) -> None:
        elapsed = time.monotonic() - self._last_request_ts
        wait = self.min_delay - elapsed
        if wait > 0:
            time.sleep(wait)
        self._last_request_ts = time.monotonic()

    # ------------------------------------------------------------------ #
    # HTTP com retry/backoff
    # ------------------------------------------------------------------ #
    def _request(self, url: str, params: Optional[dict] = None):
        requests = _get_requests()
        attempt = 0
        while True:
            self._throttle()
            try:
                self.stats["requests"] += 1
                resp = self.session.get(url, params=params, timeout=self.timeout)
            except requests.RequestException as exc:
                attempt += 1
                if attempt > self.max_retries:
                    raise
                self.stats["retries"] += 1
                self._sleep_backoff(attempt)
                continue

            if resp.status_code == 200:
                return resp

            # 429 (rate limit) ou 5xx -> retry com backoff, honrando Retry-After.
            if resp.status_code == 429 or 500 <= resp.status_code < 600:
                attempt += 1
                if attempt > self.max_retries:
                    resp.raise_for_status()
                self.stats["retries"] += 1
                delay = _parse_retry_after(resp.headers.get("Retry-After"))
                if delay is not None and delay >= 0:
                    time.sleep(delay)
                else:
                    self._sleep_backoff(attempt)
                continue

            # Outros erros (4xx) sao definitivos.
            resp.raise_for_status()
            return resp

    def _sleep_backoff(self, attempt: int) -> None:
        time.sleep(self.backoff_base ** attempt)

    # ------------------------------------------------------------------ #
    # Lista de IDs
    # ------------------------------------------------------------------ #
    def fetch_id_list(self, endpoint: str) -> List:
        """Retorna a lista completa de IDs de /v2/<endpoint> (com cache em disco)."""
        cache_file = self.raw_dir / endpoint / "_ids.json"
        cached = _read_json(cache_file)
        if cached is not None:
            self.stats["cache_hits"] += 1
            return cached

        resp = self._request(f"{API_BASE}/{endpoint}")
        ids = resp.json()
        _write_json(cache_file, ids)
        return ids

    # ------------------------------------------------------------------ #
    # Detalhes em lotes
    # ------------------------------------------------------------------ #
    def fetch_batch(self, endpoint: str, ids: List, lang: str = "en") -> List[dict]:
        """Busca um lote de ate 200 IDs. Usa cache em disco por lote."""
        if len(ids) > MAX_IDS_PER_BATCH:
            raise ValueError(
                f"lote com {len(ids)} ids excede o maximo de {MAX_IDS_PER_BATCH}"
            )

        cache_file = self._batch_cache_path(endpoint, ids, lang)
        cached = _read_json(cache_file)
        if cached is not None:
            self.stats["cache_hits"] += 1
            return cached

        params = {"ids": ",".join(str(i) for i in ids), "lang": lang}
        resp = self._request(f"{API_BASE}/{endpoint}", params=params)
        data = resp.json()
        _write_json(cache_file, data)
        return data

    def _batch_cache_path(self, endpoint: str, ids: List, lang: str) -> Path:
        # Nome estavel por conteudo do lote (primeiro-ultimo + tamanho) para
        # reaproveitar cache de forma deterministica entre runs.
        first = ids[0] if ids else "empty"
        last = ids[-1] if ids else "empty"
        name = f"{lang}_{first}_{last}_n{len(ids)}.json"
        return self.raw_dir / endpoint / name

    def iter_details(
        self,
        endpoint: str,
        ids: List,
        lang: str = "en",
        batch_size: int = DEFAULT_BATCH_SIZE,
        progress: Optional[Callable[[List[list]], Iterable]] = None,
    ) -> Iterable[dict]:
        """Itera sobre todos os objetos de detalhe, lote a lote (cacheado).

        Este e o UNICO ponto de batching de detalhes: a pipeline consome este
        gerador em vez de reimplementar o loop de lotes. `progress`, quando
        fornecido, embrulha a lista de lotes (ex.: tqdm) para a barra de
        progresso refletir o numero de lotes.
        """
        batches = chunk_ids(ids, batch_size)
        iterator = progress(batches) if progress is not None else batches
        for batch in iterator:
            if not batch:
                continue
            for obj in self.fetch_batch(endpoint, batch, lang=lang):
                yield obj


# ---------------------------------------------------------------------- #
# Helpers de cache em disco
# ---------------------------------------------------------------------- #
def _read_json(path: Path):
    if not path.exists():
        return None
    try:
        with path.open("r", encoding="utf-8") as fh:
            return json.load(fh)
    except (json.JSONDecodeError, OSError):
        return None


def _write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False)
    os.replace(tmp, path)


def extract_text_fields(obj: dict) -> dict:
    """Extrai name/description de um objeto de detalhe da API."""
    return {
        "id": obj.get("id"),
        "name": (obj.get("name") or "").strip(),
        "description": (obj.get("description") or "").strip(),
    }

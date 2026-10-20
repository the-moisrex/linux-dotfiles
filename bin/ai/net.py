"""HTTP transport for the providers: streaming lines/chunks and JSON requests.

Prefers ``curl_cffi`` (a real browser TLS/HTTP-2 fingerprint, which matters for
sites behind Cloudflare) and falls back to ``httpx``.  Standard proxy
environment variables are honoured by both clients; nothing here configures a
proxy explicitly.
"""

from __future__ import annotations

import json
import sys
import uuid

DEFAULT_IMPERSONATE = "firefox"


class StreamError(Exception):
    """A transport or HTTP-level failure while talking to a provider."""


def _clients():
    """Return ``(name, module)`` for the best available HTTP client."""
    try:
        from curl_cffi import requests as curl_requests
        return "curl_cffi", curl_requests
    except Exception:
        pass
    try:
        import httpx
        return "httpx", httpx
    except Exception:
        return None, None


def client_name():
    name, _ = _clients()
    return name or "none"


def _decode(line):
    if isinstance(line, bytes):
        return line.decode("utf-8", "replace")
    return line


def _request_kwargs(kind, headers, json_body, data, impersonate, timeout, stream):
    kwargs = {"headers": headers, "timeout": timeout}
    if stream:
        kwargs["stream"] = True
    if kind == "curl_cffi":
        if impersonate:
            kwargs["impersonate"] = impersonate
        if json_body is not None:
            kwargs["json"] = json_body
        elif data is not None:
            kwargs["data"] = data
    else:  # httpx
        if json_body is not None:
            kwargs["json"] = json_body
        elif data is not None:
            kwargs["content"] = data
    return kwargs


def stream_lines(url, method="POST", headers=None, json_body=None, data=None,
                 impersonate=DEFAULT_IMPERSONATE, timeout=600, debug=False):
    """Yield decoded response lines from a streaming request."""
    kind, module = _clients()
    if kind is None:
        raise StreamError("no HTTP client available (need curl_cffi or httpx)")
    kwargs = _request_kwargs(kind, dict(headers or {}), json_body, data,
                             impersonate, timeout, stream=True)
    if kind == "curl_cffi":
        response = module.request(method, url, **kwargs)
        if response.status_code >= 400:
            raise StreamError(_http_error(response.status_code, response.content))
        for line in response.iter_lines():
            text = _decode(line)
            if debug:
                print(f"[net] {text}", file=sys.stderr)
            yield text
        return
    with module.stream(method, url, **kwargs) as response:
        if response.status_code >= 400:
            response.read()
            raise StreamError(_http_error(response.status_code, response.content))
        for line in response.iter_lines():
            if debug:
                print(f"[net] {line}", file=sys.stderr)
            yield line


def stream_chunks(url, method="POST", headers=None, json_body=None, data=None,
                  impersonate=DEFAULT_IMPERSONATE, timeout=600, debug=False):
    """Yield raw response chunks (bytes) from a streaming request.

    Some APIs (Connect server-streaming) frame payloads as binary
    length-prefixed messages instead of lines, so callers need the bytes
    before splitting them.
    """
    kind, module = _clients()
    if kind is None:
        raise StreamError("no HTTP client available (need curl_cffi or httpx)")
    kwargs = _request_kwargs(kind, dict(headers or {}), json_body, data,
                             impersonate, timeout, stream=True)
    if kind == "curl_cffi":
        response = module.request(method, url, **kwargs)
        if response.status_code >= 400:
            raise StreamError(_http_error(response.status_code, response.content))
        for chunk in response.iter_content(chunk_size=8192):
            if not chunk:
                continue
            if debug:
                print(f"[net] {len(chunk)} bytes", file=sys.stderr)
            yield chunk
        return
    with module.stream(method, url, **kwargs) as response:
        if response.status_code >= 400:
            response.read()
            raise StreamError(_http_error(response.status_code, response.content))
        for chunk in response.iter_bytes():
            if debug:
                print(f"[net] {len(chunk)} bytes", file=sys.stderr)
            yield chunk


def request_json(url, method="GET", headers=None, json_body=None, data=None,
                 impersonate=DEFAULT_IMPERSONATE, timeout=60):
    """Perform a non-streaming request and return the decoded JSON (or None).

    ``data`` sends a raw body instead of ``json_body`` — the multipart uploads
    built by :func:`encode_multipart` go through here.
    """
    kind, module = _clients()
    if kind is None:
        raise StreamError("no HTTP client available (need curl_cffi or httpx)")
    kwargs = _request_kwargs(kind, dict(headers or {}), json_body, data,
                             impersonate, timeout, stream=False)
    if kind == "curl_cffi":
        response = module.request(method, url, **kwargs)
        if response.status_code >= 400:
            raise StreamError(_http_error(response.status_code, response.content))
        body = response.content
    else:
        response = module.request(method, url, **kwargs)
        if response.status_code >= 400:
            raise StreamError(_http_error(response.status_code, response.content))
        body = response.content
    if not body:
        return None
    try:
        return json.loads(body)
    except (ValueError, UnicodeDecodeError):
        raise StreamError(f"expected JSON, got: {body[:200]!r}")


def request_text(url, method="GET", headers=None, timeout=60):
    """Perform a request and return the decoded body as text."""
    kind, module = _clients()
    if kind is None:
        raise StreamError("no HTTP client available (need curl_cffi or httpx)")
    kwargs = _request_kwargs(kind, dict(headers or {}), None, None,
                             DEFAULT_IMPERSONATE, timeout, stream=False)
    response = module.request(method, url, **kwargs)
    if response.status_code >= 400:
        raise StreamError(_http_error(response.status_code, response.content))
    return response.text


def request_raw(url, method="PUT", headers=None, data=None,
                impersonate=DEFAULT_IMPERSONATE, timeout=300):
    """Send a raw body and return ``(status, body)`` without decoding.

    The providers' upload steps hand back a pre-signed blob URL that has to be
    written with ``PUT`` and *without* the site's cookies/token, so this is
    deliberately a lower-level call than :func:`request_json`.
    """
    kind, module = _clients()
    if kind is None:
        raise StreamError("no HTTP client available (need curl_cffi or httpx)")
    kwargs = _request_kwargs(kind, dict(headers or {}), None, data,
                             impersonate, timeout, stream=False)
    response = module.request(method, url, **kwargs)
    return response.status_code, response.content


def encode_multipart(fields, boundary=None):
    """Build a ``multipart/form-data`` body for *fields*.

    A field whose value is a ``(filename, bytes, content_type)`` tuple becomes
    a file part; anything else is a plain form field.  Returns
    ``(body, content_type_header)`` — the header carries the generated
    boundary, so it must be sent as-is with the body.
    """
    boundary = boundary or uuid.uuid4().hex
    marker = f"--{boundary}".encode()
    out = bytearray()
    for name, value in fields:
        out += marker + b"\r\n"
        if isinstance(value, tuple) and len(value) == 3:
            filename, payload, content_type = value
            out += (f'Content-Disposition: form-data; name="{name}"; '
                    f'filename="{filename}"\r\n').encode()
            out += f"Content-Type: {content_type}\r\n\r\n".encode()
            out += payload + b"\r\n"
        else:
            out += (f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                    ).encode()
            out += str(value).encode() + b"\r\n"
    out += marker + b"--\r\n"
    return bytes(out), f"multipart/form-data; boundary={boundary}"


def _http_error(status, body):
    try:
        text = body.decode("utf-8", "replace")
    except Exception:
        text = str(body)
    return f"HTTP {status}: {text[:300]}"


def sse_payloads(lines):
    """Yield the payload of each SSE ``data:`` line (ignoring comments/blanks)."""
    for line in lines:
        if not line:
            continue
        if line.startswith(":"):
            continue
        if line.startswith("data:"):
            yield line[5:].strip()

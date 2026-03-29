#!/usr/bin/env python3
"""
Script to generate a dashboard showing all podman compose files and their services.
"""

import os
import re
import subprocess
from datetime import datetime
from pathlib import Path

import yaml

COMPOSE_FILENAMES = {
    "docker-compose.yml",
    "docker-compose.yaml",
    "compose.yml",
    "compose.yaml",
    "podman-compose.yml",
    "podman-compose.yaml",
}


def find_compose_files(root_dir):
    compose_files = []
    for root, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if d != "dashboard" and not d.startswith(".")]
        for file_name in files:
            if file_name in COMPOSE_FILENAMES:
                compose_files.append(os.path.join(root, file_name))
    return sorted(compose_files)


def parse_service_name(container_name, project_name):
    normalized = container_name.replace("-", "_")
    prefixes = [f"{project_name}_", f"{project_name}-"]
    for prefix in prefixes:
        if container_name.startswith(prefix):
            raw = container_name[len(prefix):]
            return re.sub(r"[-_]\d+$", "", raw)

    parts = normalized.split("_")
    if len(parts) >= 3:
        return "_".join(parts[1:-1])
    if len(parts) >= 2:
        return parts[-1]
    return container_name


def get_container_status(project_dir, compose_file):
    compose_file_path = os.path.join(project_dir, compose_file)
    project_name = os.path.basename(project_dir)

    commands = [
        ["podman", "compose", "-f", compose_file_path, "ps", "--format", "json"],
        ["podman-compose", "-f", compose_file_path, "ps", "--format", "json"],
    ]

    for command in commands:
        try:
            result = subprocess.run(command, capture_output=True, text=True, cwd=project_dir)
            if result.returncode != 0 or not result.stdout.strip():
                continue

            containers = yaml.safe_load(result.stdout)
            if not isinstance(containers, list):
                continue

            statuses = {}
            for container in containers:
                if not isinstance(container, dict):
                    continue
                name = container.get("Names") or container.get("Names") or container.get("name") or ""
                state = (
                    container.get("State")
                    or container.get("Status")
                    or container.get("state")
                    or "unknown"
                )
                service_name = parse_service_name(name, project_name)
                statuses[service_name] = str(state)
            return statuses
        except FileNotFoundError:
            continue
        except Exception as exc:
            print(f"Error collecting status using {' '.join(command)}: {exc}")

    return {}


def extract_urls(labels):
    urls = []
    if isinstance(labels, dict):
        labels = [f"{key}={value}" for key, value in labels.items()]
    for label in labels or []:
        if "traefik.http.routers." in str(label) and ".rule=" in str(label):
            for host in re.findall(r"Host\(`([^`]+)`\)", str(label)):
                urls.append(f"http://{host}")
    return sorted(set(urls))


def parse_compose_file(file_path):
    try:
        with open(file_path, "r", encoding="utf-8") as file_obj:
            data = yaml.safe_load(file_obj)
    except Exception as exc:
        print(f"Error parsing compose file {file_path}: {exc}")
        return None

    project_dir = os.path.dirname(file_path)
    compose_file = os.path.basename(file_path)
    status_map = get_container_status(project_dir, compose_file)

    services = {}
    for service_name, service_config in (data or {}).get("services", {}).items():
        labels = service_config.get("labels", [])
        service_status = status_map.get(service_name, "unknown")
        lower_status = service_status.lower()
        is_running = "running" in lower_status or "healthy" in lower_status or "up" in lower_status

        environment = service_config.get("environment", [])
        if isinstance(environment, dict):
            environment = [f"{key}={value}" for key, value in environment.items()]

        services[service_name] = {
            "image": service_config.get("image", "N/A"),
            "ports": service_config.get("ports", []),
            "environment": environment,
            "volumes": service_config.get("volumes", []),
            "labels": labels,
            "status": service_status,
            "is_running": is_running,
            "urls": extract_urls(labels),
        }

    services = dict(sorted(services.items(), key=lambda item: (not item[1]["is_running"], item[0])))
    data_root = os.environ.get("DATA_DIR", "/app/data")

    return {
        "file_path": file_path,
        "relative_path": os.path.relpath(file_path, data_root),
        "services": services,
    }


def generate_html(compose_data, output_file):
    generation_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    total_services = sum(len(compose["services"]) for compose in compose_data)
    running_services = sum(
        1 for compose in compose_data for service in compose["services"].values() if service["is_running"]
    )

    compose_sections = ""
    for compose in compose_data:
        relative_path = compose["relative_path"]
        services = compose["services"]

        cards = ""
        for service_name, service_info in services.items():
            status = service_info["status"]
            lower_status = status.lower()
            status_class = "running" if service_info["is_running"] else ("stopped" if "exited" in lower_status or "created" in lower_status else "unknown")
            badge_class = f"status-badge-{status_class}"
            links = "".join(
                f'<a href="{url}" target="_blank" class="service-link"><i class="fas fa-external-link-alt"></i> {url.replace("http://", "")}</a>'
                for url in service_info["urls"]
            ) or '<span class="no-links"><i class="fas fa-chain-broken"></i> No direct link</span>'

            cards += f'''
            <div class="service-card {status_class}" data-service-name="{service_name.lower()}" data-compose-path="{relative_path.lower()}">
                <div class="service-header">
                    <div class="status-indicator status-{status_class}"></div>
                    <div class="logo-container">{service_name[:1].upper() if service_name else "?"}</div>
                    <div class="service-name">{service_name}<span class="service-status {badge_class}">{status}</span></div>
                </div>
                <div class="service-image">Image: <span class="detail-value">{service_info['image']}</span></div>
                <div class="service-links">{links}</div>
                <div class="service-meta">
                    <span><i class="fas fa-plug"></i> {len(service_info['ports'])} ports</span>
                    <span><i class="fas fa-folder"></i> {len(service_info['volumes'])} volumes</span>
                    <span><i class="fas fa-leaf"></i> {len(service_info['environment'])} env vars</span>
                </div>
            </div>'''

        if not cards:
            cards = '<div class="no-services">No services defined</div>'

        compose_sections += f'''
        <div class="compose-group" data-compose-path="{relative_path.lower()}">
            <div class="compose-header"><h3 class="compose-title"><i class="fas fa-file-code"></i> {relative_path}</h3><div class="expand-icon">−</div></div>
            <div class="compose-body expanded"><div class="service-grid">{cards}</div></div>
        </div>'''

    html = f'''<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Podman Compose Dashboard</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
<style>
:root{{--bg:#f5f7fa;--panel:#fff;--text:#333;--muted:#666;--border:#ddd;--header:linear-gradient(135deg,#6a11cb 0%,#2575fc 100%);--link:#3498db;--linkh:#2980b9;--card:#f8f9fa}}
.dark-mode{{--bg:#121212;--panel:#1f1f1f;--text:#efefef;--muted:#b0b0b0;--border:#3f3f3f;--card:#292929;--link:#4da6ff;--linkh:#3976aa}}
body{{font-family:Segoe UI,sans-serif;margin:0;background:var(--bg);color:var(--text);padding:20px}}.container{{max-width:1200px;margin:auto}}
header{{background:var(--header);color:#fff;border-radius:12px;padding:20px;box-shadow:0 4px 14px rgba(0,0,0,.15)}}
.controls{{display:flex;gap:12px;margin-top:14px}} .search-box{{flex:1;padding:12px;border-radius:999px;border:none}} .theme-toggle{{width:40px;height:40px;border-radius:50%;border:1px solid #fff;background:transparent;color:#fff;cursor:pointer}}
.summary{{margin-top:14px;display:flex;gap:10px;flex-wrap:wrap}} .pill{{background:rgba(255,255,255,.18);padding:6px 12px;border-radius:999px}}
.compose-group{{background:var(--panel);border-radius:10px;margin-top:18px;overflow:hidden;border:1px solid var(--border)}} .compose-header{{background:#2c3e50;color:#fff;padding:12px 16px;display:flex;justify-content:space-between;cursor:pointer}}
.compose-body.expanded{{padding:16px}} .compose-body{{max-height:0;overflow:hidden}}
.service-grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:14px}} .service-card{{background:var(--card);padding:14px;border-radius:10px;border-left:4px solid #f1c40f}}
.service-card.running{{border-left-color:#2ecc71}} .service-card.stopped{{border-left-color:#e74c3c}}
.service-header{{display:flex;align-items:center;gap:10px;margin-bottom:10px}} .status-indicator{{width:10px;height:10px;border-radius:50%}} .status-running{{background:#2ecc71}} .status-stopped{{background:#e74c3c}} .status-unknown{{background:#f1c40f}}
.logo-container{{width:34px;height:34px;border-radius:50%;display:flex;align-items:center;justify-content:center;background:#4b7bec;color:#fff;font-weight:700}} .service-name{{font-weight:600;display:flex;justify-content:space-between;gap:8px;flex:1}}
.service-status{{font-size:.75em;padding:2px 8px;border-radius:999px}} .status-badge-running{{background:#d4edda;color:#155724}} .status-badge-stopped{{background:#f8d7da;color:#721c24}} .status-badge-unknown{{background:#fff3cd;color:#856404}}
.service-link{{display:inline-flex;background:var(--link);color:#fff;padding:5px 10px;border-radius:999px;text-decoration:none;font-size:.82em}} .service-link:hover{{background:var(--linkh)}} .service-links{{display:flex;gap:8px;flex-wrap:wrap}}
.service-meta{{margin-top:10px;display:flex;gap:10px;flex-wrap:wrap;font-size:.85em;color:var(--muted)}} .hidden{{display:none!important}}
</style></head><body><div class="container"><header><h1><i class="fas fa-network-wired"></i> Podman Compose Dashboard</h1><div>Overview of all compose stacks</div>
<div class="controls"><input id="searchBox" class="search-box" placeholder="Search services or stack paths"><button id="themeToggle" class="theme-toggle"><i class="fas fa-moon"></i></button></div>
<div class="summary"><span class="pill"><i class="fas fa-layer-group"></i> {len(compose_data)} stacks</span><span class="pill"><i class="fas fa-cubes"></i> {total_services} services</span><span class="pill"><i class="fas fa-play-circle"></i> {running_services} running</span><span class="pill"><i class="fas fa-clock"></i> {generation_time}</span></div></header>
<main>{compose_sections}</main></div>
<script>
const themeToggle=document.getElementById('themeToggle');const icon=themeToggle.querySelector('i');const savedTheme=localStorage.getItem('theme')||'dark';if(savedTheme==='dark'){{document.body.classList.add('dark-mode');icon.classList.replace('fa-moon','fa-sun')}}
themeToggle.addEventListener('click',()=>{{document.body.classList.toggle('dark-mode');const dark=document.body.classList.contains('dark-mode');localStorage.setItem('theme',dark?'dark':'light');icon.classList.toggle('fa-sun',dark);icon.classList.toggle('fa-moon',!dark);}});
document.querySelectorAll('.compose-header').forEach(h=>h.addEventListener('click',()=>{{const b=h.nextElementSibling;b.classList.toggle('expanded');h.querySelector('.expand-icon').textContent=b.classList.contains('expanded')?'−':'+';}}));
document.getElementById('searchBox').addEventListener('input',function(){{const t=this.value.toLowerCase();document.querySelectorAll('.compose-group').forEach(g=>{{const p=(g.dataset.composePath||'');let m=p.includes(t);g.querySelectorAll('.service-card').forEach(c=>{{const ok=(c.dataset.serviceName||'').includes(t);c.classList.toggle('hidden',!ok&&!m);m=m||ok;}});g.classList.toggle('hidden',!m);}});}});
</script></body></html>'''

    with open(output_file, "w", encoding="utf-8") as file_obj:
        file_obj.write(html)


def main(data_dir=None):
    dashboard_dir = Path(__file__).parent
    root_dir = Path(data_dir) if data_dir else dashboard_dir.parent
    compose_files = find_compose_files(root_dir)
    compose_data = [item for item in (parse_compose_file(path) for path in compose_files) if item]
    output_file = os.path.join(dashboard_dir, "index.html")
    generate_html(compose_data, output_file)
    print(f"Dashboard generated at {output_file}")


if __name__ == "__main__":
    main()

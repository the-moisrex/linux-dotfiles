def generate_html(compose_data, output_file):
    """Generate HTML dashboard"""
    from datetime import datetime
    generation_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # Build the compose sections first
    compose_sections = ""
    for compose in compose_data:
        relative_path = compose['relative_path']
        services = compose['services']

        service_cards = ""
        if services:
            for service_name, service_info in services.items():
                # Determine status classes
                status = service_info['status']
                is_running = service_info['is_running']
                status_class = 'running' if is_running else ('stopped' if 'exited' in status.lower() or 'created' in status.lower() else 'unknown')
                status_badge_class = 'status-badge-running' if is_running else ('status-badge-stopped' if 'exited' in status.lower() or 'created' in status.lower() else 'status-badge-unknown')

                # Create a simple logo based on the first letter of the service name
                logo_letter = service_name[0].upper() if service_name else "?"

                # Create links for the service if URLs are available
                links_html = ""
                if service_info['urls']:
                    for url in service_info['urls']:
                        links_html += f'<a href="{url}" target="_blank" class="service-link"><i class="fas fa-external-link-alt"></i> {url.replace("http://", "")}</a>'
                else:
                    links_html = '<span class="no-links"><i class="fas fa-chain-broken"></i> No direct link</span>'

                # Simplified card with minimal info by default
                service_card = f'''
            <div class="service-card {status_class}" data-service-name="{service_name.lower()}" data-compose-path="{relative_path.lower()}">
                <div class="service-header">
                    <div class="status-indicator status-{status_class}"></div>
                    <div class="logo-container">{logo_letter}</div>
                    <div class="service-name">{service_name}
                        <span class="service-status {status_badge_class}">{status}</span>
                    </div>
                </div>
                <div class="service-image">Image: <span class="detail-value">{service_info['image']}</span></div>
                <div class="service-links">
                    {links_html}
                </div>
                <button class="toggle-details"><i class="fas fa-chevron-down"></i> Show More</button>
                <div class="service-details">
                    <div class="detail-item">
                        <span class="detail-label">Ports:</span>
                        <span class="detail-value">{", ".join(service_info['ports']) if service_info['ports'] else "None"}</span>
                    </div>
                    <div class="detail-item">
                        <span class="detail-label">Labels:</span>
                        <span class="detail-value">{len(service_info['labels'])} labels</span>
                    </div>
                    <div class="detail-item">
                        <span class="detail-label">Volumes:</span>
                        <span class="detail-value">{len(service_info['volumes'])} volumes</span>
                    </div>
                    <div class="detail-item">
                        <span class="detail-label">Env:</span>
                        <span class="detail-value">{len(service_info['environment'])} vars</span>
                    </div>
                </div>
            </div>'''
                service_cards += service_card
        else:
            service_cards = '<div class="no-services">No services defined</div>'

        compose_section = f'''
        <div class="compose-group" data-compose-path="{relative_path.lower()}">
            <div class="compose-header">
                <h3 class="compose-title"><i class="fas fa-file-code"></i> {relative_path}</h3>
                <div class="expand-icon">−</div>
            </div>
            <div class="compose-body expanded">
                <div class="service-grid">
                    {service_cards}
                </div>
            </div>
        </div>'''

        compose_sections += compose_section

    # Create the HTML template with the generation time inserted
    html_content = f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Podman Compose Dashboard</title>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <style>
        :root {{
            --bg-primary: #f5f7fa;
            --bg-secondary: white;
            --text-primary: #333;
            --text-secondary: #666;
            --border-color: #ddd;
            --header-bg: linear-gradient(135deg, #6a11cb 0%, #2575fc 100%);
            --card-bg: #f8f9fa;
            --card-hover: #f1f3f5;
            --link-bg: #3498db;
            --link-hover: #2980b9;
            --running-bg: #d4edda;
            --running-border: #c3e6cb;
            --running-text: #155724;
            --stopped-bg: #f8d7da;
            --stopped-border: #f5c6cb;
            --stopped-text: #721c24;
            --unknown-bg: #fff3cd;
            --unknown-border: #ffeaa7;
            --unknown-text: #856404;
        }}
        .dark-mode {{
            --bg-primary: #1a1a1a;
            --bg-secondary: #2d2d2d;
            --text-primary: #f0f0f0;
            --text-secondary: #aaa;
            --border-color: #444;
            --card-bg: #3a3a3a;
            --card-hover: #4a4a4a;
            --link-bg: #4da6ff;
            --link-hover: #1a3d6d;
            --running-bg: #1d3c25;
            --running-border: #2d5c3d;
            --running-text: #a3d9b1;
            --stopped-bg: #4d2629;
            --stopped-border: #6d3c3f;
            --stopped-text: #f1aeb5;
            --unknown-bg: #4d4725;
            --unknown-border: #6d633d;
            --unknown-text: #ffeaa7;
        }}
        body {{
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: var(--bg-primary);
            color: var(--text-primary);
            transition: background-color 0.3s, color 0.3s;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
        }}
        header {{
            text-align: center;
            margin-bottom: 30px;
            padding: 20px;
            background: var(--header-bg);
            color: white;
            border-radius: 10px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            display: flex;
            flex-direction: column;
            align-items: center;
        }}
        .header-content {{
            flex-grow: 1;
            text-align: center;
        }}
        h1 {{
            margin: 0;
            font-size: 2.5em;
        }}
        .subtitle {{
            font-size: 1.1em;
            opacity: 0.9;
            margin-top: 10px;
        }}
        .controls {{
            display: flex;
            gap: 15px;
            margin-top: 15px;
            width: 100%;
            max-width: 600px;
        }}
        .search-box {{
            flex-grow: 1;
            padding: 12px 15px;
            border: 2px solid #ddd;
            border-radius: 30px;
            font-size: 16px;
            outline: none;
            transition: border-color 0.3s;
        }}
        .search-box:focus {{
            border-color: var(--link-bg);
        }}
        .theme-toggle {{
            background: none;
            border: 1px solid white;
            border-radius: 50%;
            width: 40px;
            height: 40px;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 16px;
            color: white;
        }}
        .compose-group {{
            background: var(--bg-secondary);
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.08);
            margin-bottom: 25px;
            overflow: hidden;
        }}
        .compose-header {{
            background: #2c3e50;
            color: white;
            padding: 15px 20px;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }}
        .compose-header:hover {{
            background: #34495e;
        }}
        .compose-title {{
            margin: 0;
            font-size: 1.4em;
        }}
        .expand-icon {{
            font-size: 1.2em;
        }}
        .compose-body {{
            padding: 0;
            max-height: 0;
            overflow: hidden;
            transition: max-height 0.3s ease-out;
        }}
        .compose-body.expanded {{
            max-height: 1000px;
            padding: 20px;
        }}
        .service-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
            gap: 20px;
        }}
        .service-card {{
            background: var(--card-bg);
            border-radius: 10px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
            padding: 20px;
            position: relative;
            transition: transform 0.2s, box-shadow 0.2s;
            border-left: 4px solid #3498db;
        }}
        .service-card.running {{
            border-left-color: #28a745;
        }}
        .service-card.stopped {{
            border-left-color: #dc3545;
        }}
        .service-card.unknown {{
            border-left-color: #ffc107;
        }}
        .service-card:hover {{
            transform: translateY(-3px);
            box-shadow: 0 5px 15px rgba(0,0,0,0.1);
            background: var(--card-hover);
        }}
        .service-header {{
            display: flex;
            align-items: center;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 1px solid var(--border-color);
        }}
        .status-indicator {{
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 12px;
            flex-shrink: 0;
        }}
        .status-running {{ background-color: #28a745; }}
        .status-stopped {{ background-color: #dc3545; }}
        .status-unknown {{ background-color: #ffc107; }}
        .logo-container {{
            width: 50px;
            height: 50px;
            border-radius: 50%;
            background: linear-gradient(135deg, #3498db, #7b68ee);
            display: flex;
            align-items: center;
            justify-content: center;
            margin-right: 15px;
            color: white;
            font-weight: bold;
            font-size: 20px;
        }}
        .service-name {{
            font-weight: bold;
            color: var(--text-primary);
            font-size: 1.2em;
            flex-grow: 1;
        }}
        .service-status {{
            margin-left: 10px;
            font-size: 0.85em;
            padding: 3px 8px;
            border-radius: 12px;
        }}
        .status-badge-running {{
            background-color: var(--running-bg);
            color: var(--running-text);
            border: 1px solid var(--running-border);
        }}
        .status-badge-stopped {{
            background-color: var(--stopped-bg);
            color: var(--stopped-text);
            border: 1px solid var(--stopped-border);
        }}
        .status-badge-unknown {{
            background-color: var(--unknown-bg);
            color: var(--unknown-text);
            border: 1px solid var(--unknown-border);
        }}
        .service-links {{
            display: flex;
            gap: 10px;
            margin-top: 10px;
            flex-wrap: wrap;
        }}
        .service-link {{
            display: inline-flex;
            align-items: center;
            gap: 5px;
            padding: 6px 12px;
            background: var(--link-bg);
            color: white;
            text-decoration: none;
            border-radius: 20px;
            font-size: 0.9em;
            transition: background 0.2s;
        }}
        .service-link:hover {{
            background: var(--link-hover);
            transform: translateY(-2px);
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }}
        .service-details {{
            display: none;
        }}
        .service-details.expanded {{
            display: block;
            margin-top: 15px;
            padding-top: 15px;
            border-top: 1px dashed var(--border-color);
        }}
        .detail-item {{
            margin-bottom: 8px;
        }}
        .detail-label {{
            font-weight: bold;
            color: var(--text-secondary);
            display: inline-block;
            width: 80px;
        }}
        .detail-value {{
            color: var(--text-primary);
        }}
        .toggle-details {{
            background: none;
            border: none;
            color: var(--link-bg);
            cursor: pointer;
            font-size: 0.9em;
            text-decoration: underline;
            margin-top: 10px;
            display: flex;
            align-items: center;
            gap: 5px;
            padding: 0;
        }}
        .toggle-details:hover {{
            color: var(--link-hover);
        }}
        .no-services {{
            color: #e74c3c;
            font-style: italic;
            padding: 10px;
            text-align: center;
        }}
        footer {{
            text-align: center;
            margin-top: 40px;
            padding: 20px;
            color: var(--text-secondary);
            font-size: 0.9em;
        }}
        .hidden {{
            display: none !important;
        }}
        @media (max-width: 768px) {{
            .service-grid {{
                grid-template-columns: 1fr;
            }}
            header {{
                padding: 15px;
            }}
            .controls {{
                flex-direction: column;
            }}
        }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div class="header-content">
                <h1><i class="fas fa-network-wired"></i> Podman Compose Dashboard</h1>
                <div class="subtitle">Overview of all your compose files and services</div>
            </div>
            <div class="controls">
                <input type="text" class="search-box" id="searchBox" placeholder="Search services...">
                <button class="theme-toggle" id="themeToggle">
                    <i class="fas fa-moon"></i>
                </button>
            </div>
        </header>

        <main>
{compose_sections}
        </main>

        <footer>
            Generated on {generation_time}<div style="margin-top: 10px;">
                <button id="refreshBtn" style="padding: 8px 16px; background: var(--link-bg); color: white; border: none; border-radius: 4px; cursor: pointer;">
                    <i class="fas fa-sync-alt"></i> Refresh Status
                </button>
            </div>
        </footer>
    </div>

    <script>
        // Theme toggle functionality
        const themeToggle = document.getElementById("themeToggle");
        const themeIcon = themeToggle.querySelector("i");

        // Check for saved theme preference or respect OS setting
        const savedTheme = localStorage.getItem("theme");
        const prefersDark = window.matchMedia("(prefers-color-scheme: dark)").matches;

        if (savedTheme === "dark" || (!savedTheme && prefersDark)) {{
            document.body.classList.add("dark-mode");
            themeIcon.classList.remove("fa-moon");
            themeIcon.classList.add("fa-sun");
        }}

        themeToggle.addEventListener("click", () => {{
            document.body.classList.toggle("dark-mode");

            if (document.body.classList.contains("dark-mode")) {{
                localStorage.setItem("theme", "dark");
                themeIcon.classList.remove("fa-moon");
                themeIcon.classList.add("fa-sun");
            }} else {{
                localStorage.setItem("theme", "light");
                themeIcon.classList.remove("fa-sun");
                themeIcon.classList.add("fa-moon");
            }}
        }});

        // Toggle expand/collapse for compose sections
        document.querySelectorAll(".compose-header").forEach(header => {{
            header.addEventListener("click", () => {{
                const body = header.nextElementSibling;
                body.classList.toggle("expanded");
                const icon = header.querySelector(".expand-icon");
                icon.textContent = body.classList.contains("expanded") ? "−" : "+";
            }});
        }});

        // Expand all by default
        document.querySelectorAll(".compose-body").forEach(body => {{
            body.classList.add("expanded");
            const icon = body.previousElementSibling.querySelector(".expand-icon");
            icon.textContent = "−";
        }});

        // Toggle service details
        document.querySelectorAll(".toggle-details").forEach(button => {{
            button.addEventListener("click", function() {{
                const details = this.parentNode.querySelector(".service-details");
                details.classList.toggle("expanded");

                const icon = this.querySelector("i");
                if (details.classList.contains("expanded")) {{
                    this.innerHTML = "<i class=\\"fas fa-chevron-up\\"></i> Show Less";
                }} else {{
                    this.innerHTML = "<i class=\\"fas fa-chevron-down\\"></i> Show More";
                }}
            }});
        }});

        // Search functionality
        const searchBox = document.getElementById("searchBox");
        searchBox.addEventListener("input", function() {{
            const searchTerm = this.value.toLowerCase();
            
            // Hide/show compose groups based on search term
            document.querySelectorAll(".compose-group").forEach(group => {{
                const composePath = group.getAttribute("data-compose-path");
                if (composePath.includes(searchTerm)) {{
                    group.classList.remove("hidden");
                    return; // Don't hide this group
                }}
                
                // Check if any services in this group match the search
                let hasMatchingService = false;
                group.querySelectorAll(".service-card").forEach(service => {{
                    const serviceName = service.getAttribute("data-service-name");
                    if (serviceName.includes(searchTerm)) {{
                        service.classList.remove("hidden");
                        hasMatchingService = true;
                    }} else {{
                        service.classList.add("hidden");
                    }}
                }});
                
                // Show/hide the group based on whether it has matching services
                if (hasMatchingService || searchTerm === "") {{
                    group.classList.remove("hidden");
                }} else {{
                    group.classList.add("hidden");
                }}
            }});
        }});

        // Refresh status button
        const refreshBtn = document.getElementById("refreshBtn");
        refreshBtn.addEventListener("click", function() {{
            refreshBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Refreshing...';
            refreshBtn.disabled = true;
            
            // Reload the page to refresh the dashboard
            location.reload();
        }});
    </script>
</body>
</html>'''

    with open(output_file, 'w') as f:
        f.write(html_content)
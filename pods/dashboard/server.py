#!/usr/bin/env python3
"""
Simple HTTP server that serves the dashboard and regenerates it on each request
"""

import http.server
import socketserver
import os
from pathlib import Path
import threading
import time
from generate_dashboard import main as generate_dashboard

class DashboardHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/' or self.path == '/index.html':
            # Regenerate the dashboard before serving
            print("Regenerating dashboard...")
            try:
                # Pass the data directory to the generator
                data_dir = os.environ.get('DATA_DIR', '/app/data')
                generate_dashboard(data_dir)
                print("Dashboard regenerated successfully")
            except Exception as e:
                print(f"Error regenerating dashboard: {e}")
                # Return an error page if generation fails
                self.send_response(500)
                self.send_header('Content-type', 'text/html')
                self.end_headers()
                self.wfile.write(b"<h1>500 - Error generating dashboard</h1>")
                return

            # Serve the index.html file directly
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()

            # Read and send the index.html file
            try:
                with open('/app/index.html', 'rb') as file:
                    self.wfile.write(file.read())
            except FileNotFoundError:
                self.send_response(404)
                self.send_header('Content-type', 'text/html')
                self.end_headers()
                self.wfile.write(b"<h1>404 - Dashboard file not found</h1>")
            return
        elif self.path == '/health':
            # Health check endpoint
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"OK")
            return
        else:
            # Handle other requests (like favicon, etc.)
            self.send_response(404)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(b"<h1>404 - Page not found</h1>")

    def end_headers(self):
        # Add security headers
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('X-Frame-Options', 'DENY')
        self.send_header('X-XSS-Protection', '1; mode=block')
        super().end_headers()


def main():
    PORT = int(os.environ.get('PORT', 8000))

    # Initial generation of dashboard
    print("Generating initial dashboard...")
    data_dir = os.environ.get('DATA_DIR', '/app/data')
    generate_dashboard(data_dir)

    # Start the server
    handler = DashboardHandler
    with socketserver.TCPServer(("", PORT), handler) as httpd:
        print(f"Server running at http://0.0.0.0:{PORT}/")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
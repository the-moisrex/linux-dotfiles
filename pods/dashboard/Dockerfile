FROM python:3.11-slim

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Install system dependencies for podman
RUN apt-get update && \
    apt-get install -y curl gnupg apt-transport-https && \
    curl -fsSL https://download.opensuse.org/repositories/devel:kubic:libcontainers:stable/Debian_12/Release.key | gpg --dearmor -o /usr/share/keyrings/libcontainers-archive-keyring.gpg && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/libcontainers-archive-keyring.gpg] https://download.opensuse.org/repositories/devel:kubic:libcontainers:stable/Debian_12/ /" | tee /etc/apt/sources.list.d/devel:kubic:libcontainers:stable.list && \
    apt-get update && \
    apt-get install -y podman && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Install podman-compose
RUN pip install --no-cache-dir podman-compose

# Copy application files
COPY . .

# Expose port
EXPOSE 8000

# Run the server
CMD ["python", "server.py"]
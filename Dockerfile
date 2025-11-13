# Dockerfile
FROM ubuntu:24.04

# Install build essentials
RUN apt-get update && apt-get install -y \
    build-essential \
    ca-certificates \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Copy source files
WORKDIR /app
COPY server.cpp /app/
COPY httplib.h /app/
COPY json.hpp /app/
# (if you already have reminders.json, copy it)
# COPY reminders.json /app/

# Build binary
RUN g++ server.cpp -std=c++17 -pthread -O2 -o /app/server

EXPOSE 8080
CMD ["/app/server"]

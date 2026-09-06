FROM node:22-bookworm AS worker-deps

ENV PLAYWRIGHT_BROWSERS_PATH=/ms-playwright
WORKDIR /worker
COPY package.json ./
RUN npm install --omit=dev \
    && npx playwright install --with-deps chromium \
    && npm cache clean --force

FROM debian:bookworm AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake libcurl4-openssl-dev libsqlite3-dev \
    nlohmann-json3-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel

FROM node:22-bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
ENV NODE_ENV=production
ENV PLAYWRIGHT_BROWSERS_PATH=/ms-playwright
RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 libsqlite3-0 libstdc++6 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=worker-deps /worker/node_modules /app/node_modules
COPY --from=worker-deps /ms-playwright /ms-playwright
COPY --from=builder /app/build/tg_bot /app/tg_bot
COPY browser_worker.js package.json start.sh ./
RUN mkdir -p /app/data/custojusto/profiles \
    && chmod +x /app/start.sh
ENV DB_PATH=/app/data/bot.sqlite3
ENV BROWSER_WORKER_URL=http://127.0.0.1:3001
ENV BROWSER_WORKER_PORT=3001
EXPOSE 3001
CMD ["/app/start.sh"]

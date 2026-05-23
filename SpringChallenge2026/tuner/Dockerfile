# SC2026 auto-tuner container.
# Compiles main.cpp / other_code.cpp / arena.cpp with g++ 11+ and runs the
# Python tuner. Results land in /results which the host mounts.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    g++ \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Project sources — copy at build time so the container is self-contained.
# Bind-mount can override at runtime via `-v $PWD:/app`.
COPY main.cpp other_code.cpp arena.cpp /app/
COPY tuner/tuner.py tuner/search_space.json /app/tuner/

# Results land under /results (mount this from the host).
ENV TUNER_RESULTS=/results
RUN mkdir -p /results

CMD ["python3", "/app/tuner/tuner.py"]

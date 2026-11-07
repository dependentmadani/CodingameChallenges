# Tuning environment for the Summer Challenge bot.
#
# Multi-arch: builds on x86_64 and on arm64 (Oracle's always-free Ampere
# instances are aarch64). The referee jar's highest class-file version is 61,
# so JDK 17 is the real floor; a current LTS is used rather than pinning the
# JDK 26 that happens to be installed locally, because that tag is not
# available everywhere.
FROM eclipse-temurin:21-jdk

RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 g++ \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY arena_tools/referee/target/referee.jar /app/referee.jar
COPY main.cpp test_bot.py /app/
COPY tuning/ /app/tuning/

ENV REFEREE_JAR=/app/referee.jar \
    JAVA_BIN=java \
    PYTHONUNBUFFERED=1

# Verifies the container can produce trustworthy results before any compute is
# spent. It replays a seed twice and checks the two runs agree; if the host is
# oversubscribed the referee drops late actions and tuning would chase noise.
CMD ["python3", "tuning/selfcheck.py"]

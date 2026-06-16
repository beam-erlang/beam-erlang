ARG BASE
FROM ${BASE}
# A development image contains the complete qualified SDK and native build tools.
RUN --mount=type=bind,source=.,target=/inputs,readonly \
    if command -v apk >/dev/null && [ -d /inputs/packages ]; then \
      apk add --no-network /inputs/packages/*.apk; \
    elif command -v apk >/dev/null; then \
      apk add --no-cache build-base cmake ninja-is-really-ninja perl python3 libncursesw git; \
    else \
      apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build perl python3 libncurses6 git ca-certificates \
      && rm -rf /var/lib/apt/lists/*; \
    fi
COPY sdk/ /opt/beam/
ENV PATH="/opt/beam/bin:${PATH}"
ENV CMAKE_PREFIX_PATH="/opt/beam"
WORKDIR /work
CMD ["beam", "shell"]

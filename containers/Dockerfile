ARG BASE
FROM ${BASE}
# A development image contains the complete qualified SDK and native build tools.
RUN if command -v apk >/dev/null; then \
      apk add --no-cache build-base cmake ninja perl python3 ncurses-libs git; \
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

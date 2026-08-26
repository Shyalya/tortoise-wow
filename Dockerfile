# syntax=docker/dockerfile:1.7
# Tortoise WoW (Shyalya/tortoise-wow) — Ubuntu 22.04 build for GHCR + compose.
# BUILD_PLAYERBOTS controls whether the playerbots library is compiled in.
# Dynamic XP is a standalone module and is included in every image.

ARG UBUNTU_VERSION=22.04

# -----------------------------------------------------------------------------
# Builder
# -----------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS builder

ARG BUILD_PLAYERBOTS=ON
ARG USE_EXTRACTORS=OFF
ARG SOURCE_REPO=https://github.com/Shyalya/tortoise-wow.git
ARG SOURCE_REF=playerbots-integration-gh
ARG SOURCE_COMMIT=
ARG CMAKE_BUILD_TYPE=Release
ARG CMAKE_INSTALL_PREFIX=/opt/turtle
ARG BUILD_JOBS=22
ARG CPU_TARGET=x86-64-v2

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC \
    CCACHE_DIR=/root/.cache/ccache

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libace-dev \
        libboost-all-dev \
        default-libmysqlclient-dev \
        libssl-dev \
        zlib1g-dev \
        libbz2-dev \
        ccache \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# RUN git clone --depth 1 --branch "${SOURCE_REF}" "${SOURCE_REPO}" tortoise-wow
COPY . /src/tortoise-wow

WORKDIR /src/tortoise-wow

ARG EXTRACTORS_ONLY=OFF

# The upstream build currently adds -march=native. That makes a published
# image depend on the CPU that built it and can emit instructions unavailable
# on otherwise supported hosts. Keep the image portable across x86-64 CPUs.
RUN if grep -q -- '-march=native' CMakeLists.txt; then \
        sed -i "s/-march=native/-march=${CPU_TARGET}/g" CMakeLists.txt; \
    fi \
    && if grep -q -- '-march=native' CMakeLists.txt; then \
         echo "CMakeLists.txt still contains -march=native" >&2; \
         exit 1; \
       fi

# BuildKit cache mounts keep compiler results and the CMake build tree between
# builds. The cache IDs isolate incompatible build configurations.
RUN --mount=type=cache,id=tortoise-wow-ccache-${UBUNTU_VERSION}-${CPU_TARGET},target=/root/.cache/ccache \
    --mount=type=cache,id=tortoise-wow-build-${UBUNTU_VERSION}-${CMAKE_BUILD_TYPE}-${BUILD_PLAYERBOTS}-${USE_EXTRACTORS}-${EXTRACTORS_ONLY}-${CPU_TARGET},target=/src/tortoise-wow/build,sharing=locked \
    cmake -B build \
         -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
         -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}" \
         -DBUILD_PLAYERBOTS="${BUILD_PLAYERBOTS}" \
         -DMODULE_MOD_DYNAMIC_XP=static \
         -DUSE_EXTRACTORS="${USE_EXTRACTORS}" \
         -DALLOW_TURTLE_ADDONS=ON \
         -DCMAKE_C_COMPILER_LAUNCHER=ccache \
         -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
     && if [ "${EXTRACTORS_ONLY}" = "ON" ]; then \
          cmake --build build -j"${BUILD_JOBS}" --target mapextractor vmapextractor vmap_assembler MoveMapGen \
         && mkdir -p /opt/turtle/bin \
         && find build -type f -executable \
              \( -name mapextractor -o -name vmapextractor -o -name vmap_assembler -o -name MoveMapGen \) \
              -exec cp -a '{}' /opt/turtle/bin/ ';'; \
       else \
          cmake --build build -j"${BUILD_JOBS}" \
          && cmake --install build; \
        fi

# Keep SQL needed for first-time DB init + AutoUpdate path. Older upstream
# updates lived directly under database_updates; normalize those files into
# the database-specific directories scanned by the current updater.
RUN mkdir -p /opt/turtle/sql \
    && cp -a sql/create_databases.sql sql/base sql/database_updates /opt/turtle/sql/ \
    && for f in /opt/turtle/sql/database_updates/*.sql; do \
         [ -f "${f}" ] || continue; \
         case "${f}" in \
           *_world.sql) target_dir=/opt/turtle/sql/database_updates/world ;; \
           *_character.sql) target_dir=/opt/turtle/sql/database_updates/character ;; \
           *_auth.sql) target_dir=/opt/turtle/sql/database_updates/auth ;; \
           *) echo "Cannot classify migration ${f}" >&2; exit 1 ;; \
         esac; \
         mkdir -p "${target_dir}"; \
         target="${target_dir}/$(basename "${f}")"; \
         if [ -e "${target}" ]; then \
           echo "Migration collision at ${target}" >&2; exit 1; \
         fi; \
         mv "${f}" "${target}"; \
       done \
     && if [ -d src/modules/PlayerBots/sql ]; then \
          mkdir -p /opt/turtle/sql/playerbots \
          && cp -a src/modules/PlayerBots/sql/. /opt/turtle/sql/playerbots/; \
        fi

RUN if git rev-parse --verify HEAD >/dev/null 2>&1; then \
      git rev-parse HEAD; \
    elif [ -n "${SOURCE_COMMIT}" ]; then \
      printf '%s\n' "${SOURCE_COMMIT}"; \
    else \
      printf '%s\n' "${SOURCE_REF}"; \
    fi > /opt/turtle/SOURCE_COMMIT

# -----------------------------------------------------------------------------
# Runtime
# -----------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS runtime

ARG BUILD_PLAYERBOTS=ON
ARG CMAKE_INSTALL_PREFIX=/opt/turtle
ARG CPU_TARGET=x86-64-v2

LABEL org.opencontainers.image.title="tortoise-docker" \
      org.opencontainers.image.description="Turtle WoW / Tortoise server (realmd + mangosd)" \
      org.opencontainers.image.source="https://github.com/Shyalya/tortoise-wow" \
      org.opencontainers.image.licenses="GPL-2.0" \
      org.opencontainers.image.cpu.target="${CPU_TARGET}"

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC \
    TURTLE_HOME=/opt/turtle \
    PLAYERBOTS_BUILT=${BUILD_PLAYERBOTS} \
    PATH=/opt/turtle/bin:$PATH

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libace-7.0.6 \
        libboost-atomic1.74.0 \
        libboost-chrono1.74.0 \
        libboost-date-time1.74.0 \
        libboost-filesystem1.74.0 \
        libboost-iostreams1.74.0 \
        libboost-program-options1.74.0 \
        libboost-regex1.74.0 \
        libboost-serialization1.74.0 \
        libboost-system1.74.0 \
        libboost-thread1.74.0 \
        libmysqlclient21 \
        libssl3 \
        zlib1g \
        libbz2-1.0 \
        libreadline8 \
        libncurses6 \
        mariadb-client \
        tini \
        gosu \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 1000 turtle \
    && useradd --uid 1000 --gid turtle --home-dir /opt/turtle --shell /usr/sbin/nologin turtle

COPY --from=builder /opt/turtle /opt/turtle
# Keep module templates available even when Compose bind-mounts ./config over
# /opt/turtle/etc. render-config.sh copies these into the mounted config tree.
COPY modules/mod-dynamic-xp/conf/mod-dynamic-xp.conf.dist /opt/turtle/module-configs/mod-dynamic-xp.conf.dist
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
COPY docker/init-db.sh /usr/local/bin/init-db.sh
COPY docker/render-config.sh /usr/local/bin/render-config.sh
COPY docker/repair-migrations.sh /usr/local/bin/repair-migrations.sh
COPY docker/character-inventory-copy.sql /opt/turtle/sql/character-inventory-copy.sql

RUN chmod +x /usr/local/bin/entrypoint.sh \
              /usr/local/bin/init-db.sh \
              /usr/local/bin/render-config.sh \
              /usr/local/bin/repair-migrations.sh \
    && mkdir -p /opt/turtle/data /opt/turtle/logs /opt/turtle/run /var/lib/turtle-init \
    && chown -R turtle:turtle /opt/turtle /var/lib/turtle-init

WORKDIR /opt/turtle/bin

EXPOSE 3724/tcp 8090/tcp

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/entrypoint.sh"]
CMD ["mangosd"]

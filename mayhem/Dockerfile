# syntax=docker/dockerfile:1.7
# Commit image for ygopro: org C/C++ base + the data-layer build from mayhem/build.sh.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Dev headers the client's header chain needs (SQLite for the card database, liblzma for replays,
# FreeType for the GUI font header), pinned to SOURCE_DATE_EPOCH's Debian snapshot.
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends \
        libsqlite3-dev liblzma-dev libfreetype-dev \
 && rm -rf /var/lib/apt/lists/*

# Dependencies upstream pulls in as git submodules / external checkouts, fetched once from pinned commits
# (SPEC 6.2 item 19). ygopro pins its `ocgcore` submodule to a commit that predates the client code it
# ships with, so the ygopro-core commit that the client builds against is used instead. Irrlicht is
# header-only here: the data layer includes its headers but never calls into it.
ARG CORE_REV=1f7769cd40dbb13fc6ec33de7ac99757425e1f22
ARG CORE_SHA256=8f2126c406f00ff7716dbf61bd15bc0b6fbb14dfee0ba558bdeb0f7d777292c3
ARG IRR_REV=966d8756daefcc49b3333b1830049e8ae97b8c98
ARG IRR_SHA256=d5cd8f61044fa107d398bfb85a7cde5a8fb4501d716dcdaf075ae704fb93f5d5
RUN set -e; \
    d=/opt/toolchains/deps/ygopro-core-1f7769cd; mkdir -p "$d/src"; \
    curl -fsSL -o /tmp/core.tgz "https://github.com/Fluorohydride/ygopro-core/archive/${CORE_REV}.tar.gz"; \
    echo "${CORE_SHA256}  /tmp/core.tgz" | sha256sum -c -; \
    tar -xzf /tmp/core.tgz --strip-components=1 -C "$d/src"; \
    d=/opt/toolchains/deps/irrlicht-966d8756; mkdir -p "$d/src"; \
    curl -fsSL -o /tmp/irr.tgz "https://github.com/mercury233/irrlicht/archive/${IRR_REV}.tar.gz"; \
    echo "${IRR_SHA256}  /tmp/irr.tgz" | sha256sum -c -; \
    tar -xzf /tmp/irr.tgz --strip-components=1 -C "$d/src" --exclude='*/media' --exclude='*/bin' --exclude='*/examples' --exclude='*/doc'; \
    rm -f /tmp/core.tgz /tmp/irr.tgz; \
    chmod -R a+rX /opt/toolchains/deps

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem

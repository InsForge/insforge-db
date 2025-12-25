# Build stage for compiling extensions
FROM postgres:15.13 AS builder

# Install compiler tools and build dependencies
RUN apt-get update && apt-get install -y \
    postgresql-server-dev-15 \
    build-essential \
    git \
    cmake \
    wget \
    curl \
    pkg-config \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build pgvector
RUN cd /tmp && \
    git clone --branch v0.7.4 https://github.com/pgvector/pgvector.git && \
    cd pgvector && \
    make PG_CONFIG=/usr/lib/postgresql/15/bin/pg_config OPTFLAGS="" && \
    make install PG_CONFIG=/usr/lib/postgresql/15/bin/pg_config && \
    cd / && \
    rm -rf /tmp/pgvector

# Install Rust and cargo-pgrx for pg_graphql
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"
RUN cargo install --locked cargo-pgrx --version 0.12.9

# Build pg_graphql
RUN cd /tmp && \
    git clone https://github.com/supabase/pg_graphql.git && \
    cd pg_graphql && \
    git checkout v1.5.11 && \
    cargo pgrx init --pg15=/usr/lib/postgresql/15/bin/pg_config && \
    cargo pgrx install --release --pg-config=/usr/lib/postgresql/15/bin/pg_config && \
    cd / && \
    rm -rf /tmp/pg_graphql

# Final stage - clean runtime image
FROM postgres:15.13

# Add TimescaleDB repository using modern method
RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    gnupg \
    lsb-release \
    && mkdir -p /etc/apt/keyrings \
    && curl -fsSL https://packagecloud.io/timescale/timescaledb/gpgkey | gpg --dearmor -o /etc/apt/keyrings/timescaledb.gpg \
    && echo "deb [signed-by=/etc/apt/keyrings/timescaledb.gpg] https://packagecloud.io/timescale/timescaledb/debian/ $(lsb_release -cs) main" > /etc/apt/sources.list.d/timescaledb.list \
    && apt-get update \
    && rm -rf /var/lib/apt/lists/*

# Install runtime dependencies and pre-built extensions
RUN apt-get update && apt-get install -y \
    postgresql-contrib-15 \
    postgresql-15-cron \
    postgresql-15-http \
    postgresql-15-postgis-3 \
    postgis \
    timescaledb-2-postgresql-15 \
    && rm -rf /var/lib/apt/lists/*

# Copy compiled extensions from builder stage
COPY --from=builder /usr/lib/postgresql/15/lib/vector.so /usr/lib/postgresql/15/lib/
COPY --from=builder /usr/share/postgresql/15/extension/vector* /usr/share/postgresql/15/extension/
COPY --from=builder /usr/lib/postgresql/15/lib/pg_graphql.so /usr/lib/postgresql/15/lib/
COPY --from=builder /usr/share/postgresql/15/extension/pg_graphql* /usr/share/postgresql/15/extension/

# Clean up unnecessary packages
RUN apt-get purge -y lsb-release curl gnupg && \
    apt-get autoremove -y && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

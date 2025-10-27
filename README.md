# Insforge Database

Custom PostgreSQL Docker image with pre-installed extensions for the Insforge platform.

## Features

This image is based on **PostgreSQL 15.13** and includes the following extensions:

- **[pgvector](https://github.com/pgvector/pgvector)** (v0.7.4) - Vector similarity search for AI embeddings
- **[pg_graphql](https://github.com/supabase/pg_graphql)** (v1.5.11) - GraphQL API generation from PostgreSQL schemas
- **[TimescaleDB 2](https://www.timescale.com/)** - Time-series database capabilities
- **[PostGIS 3](https://postgis.net/)** - Spatial and geographic objects support
- **[pg_cron](https://github.com/citusdata/pg_cron)** - Job scheduling within PostgreSQL
- **[pgsql-http](https://github.com/pramsey/pgsql-http)** - HTTP client for making requests from SQL

## Quick Start

### Pull and run the image

```bash
docker pull ghcr.io/<owner>/postgres:latest
docker run -d \
  --name insforge-db \
  -e POSTGRES_PASSWORD=your_password \
  -p 5432:5432 \
  ghcr.io/<owner>/postgres:latest
```

### Enable extensions

```sql
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_graphql;
CREATE EXTENSION IF NOT EXISTS timescaledb;
CREATE EXTENSION IF NOT EXISTS postgis;
CREATE EXTENSION IF NOT EXISTS pg_cron;
CREATE EXTENSION IF NOT EXISTS http;
```

## Building from Source

```bash
# Clone the repository
git clone https://github.com/<owner>/insforge-db.git
cd insforge-db

# Build the image
docker build -t insforge-postgres:local ./postgres

# Run locally
docker run -d \
  --name insforge-db \
  -e POSTGRES_PASSWORD=testpass \
  -p 5432:5432 \
  insforge-postgres:local
```

## Supported Architectures

- `linux/amd64`
- `linux/arm64`

## Releases

Images are automatically built and published to GitHub Container Registry when tags are pushed:

```bash
git tag v1.0.0
git push origin v1.0.0
```

Published images are available at: `ghcr.io/<owner>/postgres`

## License

This project uses PostgreSQL and various open-source extensions. Please refer to each extension's license for details.

## Documentation

For development guidelines and detailed architecture information, see [CLAUDE.md](CLAUDE.md).
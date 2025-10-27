# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repository builds a custom PostgreSQL 15.13 Docker image with pre-installed extensions for the Insforge platform. The image is published to GitHub Container Registry (ghcr.io) and supports both AMD64 and ARM64 architectures.

## Installed Extensions

The custom Postgres image includes:
- **pgvector** (v0.7.4) - Vector similarity search for embeddings
- **pg_graphql** (v1.5.11) - GraphQL API generation from PostgreSQL schemas
- **TimescaleDB 2** - Time-series database capabilities
- **PostGIS 3** - Spatial database support
- **pg_cron** - Job scheduling within PostgreSQL
- **pgsql-http** - HTTP client for making requests from SQL

## Build Process

### Multi-stage Docker Build

The Dockerfile uses a two-stage build process:

1. **Builder Stage**: Compiles extensions from source (pgvector, pg_graphql)
   - Installs build dependencies (gcc, Rust, cargo-pgrx, etc.)
   - Compiles pgvector from source
   - Compiles pg_graphql using Rust and cargo-pgrx

2. **Final Stage**: Creates minimal runtime image
   - Copies compiled extensions from builder
   - Installs pre-built extensions from apt repositories
   - Removes build tools to minimize image size

### Building Locally

```bash
# Build the image locally
docker build -t insforge-postgres:local ./postgres

# Test the image
docker run --rm -e POSTGRES_PASSWORD=testpass -p 5432:5432 insforge-postgres:local
```

### GitHub Actions CI/CD

The repository uses GitHub Actions to automatically build and publish multi-arch images:

- **Trigger conditions**:
  - Push tags matching `v*` (e.g., `v1.0.0`)
  - Pull requests modifying `postgres/**` files
  - Manual workflow dispatch with custom test tags

- **Manual release workflow**:
```bash
# Create and push a tag to trigger build
git tag v1.0.0
git push origin v1.0.0

# Or trigger manual build via GitHub Actions UI with custom tag
```

- **Published to**: `ghcr.io/<owner>/postgres`
- **Platforms**: linux/amd64, linux/arm64

## Important Notes

### Extension Version Management

When updating extension versions:
- **pgvector**: Update git tag in line 28 of [postgres/Dockerfile](postgres/Dockerfile#L28)
- **pg_graphql**: Update git checkout in line 44 of [postgres/Dockerfile](postgres/Dockerfile#L44)
- **TimescaleDB, PostGIS, pg_cron, pg_http**: Managed via apt packages

### Build Dependencies

- **pg_graphql** requires:
  - Rust toolchain (installed via rustup)
  - cargo-pgrx v0.12.9 (must match pg_graphql compatibility)
  - PostgreSQL server development files

- **pgvector** requires:
  - GCC compiler
  - PostgreSQL server development files
  - Standard build tools (make, cmake)

### Testing Extensions

After building, verify extensions are available:

```bash
# Connect to running container
docker exec -it <container_id> psql -U postgres

# Inside psql, check available extensions
SELECT * FROM pg_available_extensions
WHERE name IN ('vector', 'pg_graphql', 'timescaledb', 'postgis', 'pg_cron', 'http');

# Enable extensions
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_graphql;
CREATE EXTENSION IF NOT EXISTS timescaledb;
CREATE EXTENSION IF NOT EXISTS postgis;
CREATE EXTENSION IF NOT EXISTS pg_cron;
CREATE EXTENSION IF NOT EXISTS http;
```

## Repository Structure

```
.
├── postgres/
│   └── Dockerfile          # Multi-stage build for custom Postgres image
└── .github/
    └── workflows/
        └── build-postgres.yml  # CI/CD for building and publishing images
```

## Common Development Tasks

### Adding a New Extension

1. Determine if the extension is available via apt or needs compilation
2. For apt-installable extensions: Add to the `apt-get install` command in line 71
3. For compiled extensions: Add build steps in builder stage, copy artifacts to final stage
4. Test the build locally before pushing
5. Update this CLAUDE.md to document the new extension

### Updating Base Postgres Version

1. Update `FROM postgres:X.Y.Z` in both builder and final stages (lines 2 and 51)
2. Update all references to `/usr/lib/postgresql/X/` paths throughout Dockerfile
3. Verify extension compatibility with new Postgres version
4. Update cargo-pgrx version if needed for pg_graphql compatibility

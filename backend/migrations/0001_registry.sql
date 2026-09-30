CREATE TABLE packages (
 name text PRIMARY KEY CHECK(name ~ '^[a-z][a-z0-9-]{1,62}[a-z0-9]$'),
 owner_name text NOT NULL, created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE releases (
 name text NOT NULL REFERENCES packages(name), version text NOT NULL,
 major integer NOT NULL, minor integer NOT NULL, patch integer NOT NULL,
 manifest jsonb NOT NULL CHECK(jsonb_typeof(manifest)='object'),
 published_at timestamptz NOT NULL DEFAULT now(), PRIMARY KEY(name,version)
);
CREATE INDEX releases_latest ON releases(name,major DESC,minor DESC,patch DESC);

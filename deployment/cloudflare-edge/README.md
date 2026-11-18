# Public Forge deployment

After both main-branch CI workflows pass, the deploy workflow builds changed
containers, verifies a Cloud Run candidate and publishes the site image to
the existing Seoul frontend bucket. Vite chunks use the public assets prefix.
Other static files use content-addressed names. The preview Worker serves the
candidate manifest before the current manifest is updated with a generation
precondition. Previous assets stay available to browsers and for rollback.

The production Worker retains its ASSETS binding as a fallback. The preview
host reads the preview manifest. Existing API, installer, live-data, security
headers and GCS release redirects use their established routes. The six
legacy package GET routes on storage.forge-lang.org serve the verified
archives in the releases bucket. Their names are the registry SHA-256 values.

GitHub Actions authenticates to Google Cloud by workload identity federation.
Its service account can create immutable site artifacts in the two publication
prefixes and replace only the preview and current manifests. No Cloudflare
credential is used by CI. Worker routing code changes require a separate
authenticated Cloudflare version upload with the current bindings, asset
routing config and `keep_assets: true`. A prior Worker version can be
redeployed without rebuilding the site.

# Forge Platform

- Prefix every Git command with GIT_MASTER=1.
- Backend routes, SQL, authorization and package resolution belong in .fg source.
- Native bridges provide only OS, protocol, crypto and resource-lifetime primitives.
- Frontend uses React, TypeScript and Tailwind.
- Never commit .env files, OAuth credentials or signing secrets.
- Use isolated test databases and container names. Existing production stacks are outside scope.
- Run scripts/test.sh and installer/package-manager integration checks before publishing.
- Keep README and API/installer docs consistent with supported platforms and real behavior.

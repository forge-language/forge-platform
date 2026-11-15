.PHONY: build test check register start stop logs
build:
	docker compose build
test:
	sh scripts/test-native.sh
check:
	docker compose run --rm --no-deps bot forge-discord-check $(GUILD)
register:
	docker compose run --rm --no-deps bot forge-discord-register $(GUILD)
start:
	docker compose up -d
stop:
	docker compose stop bot
logs:
	docker compose logs --tail=100 -f bot

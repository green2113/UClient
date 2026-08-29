# UClient rooms service

Cloudflare Worker and D1 backend for UClient accounts, bans, and chat rooms.

## Local setup

1. Run `npm install`.
2. Copy `.dev.vars.example` to `.dev.vars`.
3. Run `npm run keygen`. Put the private seed in `.dev.vars` and embed the printed public key in the UClient account component.
4. Run `npx wrangler d1 migrations apply uclient-rooms --local`.
5. Run `npm run dev`.

The `database_id` in `wrangler.jsonc` is a local placeholder. Replace it with the ID returned by `npx wrangler d1 create uclient-rooms` before deploying.

## Production secrets

Set secrets without putting them in source control:

```text
npx wrangler secret put ACCOUNT_PEPPER
npx wrangler secret put GRACE_PRIVATE_KEY_SEED_HEX
npx wrangler secret put RELAY_SECRET
npx wrangler secret put RELAY_INVALIDATE_SECRET
```

`RELAY_INVALIDATE_URL` is a non-secret Worker variable containing the relay's `/internal/rooms/invalidate` URL.

## Manual bans

`expires_at` is a Unix timestamp in seconds. Use `NULL` for a permanent ban.

```sql
INSERT INTO user_bans(install_id, reason, banned_at, expires_at, banned_by)
VALUES(
	'00000000-0000-0000-0000-000000000000',
	'Reason in English',
	unixepoch(),
	unixepoch() + 86400,
	'admin'
);
```

Remove all bans for an account:

```sql
DELETE FROM user_bans WHERE install_id = '00000000-0000-0000-0000-000000000000';
```

Run SQL remotely with `npx wrangler d1 execute uclient-rooms --remote --command "..."`.
